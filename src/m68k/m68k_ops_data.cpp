// 68000 data movement: MOVE, MOVEA, MOVEQ, LEA, PEA, CLR, EXG, SWAP, EXT, LINK, UNLK,
// MOVE USP, MOVEM and MOVEP. Cycle counts come from the bus cycles each one runs plus the
// internal clocks of UM tables 8-1..8-3, 8-6, 8-10, 8-12 and 8-13. Where the bus order
// matters (an address error stacks the PC and the clocks run before the fault), it is the
// suite's (DECISION M11, M13).
#include "m68k/m68k.h"

namespace ens::m68k {

namespace {

bool isMemoryMode(int eaIdx) { return eaIdx >= 2 && eaIdx <= 10; }

}  // namespace

void Cpu::opMove(int size) {
    const uint16_t op = opcode_;
    const uint16_t src = op & 0x3F;
    const uint32_t v = readEa(src, size);
    const int r = (op >> 9) & 7;
    const uint32_t stepBy = (size == kByte && r == 7) ? 2u : static_cast<uint32_t>(size);
    const auto write = [this, size](uint32_t addr, uint32_t value) {
        if (size == kByte) write8(addr, static_cast<uint8_t>(value));
        else if (size == kWord) write16(addr, static_cast<uint16_t>(value));
        else write32(addr, value);
    };
    uint32_t addr = 0;
    switch ((op >> 6) & 7) {  // destination mode (An is MOVEA, reserved modes are illegal)
    case 0:
        setDreg(r, size, v);
        setLogicFlags(v, size);
        prefetch();
        return;
    case 2:
    case 3: addr = a_[r]; break;  // (An)+ steps An after the write (below)
    case 4:
        // UM table 8-2: no internal clocks for a -(An) destination. The closing fetch comes
        // first, then the write; a long goes low word first, An stepping by 2 per word
        // (suite MOVE.l "2506 [MOVE.l D6, -(A2)] 92": an odd A2 faults at A2 - 2 and keeps it).
        prefetch();
        setLogicFlags(v, size);
        if (size == kLong) {
            a_[r] -= 2;
            write16(a_[r], static_cast<uint16_t>(v));
            a_[r] -= 2;
            write16(a_[r], static_cast<uint16_t>(v >> 16));
        } else {
            a_[r] -= stepBy;
            write(a_[r], v);
        }
        return;
    case 5: addr = a_[r] + signExtend(nextWord(), kWord); break;
    case 6:
        idle(2);
        addr = indexWord(a_[r]);
        break;
    default:  // mode 7: register 0 (xxx).W, 1 (xxx).L
        if (r == 0) {
            addr = signExtend(nextWord(), kWord);
            break;
        }
        {
            const uint32_t hi = nextWord();
            if (isMemoryMode(eaIndex(src))) {
                // From memory the write goes before the fetch that replaces the low address
                // word in IRC (suite MOVE.l "23d0 [MOVE.l (A0), (xxx).l] 675").
                setLogicFlags(v, size);
                write((hi << 16) | irc_, v);
                nextWord();
                prefetch();
                return;
            }
            addr = (hi << 16) | nextWord();
        }
        break;
    }
    // The flags are set before the write: an address error on it stacks the new CCR (suite
    // MOVE.w "3080 [MOVE.w D0, (A0)] 4994").
    setLogicFlags(v, size);
    write(addr, v);
    // An (An)+ destination steps after the write: a faulting write leaves An as it was
    // (suite MOVE.l "22cb [MOVE.l A3, (A1)+] 32"; a source (An)+ steps before its read).
    if (((op >> 6) & 7) == 3) a_[r] += stepBy;
    prefetch();
}

void Cpu::opMovea(int size) {
    const uint32_t v = readEa(opcode_ & 0x3F, size);
    a_[(opcode_ >> 9) & 7] = signExtend(v, size);
    prefetch();
}

void Cpu::opMoveq() {
    const uint32_t v = signExtend(opcode_, kByte);
    d_[(opcode_ >> 9) & 7] = v;
    setLogicFlags(v, kLong);
    prefetch();  // 4(1/0)
}

void Cpu::opLea() {
    // UM table 8-10: the index modes take 4 internal clocks here (12(2/0)).
    a_[(opcode_ >> 9) & 7] = controlAddress(opcode_ & 0x3F, 4);
    prefetch();
}

void Cpu::opPea() {
    // UM table 8-10; the push goes high word first (suite PEA "4879 [PEA (xxx).l] 30").
    const uint32_t addr = controlAddress(opcode_ & 0x3F, 4);
    a_[7] -= 4;
    write32(a_[7], addr);
    prefetch();
}

void Cpu::opClr(int size) {
    const Loc l = locate(opcode_ & 0x3F, size);
    if (l.mode == 0) {
        if (size == kLong) idle(2);  // UM table 8-6: CLR.l Dn 6(1/0)
        setDreg(l.reg, size, 0);
        setLogicFlags(0, size);
        prefetch();
        return;
    }
    // The 68000 reads the operand before clearing it: read, closing fetch, write
    // (suite CLR.w "4254 [CLR.w (A4)] 6"; UM table 8-6: 8(1/1)+ and 12(1/2)+).
    (void)load(l, size);
    setLogicFlags(0, size);
    prefetch();
    store(l, size, 0);
}

void Cpu::opExg() {
    const int x = (opcode_ >> 9) & 7, y = opcode_ & 7;
    uint32_t* rx = nullptr;
    uint32_t* ry = nullptr;
    switch ((opcode_ >> 3) & 0x1F) {
    case 0x08: rx = &d_[x]; ry = &d_[y]; break;
    case 0x09: rx = &a_[x]; ry = &a_[y]; break;
    default: rx = &d_[x]; ry = &a_[y]; break;  // 0x11 (decodeOp admits no other)
    }
    const uint32_t t = *rx;
    *rx = *ry;
    *ry = t;
    prefetch();
    idle(2);  // UM table 8-12: 6(1/0)
}

void Cpu::opSwap() {
    uint32_t& d = d_[opcode_ & 7];
    d = (d << 16) | (d >> 16);
    setLogicFlags(d, kLong);
    prefetch();
}

void Cpu::opExt(int size) {
    const int r = opcode_ & 7;
    if (size == kWord) {
        setDreg(r, kWord, signExtend(d_[r], kByte));
    } else {
        d_[r] = signExtend(d_[r], kWord);
    }
    setLogicFlags(d_[r], size);
    prefetch();
}

void Cpu::opLink() {
    // PRM LINK: SP - 4 -> SP; An -> (SP); SP -> An; SP + d16 -> SP. 16(2/2). For LINK A7 the
    // pushed value is the decremented SP.
    const int r = opcode_ & 7;
    const uint32_t disp = signExtend(nextWord(), kWord);
    a_[7] -= 4;
    write32(a_[7], a_[r]);
    a_[r] = a_[7];
    a_[7] += disp;
    prefetch();
}

void Cpu::opUnlk() {
    // PRM UNLK: An -> SP; (SP) -> An; SP + 4 -> SP. 12(3/0). For UNLK A7 the loaded value wins.
    const int r = opcode_ & 7;
    a_[7] = a_[r];
    const uint32_t v = read32(a_[7]);
    a_[7] += 4;
    a_[r] = v;
    prefetch();
}

void Cpu::opMoveUsp(bool toUsp) {
    if (!supervisor()) {
        privilegeViolation();
        return;
    }
    const int r = opcode_ & 7;
    if (toUsp) otherSp_ = a_[r];  // in supervisor mode the USP is the inactive stack pointer
    else a_[r] = otherSp_;
    prefetch();  // 4(1/0)
}

void Cpu::opMovemToMem(int size) {
    // UM table 8-10: 8+4n / 8+8n plus the address calculation (2 internal clocks for the
    // index modes, none for -(An)).
    const uint16_t mask = nextWord();
    const uint16_t field = opcode_ & 0x3F;
    if (eaIndex(field) == 4) {
        // -(An): mask bit 0 is A7, bit 15 is D0; registers go to descending addresses, a
        // long low word first (suite MOVEM.l "48e0 [MOVEM.l #, -(A0)] 34"). An is written
        // once at the end, so a stored An is its initial value and a fault leaves An as it was.
        const int r = field & 7;
        uint32_t addr = a_[r];
        for (int i = 0; i < 16; ++i) {
            if (((mask >> i) & 1) == 0) continue;
            const uint32_t v = i < 8 ? a_[7 - i] : d_[15 - i];
            addr -= static_cast<uint32_t>(size);
            if (size == kLong) write32Desc(addr, v);
            else write16(addr, static_cast<uint16_t>(v));
        }
        a_[r] = addr;
    } else {
        uint32_t addr = controlAddress(field, 2);
        for (int i = 0; i < 16; ++i) {
            if (((mask >> i) & 1) == 0) continue;
            const uint32_t v = i < 8 ? d_[i] : a_[i - 8];
            if (size == kLong) write32(addr, v);
            else write16(addr, static_cast<uint16_t>(v));
            addr += static_cast<uint32_t>(size);
        }
    }
    prefetch();
}

void Cpu::opMovemToReg(int size) {
    // UM table 8-10: 12+4n / 12+8n plus the address calculation. The 12 include one extra
    // word read past the last register. Words are sign-extended into the whole register.
    const uint16_t mask = nextWord();
    const uint16_t field = opcode_ & 0x3F;
    const int mode = eaIndex(field);
    const bool program = mode == 9 || mode == 10;  // DECISION M12
    uint32_t addr = mode == 3 ? a_[field & 7] : controlAddress(field, 2);
    // (An)+: An runs one word ahead of the first read, so an odd An faults with An + 2
    // (suite MOVEM.w "4c98 [MOVEM.w (A0)+, #] 1"); the final address is written at the end.
    if (mode == 3) a_[field & 7] = addr + 2;
    for (int i = 0; i < 16; ++i) {
        if (((mask >> i) & 1) == 0) continue;
        const uint32_t v = size == kLong ? read32(addr, program) : signExtend(read16(addr, program), kWord);
        if (i < 8) d_[i] = v;
        else a_[i - 8] = v;
        addr += static_cast<uint32_t>(size);
    }
    (void)read16(addr, program);
    if (mode == 3) a_[field & 7] = addr;  // (An)+: the final address wins over a loaded An
    prefetch();
}

void Cpu::opMovepToReg(int size) {
    // UM table 8-13: 16(4/0) / 24(6/0). Bytes at every other address, high byte first.
    const uint32_t addr = a_[opcode_ & 7] + signExtend(nextWord(), kWord);
    const int n = size;  // 2 or 4 bytes
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | read8(addr + 2u * static_cast<uint32_t>(i));
    setDreg((opcode_ >> 9) & 7, size, v);
    prefetch();
}

void Cpu::opMovepToMem(int size) {
    // UM table 8-13: 16(2/2) / 24(2/4).
    const uint32_t addr = a_[opcode_ & 7] + signExtend(nextWord(), kWord);
    const uint32_t v = d_[(opcode_ >> 9) & 7];
    const int n = size;
    for (int i = 0; i < n; ++i) {
        write8(addr + 2u * static_cast<uint32_t>(i), static_cast<uint8_t>(v >> (8 * (n - 1 - i))));
    }
    prefetch();
}

}  // namespace ens::m68k
