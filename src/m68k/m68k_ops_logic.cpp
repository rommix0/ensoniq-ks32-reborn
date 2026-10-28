// 68000 logic, shifts and bit operations: AND, ANDI, OR, ORI, EOR, EORI, NOT, ASL/ASR,
// LSL/LSR, ROL/ROR, ROXL/ROXR, BTST, BCHG, BCLR, BSET, Scc and TAS. Cycle counts come from
// the bus cycles each one runs plus the internal clocks of UM tables 8-4..8-9; the place of
// each internal-clock run follows the suite's transaction logs (where it is not observable
// it only has to be in the right total). Shift flags come from m68k_alu.cpp.
#include "m68k/m68k.h"
#include "m68k/m68k_alu.h"

namespace ens::m68k {

namespace {

constexpr int kAnd = 0, kOr = 1, kEor = 2;                    // opLogic kinds
constexpr int kBtst = 0, kBchg = 1, kBclr = 2, kBset = 3;     // opBit kinds

// Register direct or immediate source: the long forms of AND/OR are 2 clocks slower (UM
// table 8-4, footnote **).
bool isRegOrImm(int eaIdx) { return eaIdx <= 1 || eaIdx == 11; }

uint32_t logicOf(int kind, uint32_t a, uint32_t b) {
    return kind == kAnd ? (a & b) : kind == kOr ? (a | b) : (a ^ b);
}

// Shift kind of an opcode: tt (type) in bits 4..3 of the register form or bits 10..9 of the
// memory form, direction in bit 8 (1 = left). Types AS, LS, ROX, RO map to ShiftKind pairs.
int shiftKindOf(uint16_t op, bool memory) {
    static constexpr int kBase[4] = {kAsl, kLsl, kRoxl, kRol};
    const int tt = (op >> (memory ? 9 : 3)) & 3;
    return kBase[tt] + ((op & 0x0100) != 0 ? 0 : 1);
}

}  // namespace

// ---- AND, OR, EOR, NOT --------------------------------------------------------------------

void Cpu::opLogic(int size, int kind) {
    // <ea>,Dn: 4(1/0)+, long 6(1/0)+ or 8(1/0)+ for a register or immediate source.
    const Loc l = locate(opcode_ & 0x3F, size);
    const uint32_t src = load(l, size);
    const int r = (opcode_ >> 9) & 7;
    const uint32_t res = logicOf(kind, d_[r], src);
    setDreg(r, size, res);
    setLogicFlags(res, size);
    prefetch();
    if (size == kLong) idle(isRegOrImm(l.mode) ? 4 : 2);
}

void Cpu::opLogicToMem(int size, int kind) {
    // Dn,<ea>: 8(1/1)+ / 12(1/2)+; EOR Dn,Dn is 4(1/0) / 8(1/0).
    const Loc l = locate(opcode_ & 0x3F, size);
    const uint32_t dst = load(l, size);
    const uint32_t res = logicOf(kind, dst, d_[(opcode_ >> 9) & 7]);
    setLogicFlags(res, size);
    prefetch();
    if (l.mode == 0) {
        setDreg(l.reg, size, res);
        if (size == kLong) idle(4);
    } else {
        storeRmw(l, size, res);
    }
}

void Cpu::opLogicImm(int size, int kind) {
    // ANDI/ORI/EORI: the immediate first, then the destination's extension words. Dn:
    // 8(2/0), long 16(3/0); memory 12(2/1)+ / 20(3/2)+ (read, closing fetch, write).
    const uint32_t imm = locate(0x3C, size).addr;
    const Loc l = locate(opcode_ & 0x3F, size);
    const uint32_t dst = load(l, size);
    const uint32_t res = logicOf(kind, dst, imm);
    setLogicFlags(res, size);
    prefetch();
    if (l.mode == 0) {
        setDreg(l.reg, size, res);
        if (size == kLong) idle(4);
    } else {
        storeRmw(l, size, res);
    }
}

void Cpu::opNot(int size) {
    // UM table 8-6: Dn 4(1/0) / 6(1/0); memory 8(1/1)+ / 12(1/2)+.
    const Loc l = locate(opcode_ & 0x3F, size);
    const uint32_t res = ~load(l, size) & sizeMask(size);
    setLogicFlags(res, size);
    prefetch();
    if (l.mode == 0) {
        setDreg(l.reg, size, res);
        if (size == kLong) idle(2);
    } else {
        storeRmw(l, size, res);
    }
}

// ---- shifts and rotates -------------------------------------------------------------------

void Cpu::opShiftReg(int size) {
    // UM table 8-7: 6 + 2n (byte, word) or 8 + 2n (long), n the count. #n is 1..8 (0
    // encodes 8); a register count is Dn modulo 64 (a count of 0 changes nothing but the flags).
    const int field = (opcode_ >> 9) & 7;
    int count = field == 0 ? 8 : field;
    if ((opcode_ & 0x0020) != 0) count = static_cast<int>(d_[field] & 63u);
    const int r = opcode_ & 7;
    const AluOut o = shift(shiftKindOf(opcode_, false), d_[r], count, size, (sr_ & kCcrX) != 0);
    setDreg(r, size, o.value);
    setCcr(o.ccr);
    prefetch();
    idle((size == kLong ? 4 : 2) + 2 * count);
}

void Cpu::opShiftMem() {
    // UM table 8-7: 8(1/1)+, a word shifted by 1: read, closing fetch, write.
    const Loc l = locate(opcode_ & 0x3F, kWord);
    const uint32_t v = load(l, kWord);
    const AluOut o = shift(shiftKindOf(opcode_, true), v, 1, kWord, (sr_ & kCcrX) != 0);
    setCcr(o.ccr);
    prefetch();
    storeRmw(l, kWord, o.value);
}

// ---- BTST, BCHG, BCLR, BSET ---------------------------------------------------------------

void Cpu::opBit(int kind, bool immediate) {
    // The bit number: the immediate extension word (fetched before the operand's) or Dn.
    // A data register tests the bit modulo 32, a memory byte modulo 8 (UM, PRM).
    const uint32_t number = immediate ? locate(0x3C, kWord).addr : d_[(opcode_ >> 9) & 7];
    const Loc l = locate(opcode_ & 0x3F, kByte);
    const bool reg = l.mode == 0;
    const uint32_t bit = number & (reg ? 31u : 7u);
    const uint32_t mask = 1u << bit;
    const uint32_t v = reg ? d_[l.reg] : load(l, kByte);
    const uint32_t res = kind == kBchg ? (v ^ mask) : kind == kBclr ? (v & ~mask) : (v | mask);
    setCcr(static_cast<uint16_t>((sr_ & ~kCcrZ) | ((v & mask) == 0 ? kCcrZ : 0)));
    prefetch();
    if (reg || l.mode == 11) {  // a register, or BTST Dn,#imm, which runs as a register operand
        if (reg && kind != kBtst) d_[l.reg] = res;
        // DECISION M17: UM table 8-8 gives the register forms as maxima (footnote *): BTST 6,
        // BCHG/BSET 8, BCLR 10, the immediate-number forms 4 more. Every suite case with a
        // bit number below 16 takes 2 clocks less for BCHG, BSET and BCLR; BTST is 6 always.
        const int high = bit >= 16 ? 2 : 0;
        idle(kind == kBtst ? 2 : kind == kBclr ? 4 + high : 2 + high);
        return;
    }
    if (kind != kBtst) storeRmw(l, kByte, res);
}

// ---- Scc, TAS -----------------------------------------------------------------------------

bool Cpu::condition(int cc) const {
    const bool c = (sr_ & kCcrC) != 0, v = (sr_ & kCcrV) != 0;
    const bool z = (sr_ & kCcrZ) != 0, n = (sr_ & kCcrN) != 0;
    switch (cc & 15) {
    case 0: return true;
    case 1: return false;
    case 2: return !c && !z;
    case 3: return c || z;
    case 4: return !c;
    case 5: return c;
    case 6: return !z;
    case 7: return z;
    case 8: return !v;
    case 9: return v;
    case 10: return !n;
    case 11: return n;
    case 12: return n == v;
    case 13: return n != v;
    case 14: return !z && n == v;
    default: return z || n != v;
    }
}

void Cpu::opScc() {
    // UM table 8-6: Dn 4(1/0) false, 6(1/0) true; memory 8(1/1)+ either way. The memory
    // form reads the byte first (suite Scc "52e5 [Scc -(A5)] 1"), then the closing fetch, then writes.
    const bool set = condition((opcode_ >> 8) & 15);
    const uint32_t value = set ? 0xFFu : 0u;
    const Loc l = locate(opcode_ & 0x3F, kByte);
    if (l.mode == 0) {
        setDreg(l.reg, kByte, value);
        prefetch();
        if (set) idle(2);
        return;
    }
    (void)load(l, kByte);
    prefetch();
    storeRmw(l, kByte, value);
}

void Cpu::opTas() {
    // UM table 8-6: Dn 4(1/0). Memory: DECISION M20: 10(1/1)+, not the table's 14(2/1)+ (that
    // counts the operand read twice: the effective address time of (An) already holds it). The
    // read-modify-write is a read, 2 internal clocks and a write, then the closing fetch (suite
    // TAS: one 10-clock locked cycle, then the fetch). No bus lock signal on Bus: two plain accesses.
    const Loc l = locate(opcode_ & 0x3F, kByte);
    const uint32_t v = l.mode == 0 ? (d_[l.reg] & 0xFFu) : read8(l.addr);
    uint16_t ccr = static_cast<uint16_t>(sr_ & kCcrX);
    if (v == 0) ccr |= kCcrZ;
    if ((v & 0x80u) != 0) ccr |= kCcrN;
    setCcr(ccr);
    if (l.mode == 0) {
        setDreg(l.reg, kByte, v | 0x80u);
        prefetch();
        return;
    }
    idle(2);
    write8(l.addr, static_cast<uint8_t>(v | 0x80u));
    prefetch();
}

}  // namespace ens::m68k
