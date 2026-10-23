// 68000 integer and BCD arithmetic, flags and MUL/DIV cycle counts (pure functions).
// All arithmetic is on uint32_t: no signed overflow, no shift by the operand width.
#include "m68k/m68k_alu.h"

namespace ens::m68k {

namespace {

constexpr uint16_t kX = 0x10, kN = 0x08, kZ = 0x04, kV = 0x02, kC = 0x01;

uint32_t maskOf(int size) { return size == 1 ? 0xFFu : size == 2 ? 0xFFFFu : 0xFFFFFFFFu; }
uint32_t msbOf(int size) { return size == 1 ? 0x80u : size == 2 ? 0x8000u : 0x80000000u; }

// N and Z of a masked result, plus V and C (X = C).
uint16_t flags(uint32_t r, uint32_t msb, bool v, bool c) {
    uint16_t ccr = 0;
    if ((r & msb) != 0) ccr |= kN;
    if (r == 0) ccr |= kZ;
    if (v) ccr |= kV;
    if (c) ccr |= kC | kX;
    return ccr;
}

}  // namespace

AluOut add(uint32_t a, uint32_t b, int size, bool x, bool addx) {
    const uint32_t m = maskOf(size), msb = msbOf(size);
    a &= m;
    b &= m;
    const uint32_t cin = (addx && x) ? 1u : 0u;
    const uint32_t r = (a + b + cin) & m;
    // Carry out of the top bit: from the operands' and the result's top bits (PRM ADD).
    const bool c = (((a & b) | (~r & (a | b))) & msb) != 0;
    const bool v = (((a ^ r) & (b ^ r)) & msb) != 0;
    return {r, flags(r, msb, v, c)};
}

AluOut sub(uint32_t a, uint32_t b, int size, bool x, bool subx) {
    const uint32_t m = maskOf(size), msb = msbOf(size);
    a &= m;
    b &= m;
    const uint32_t bin = (subx && x) ? 1u : 0u;
    const uint32_t r = (a - b - bin) & m;
    const bool c = (((~a & b) | (r & (~a | b))) & msb) != 0;  // borrow (PRM SUB)
    const bool v = (((a ^ b) & (a ^ r)) & msb) != 0;
    return {r, flags(r, msb, v, c)};
}

// DECISION M2: N and V of ABCD/SBCD/NBCD follow the suite (every ABCD, SBCD and NBCD case
// with register operands matches): the binary sum or difference is decimal-corrected by
// 0x06 (low digit above 9, or a half carry/borrow) and 0x60 (decimal carry/borrow);
// N = bit 7 of the corrected result, V = bit 7 turned on (ABCD) or off (SBCD) by the correction.
AluOut abcd(uint8_t a, uint8_t b, bool x, uint16_t ccrIn) {
    const uint32_t bin = uint32_t(a) + b + (x ? 1u : 0u);
    uint32_t corr = 0;
    if (((a ^ b ^ bin) & 0x10) != 0 || (bin & 0x0F) > 9) corr = 0x06;
    const bool c = bin > 0x99;
    if (c) corr |= 0x60;
    const uint32_t r = (bin + corr) & 0xFF;
    uint16_t ccr = c ? (kX | kC) : 0;
    if ((r & 0x80) != 0) ccr |= kN;
    if ((~bin & r & 0x80) != 0) ccr |= kV;
    if (r == 0) ccr |= ccrIn & kZ;
    return {r, ccr};
}

AluOut sbcd(uint8_t a, uint8_t b, bool x, uint16_t ccrIn) {
    const uint32_t bin = (uint32_t(a) - b - (x ? 1u : 0u)) & 0xFFFF;
    uint32_t corr = 0;
    if (((a ^ b ^ bin) & 0x10) != 0) corr = 0x06;
    bool c = (bin & 0x100) != 0;  // binary borrow
    if (c) corr |= 0x60;
    const uint32_t full = (bin - corr) & 0xFFFF;
    if (!c && (full & 0x100) != 0) c = true;  // the low-digit correction borrowed
    const uint32_t r = full & 0xFF;
    uint16_t ccr = c ? (kX | kC) : 0;
    if ((r & 0x80) != 0) ccr |= kN;
    if ((bin & ~full & 0x80) != 0) ccr |= kV;
    if (r == 0) ccr |= ccrIn & kZ;
    return {r, ccr};
}

int mulCycles(uint16_t src, bool isSigned) {
    // UM 8.3: 38 + 2n. MULS: n = 01 or 10 pairs of the 17 bits src:0.
    const uint32_t bits = isSigned ? ((uint32_t(src) << 1) ^ uint32_t(src)) & 0xFFFF : src;
    int n = 0;
    for (uint32_t v = bits; v != 0; v &= v - 1) ++n;
    return 38 + 2 * n;
}

int divCycles(uint32_t dividend, uint16_t divisor, bool isSigned) {
    // DECISION M10: totals in clocks (closing prefetch included, address calculation not).
    // The UM gives only the bounds (140 DIVU, 158 DIVS) and "less than 10% difference
    // between best and worst"; this is the shift-and-subtract loop that reproduces every
    // register-operand case of the suite (DIVU 1149/1149, DIVS 1220/1220).
    if (divisor == 0) return 38;  // UM table 8-14: divide by zero, exception included
    if (!isSigned) {
        if ((dividend >> 16) >= divisor) return 10;  // overflow is detected before the loop
        int units = 38;  // 2-clock units
        const uint32_t hdivisor = uint32_t(divisor) << 16;
        for (int i = 0; i < 15; ++i) {
            const bool carry = (dividend & 0x80000000u) != 0;
            dividend <<= 1;
            if (carry) {
                dividend -= hdivisor;
            } else {
                units += 2;
                if (dividend >= hdivisor) {
                    dividend -= hdivisor;
                    --units;
                }
            }
        }
        return units * 2;
    }
    const bool dvdNeg = (dividend & 0x80000000u) != 0;
    const bool dvsNeg = (divisor & 0x8000u) != 0;
    const uint32_t absDvd = dvdNeg ? 0u - dividend : dividend;
    const uint32_t absDvs = dvsNeg ? 0x10000u - divisor : divisor;
    int units = dvdNeg ? 7 : 6;  // 2-clock units
    const uint32_t absQuot = absDvd / absDvs;
    // Overflow: the quotient does not fit in 16 bits signed. The suite exits at the same
    // early point for every overflow, also when only the signed range is exceeded.
    const bool negQuot = dvdNeg != dvsNeg;
    if (absQuot > (negQuot ? 0x8000u : 0x7FFFu)) return (units + 2) * 2;
    units += 55;
    if (!dvsNeg) units += dvdNeg ? 1 : -1;
    for (int bit = 15; bit >= 1; --bit) {  // one step for each 0 in quotient bits 15..1
        if (((absQuot >> bit) & 1u) == 0) ++units;
    }
    return units * 2;
}

}  // namespace ens::m68k
