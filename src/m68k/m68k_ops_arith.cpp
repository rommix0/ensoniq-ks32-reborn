// 68000 integer arithmetic, compare, BCD, multiply, divide and CHK: ADD, ADDA, ADDI, ADDQ,
// ADDX, SUB, SUBA, SUBI, SUBQ, SUBX, CMP, CMPA, CMPI, CMPM, NEG, NEGX, TST, MULU, MULS,
// DIVU, DIVS, ABCD, SBCD, NBCD, CHK. Cycle counts come from the bus cycles each one runs
// plus the internal clocks of UM tables 8-4..8-6, 8-11, 8-12 and 8-14; the place of each
// internal-clock run follows the suite's transaction logs (where it is not observable it
// only has to be in the right total). The flags come from m68k_alu.cpp.
#include "m68k/m68k.h"
#include "m68k/m68k_alu.h"

namespace ens::m68k {

namespace {

constexpr int kVectorDivideByZero = 5;
constexpr int kVectorChk = 6;

// Register direct (Dn, An) or immediate: the operand modes that make the long forms of
// ADD/SUB/ADDA/SUBA 2 clocks slower (UM table 8-4, footnote **).
bool isRegOrImm(int eaIdx) { return eaIdx <= 1 || eaIdx == 11; }

AluOut addOrSub(bool isSub, uint32_t a, uint32_t b, int size, bool x, bool extended) {
    return isSub ? sub(a, b, size, x, extended) : add(a, b, size, x, extended);
}

}  // namespace

// ---- shared tails -------------------------------------------------------------------------

void Cpu::setCmpFlags(uint32_t a, uint32_t b, int size) {
    const AluOut o = sub(a, b, size, false, false);
    setCcr(static_cast<uint16_t>((o.ccr & ~kCcrX) | (sr_ & kCcrX)));  // CMP leaves X
}

uint32_t Cpu::readPredec(int r, int size, uint32_t& addr) {
    // -(An) for the memory forms of ADDX/SUBX/ABCD/SBCD, after the one 2-clock idle the
    // caller runs. A long is read low word first, An stepping by 2 per word, so an odd An
    // faults at An - 2 and keeps that value (suite ADDX.l "db8a [ADDX.l -(A2), -(A5)] 7").
    if (size == kLong) {
        a_[r] -= 2;
        const uint32_t lo = read16(a_[r]);
        a_[r] -= 2;
        addr = a_[r];
        return (static_cast<uint32_t>(read16(addr)) << 16) | lo;
    }
    a_[r] -= (size == kByte && r != 7) ? 1u : 2u;  // A7 steps a byte by 2
    addr = a_[r];
    return size == kByte ? read8(addr) : read16(addr);
}

// ---- ADD, SUB and their address, immediate, quick and extended forms ----------------------

void Cpu::opAddSub(int size, bool isSub) {
    // <ea>,Dn: 4(1/0)+, long 6(1/0)+ or 8(1/0)+ for a register or immediate source.
    const Loc l = locate(opcode_ & 0x3F, size);
    const uint32_t src = load(l, size);
    const int r = (opcode_ >> 9) & 7;
    const AluOut o = addOrSub(isSub, d_[r], src, size, false, false);
    setDreg(r, size, o.value);
    setCcr(o.ccr);
    prefetch();
    if (size == kLong) idle(isRegOrImm(l.mode) ? 4 : 2);
}

void Cpu::opAddSubToMem(int size, bool isSub) {
    // Dn,<ea>: 8(1/1)+ / 12(1/2)+. Read, closing fetch, write (suite ADD.l "d392 [ADD.l D1, (A2)]").
    const Loc l = locate(opcode_ & 0x3F, size);
    const uint32_t dst = load(l, size);
    const AluOut o = addOrSub(isSub, dst, d_[(opcode_ >> 9) & 7], size, false, false);
    setCcr(o.ccr);
    prefetch();
    storeRmw(l, size, o.value);
}

void Cpu::opAddaSuba(int size, bool isSub) {
    // ADDA/SUBA: 8(1/0)+ for a word source, 6(1/0)+ or 8(1/0)+ for a long one. The word
    // source is sign-extended and the whole An changes; no flags.
    const Loc l = locate(opcode_ & 0x3F, size);
    const uint32_t src = signExtend(load(l, size), size);
    uint32_t& an = a_[(opcode_ >> 9) & 7];
    an = isSub ? an - src : an + src;
    prefetch();
    idle(size == kWord || isRegOrImm(l.mode) ? 4 : 2);
}

void Cpu::opAddSubImm(int size, bool isSub) {
    // ADDI/SUBI: the immediate first, then the destination's extension words. Dn: 8(2/0),
    // long 16(3/0); memory: 12(2/1)+ / 20(3/2)+ (read, closing fetch, write).
    const uint32_t imm = locate(0x3C, size).addr;
    const Loc l = locate(opcode_ & 0x3F, size);
    const uint32_t dst = load(l, size);
    const AluOut o = addOrSub(isSub, dst, imm, size, false, false);
    setCcr(o.ccr);
    prefetch();
    if (l.mode == 0) {
        setDreg(l.reg, size, o.value);
        if (size == kLong) idle(4);
    } else {
        storeRmw(l, size, o.value);
    }
}

void Cpu::opAddSubQuick(int size, bool isSub) {
    // ADDQ/SUBQ #1..8: Dn 4(1/0), long 8(1/0); memory 8(1/1)+ / 12(1/2)+.
    const uint32_t data = (opcode_ >> 9) & 7u;
    const uint32_t q = data == 0 ? 8u : data;  // 0 encodes 8
    const Loc l = locate(opcode_ & 0x3F, size);
    if (l.mode == 1) {
        // To An: the whole register, no flags (a word form is a long operation).
        uint32_t& an = a_[l.reg];
        an = isSub ? an - q : an + q;
        prefetch();
        // DECISION M14: word 8 clocks, long 6 as the suite has it (UM table 8-5 says 8 for
        // the long form, and 4 for ADDQ.w An, which the suite and SUBQ's own row contradict).
        idle(size == kLong ? 2 : 4);
        return;
    }
    const uint32_t dst = load(l, size);
    const AluOut o = addOrSub(isSub, dst, q, size, false, false);
    setCcr(o.ccr);
    prefetch();
    if (l.mode == 0) {
        setDreg(l.reg, size, o.value);
        if (size == kLong) idle(4);
    } else {
        storeRmw(l, size, o.value);
    }
}

void Cpu::opAddxSubx(int size, bool isSub) {
    // UM table 8-11: Dy,Dx 4(1/0) / 8(1/0); -(Ay),-(Ax) 18(3/1) / 30(5/2).
    const int rx = (opcode_ >> 9) & 7, ry = opcode_ & 7;
    const bool x = (sr_ & kCcrX) != 0;
    const uint16_t oldZ = static_cast<uint16_t>(sr_ & kCcrZ);
    if ((opcode_ & 0x0008) == 0) {
        const AluOut o = addOrSub(isSub, d_[rx], d_[ry], size, x, true);
        setDreg(rx, size, o.value);
        setCcr(static_cast<uint16_t>((o.ccr & ~kCcrZ) | (o.ccr & oldZ)));  // Z only cleared
        prefetch();
        if (size == kLong) idle(4);
        return;
    }
    // One 2-clock idle for both predecrements; read source, read destination; byte and
    // word: closing fetch, write; long: low word, closing fetch, high word (suite ADDX.l
    // "d98e [ADDX.l -(A6), -(A4)] 1").
    idle(2);
    uint32_t srcAddr = 0, dstAddr = 0;
    const uint32_t src = readPredec(ry, size, srcAddr);
    const uint32_t dst = readPredec(rx, size, dstAddr);
    const AluOut o = addOrSub(isSub, dst, src, size, x, true);
    setCcr(static_cast<uint16_t>((o.ccr & ~kCcrZ) | (o.ccr & oldZ)));
    if (size == kLong) {
        write16(dstAddr + 2, static_cast<uint16_t>(o.value));
        prefetch();
        write16(dstAddr, static_cast<uint16_t>(o.value >> 16));
        return;
    }
    prefetch();
    if (size == kByte) write8(dstAddr, static_cast<uint8_t>(o.value));
    else write16(dstAddr, static_cast<uint16_t>(o.value));
}

// ---- compares and TST ---------------------------------------------------------------------

void Cpu::opCmp(int size) {
    // UM table 8-4: 4(1/0)+, long 6(1/0)+.
    const uint32_t src = readEa(opcode_ & 0x3F, size);
    setCmpFlags(d_[(opcode_ >> 9) & 7], src, size);
    prefetch();
    if (size == kLong) idle(2);
}

void Cpu::opCmpa(int size) {
    // UM table 8-4: 6(1/0)+ for both sizes; a word source is sign-extended, the compare is long.
    const uint32_t src = signExtend(readEa(opcode_ & 0x3F, size), size);
    setCmpFlags(a_[(opcode_ >> 9) & 7], src, kLong);
    prefetch();
    idle(2);
}

void Cpu::opCmpi(int size) {
    // UM table 8-5: Dn 8(2/0) / 14(3/0); memory 8(2/0)+ / 12(3/0)+ (no write).
    const uint32_t imm = locate(0x3C, size).addr;
    const Loc l = locate(opcode_ & 0x3F, size);
    setCmpFlags(load(l, size), imm, size);
    prefetch();
    if (l.mode == 0 && size == kLong) idle(2);
}

void Cpu::opCmpm(int size) {
    // UM table 8-11: 12(3/0) / 20(5/0). (Ay)+ is located and read before (Ax)+, each
    // stepping before its read (suite CMP.l "b789 [CMP.l (A1)+, (A3)+] 287").
    const Loc s = locate(static_cast<uint16_t>(0x18 | (opcode_ & 7)), size);
    const uint32_t src = load(s, size);
    const Loc d = locate(static_cast<uint16_t>(0x18 | ((opcode_ >> 9) & 7)), size);
    const uint32_t dst = load(d, size);
    setCmpFlags(dst, src, size);
    prefetch();
}

void Cpu::opTst(int size) {
    // UM table 8-6: 4(1/0)+.
    setLogicFlags(readEa(opcode_ & 0x3F, size), size);
    prefetch();
}

// ---- NEG, NEGX, NBCD, ABCD, SBCD ----------------------------------------------------------

void Cpu::opNeg(int size, bool negx) {
    // UM table 8-6: Dn 4(1/0) / 6(1/0); memory 8(1/1)+ / 12(1/2)+.
    const Loc l = locate(opcode_ & 0x3F, size);
    const uint32_t v = load(l, size);
    const uint16_t oldZ = static_cast<uint16_t>(sr_ & kCcrZ);
    const AluOut o = sub(0, v, size, (sr_ & kCcrX) != 0, negx);
    setCcr(negx ? static_cast<uint16_t>((o.ccr & ~kCcrZ) | (o.ccr & oldZ)) : o.ccr);
    if (l.mode == 0) {
        setDreg(l.reg, size, o.value);
        prefetch();
        if (size == kLong) idle(2);
        return;
    }
    prefetch();
    storeRmw(l, size, o.value);
}

void Cpu::opNbcd() {
    // UM table 8-6: Dn 6(1/0); memory 8(1/1)+.
    const Loc l = locate(opcode_ & 0x3F, kByte);
    const uint32_t v = load(l, kByte);
    const AluOut o = sbcd(0, static_cast<uint8_t>(v), (sr_ & kCcrX) != 0, sr_);
    setCcr(o.ccr);
    if (l.mode == 0) {
        setDreg(l.reg, kByte, o.value);
        prefetch();
        idle(2);
        return;
    }
    prefetch();
    storeRmw(l, kByte, o.value);
}

void Cpu::opAbcdSbcd(bool isSub) {
    // UM table 8-11: Dy,Dx 6(1/0); -(Ay),-(Ax) 18(3/1), ordered as ADDX.b.
    const int rx = (opcode_ >> 9) & 7, ry = opcode_ & 7;
    const bool x = (sr_ & kCcrX) != 0;
    const auto bcd = [&](uint32_t a, uint32_t b) {
        return isSub ? sbcd(static_cast<uint8_t>(a), static_cast<uint8_t>(b), x, sr_)
                     : abcd(static_cast<uint8_t>(a), static_cast<uint8_t>(b), x, sr_);
    };
    if ((opcode_ & 0x0008) == 0) {
        const AluOut o = bcd(d_[rx], d_[ry]);
        setDreg(rx, kByte, o.value);
        setCcr(o.ccr);
        prefetch();
        idle(2);
        return;
    }
    idle(2);
    uint32_t srcAddr = 0, dstAddr = 0;
    const uint32_t src = readPredec(ry, kByte, srcAddr);
    const uint32_t dst = readPredec(rx, kByte, dstAddr);
    const AluOut o = bcd(dst, src);
    setCcr(o.ccr);
    prefetch();
    write8(dstAddr, static_cast<uint8_t>(o.value));
}

// ---- MULU, MULS, DIVU, DIVS, CHK ----------------------------------------------------------

void Cpu::opMul(bool isSigned) {
    // UM table 8-4: 38 + 2n (mulCycles) + <ea>. Closing fetch, then the internal clocks
    // (suite MULU "c4e1 [MULU -(A1), D2] 143").
    const uint32_t src = readEa(opcode_ & 0x3F, kWord);
    uint32_t& dn = d_[(opcode_ >> 9) & 7];
    prefetch();
    idle(mulCycles(static_cast<uint16_t>(src), isSigned) - 4);
    // Unsigned 32-bit products of the (sign-extended) operands: the low 32 bits are the
    // two's-complement product, without signed overflow.
    const uint32_t a = isSigned ? signExtend(dn, kWord) : (dn & 0xFFFF);
    const uint32_t b = isSigned ? signExtend(src, kWord) : src;
    dn = a * b;
    setLogicFlags(dn, kLong);
}

void Cpu::opDiv(bool isSigned) {
    // UM table 8-4: at most 140 (DIVU) / 158 (DIVS) + <ea>; the exact count is divCycles
    // (DECISION M10). Internal clocks, then the closing fetch (suite DIVU "84c1 [DIVU D1, D2]").
    const uint32_t divisor = readEa(opcode_ & 0x3F, kWord);
    uint32_t& dn = d_[(opcode_ >> 9) & 7];
    const uint32_t dividend = dn;
    if (divisor == 0) {
        // UM table 8-14: 38(4/3)+. DECISION M2: N Z V C cleared, X kept (the suite's only
        // divide-by-zero case, DIVU "80ef [DIVU (d16, A7), D0] 5745"). The stacked PC is
        // the next instruction's address (UM 6.3.5); DECISION M15: the suite's case stacks
        // the instruction's own address instead.
        setCcr(static_cast<uint16_t>(sr_ & kCcrX));
        idle(4);
        exception(kVectorDivideByZero, pc_);
        return;
    }
    idle(divCycles(dividend, static_cast<uint16_t>(divisor), isSigned) - 4);
    uint32_t quot = 0, rem = 0;
    bool overflow = false;
    if (!isSigned) {
        quot = dividend / divisor;
        rem = dividend % divisor;
        overflow = quot > 0xFFFF;
    } else {
        // Magnitudes in unsigned arithmetic: 0x80000000 / -1 is well defined here.
        const bool dvdNeg = (dividend & 0x80000000u) != 0;
        const bool dvsNeg = (divisor & 0x8000u) != 0;
        const uint32_t absDvd = dvdNeg ? 0u - dividend : dividend;
        const uint32_t absDvs = dvsNeg ? 0x10000u - divisor : divisor;
        const uint32_t absQuot = absDvd / absDvs;
        const uint32_t absRem = absDvd % absDvs;
        const bool negQuot = dvdNeg != dvsNeg;
        overflow = absQuot > (negQuot ? 0x8000u : 0x7FFFu);
        quot = negQuot ? 0u - absQuot : absQuot;
        rem = dvdNeg ? 0u - absRem : absRem;  // the remainder has the dividend's sign
    }
    if (overflow) {
        // PRM: V set, C cleared, the operand unchanged. DECISION M2: N and Z (undefined)
        // are left unchanged, as in every overflow case of the suite.
        setCcr(static_cast<uint16_t>((sr_ & (kCcrX | kCcrN | kCcrZ)) | kCcrV));
    } else {
        dn = ((rem & 0xFFFF) << 16) | (quot & 0xFFFF);
        setLogicFlags(quot, kWord);
    }
    prefetch();
}

void Cpu::opChk() {
    // CHK <ea>,Dn (word): no trap 10(1/0)+; trap (UM table 8-14) 40(4/3)+. The closing
    // fetch runs first and the frame stacks the next instruction's address (suite CHK).
    // DECISION M16: a trap for Dn > bound takes 38 + <ea>, one for Dn < 0 (within the bound)
    // 40 + <ea>, as the suite has it; the UM's single 40 covers only the second case.
    const uint32_t bound = signExtend(readEa(opcode_ & 0x3F, kWord), kWord);
    const uint32_t dn = signExtend(d_[(opcode_ >> 9) & 7], kWord);
    prefetch();
    // DECISION M2: the PRM defines N only when trapping (set for Dn < 0, cleared for Dn >
    // bound) and leaves Z, V, C undefined. The suite: N = (Dn < 0) on every trap, N kept
    // without one, V = C = 0, Z = 0 in every case (all have Dn != 0); Z = (Dn == 0) here.
    const bool negative = (dn & 0x80000000u) != 0;
    uint16_t ccr = static_cast<uint16_t>(sr_ & (kCcrX | kCcrN));
    if ((dn & 0xFFFF) == 0) ccr |= kCcrZ;
    const auto signedGreater = [](uint32_t a, uint32_t b) {
        return (a ^ 0x80000000u) > (b ^ 0x80000000u);
    };
    if (signedGreater(dn, bound)) {
        // Above the bound (also when negative): the trap starts at once, 38 + <ea> in all
        // (DECISION M16; suite CHK "4581 [CHK D1, D2] 443").
        setCcr(static_cast<uint16_t>((ccr & ~kCcrN) | (negative ? kCcrN : 0)));
        exception(kVectorChk, pc_ - 2);
    } else if (negative) {
        // Below zero: 2 more clocks, 40 + <ea> (the UM's figure; DECISION M16).
        setCcr(static_cast<uint16_t>(ccr | kCcrN));
        idle(2);
        exception(kVectorChk, pc_ - 2);
    } else {
        setCcr(ccr);
        idle(6);
    }
}

}  // namespace ens::m68k
