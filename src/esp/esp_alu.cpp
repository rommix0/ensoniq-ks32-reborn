#include "esp/esp_alu.h"

#include "common/bits.h"

namespace ens::esp {

namespace {

constexpr uint32_t kSign = 0x800000;
constexpr int32_t kMax = 0x7FFFFF;
constexpr int32_t kMin = -0x800000;

bool neg(uint32_t v) { return (v & kSign) != 0; }

// Unsaturated A +/- B (ADDU, SUBU, CMP, DIFF): wrapped result, V = signed overflow,
// LT = N xor V, N/Z from the wrapped result.
// DECISION E16: unsaturated ops take N, Z from the wrapped result, V = signed overflow, LT = N xor V
AluResult wrapped(int64_t sa, int64_t sb, bool sub, bool carry) {
    const int64_t exact = sub ? sa - sb : sa + sb;
    AluResult r{};
    r.value = mask24(exact);
    r.flags.n = neg(r.value);
    r.flags.z = r.value == 0;
    r.flags.v = exact > kMax || exact < kMin;
    r.flags.lt = r.flags.n != r.flags.v;
    r.flags.c = carry;
    r.writes = true;
    return r;
}

// ASL2/ASL8: saturating left shift; V = saturation occurred. C and LT unchanged.
AluResult shiftLeft(uint32_t b, int n, Flags prev) {
    const int64_t exact = static_cast<int64_t>(sext(b, 24)) * (int64_t(1) << n);
    const int32_t s = sat(exact, 24);
    AluResult r{};
    r.value = mask24(s);
    r.flags = prev;
    r.flags.n = neg(r.value);
    r.flags.z = r.value == 0;
    r.flags.v = exact != s;  // DECISION E13: V=1 only when saturation actually clipped the result
    r.writes = true;
    return r;
}

}  // namespace

AluResult alu(Op op, uint32_t a, uint32_t b, Flags prev) {
    a &= 0xFFFFFF;
    b &= 0xFFFFFF;
    const int64_t sa = sext(a, 24);
    const int64_t sb = sext(b, 24);
    AluResult r{};
    r.flags = prev;
    r.writes = true;

    switch (op) {
        case Op::ADD: {
            const int64_t exact = sa + sb;
            const int32_t s = sat(exact, 24);
            r.value = mask24(s);
            r.flags.n = neg(r.value);  // DECISION E16: N, Z from the stored (saturated) result
            r.flags.z = r.value == 0;
            r.flags.v = exact != s;    // DECISION E16: V = saturation occurred
            r.flags.lt = exact < 0;    // DECISION E16: LT = sign of the infinite-precision result
            r.flags.c = (a + b) > 0xFFFFFF;  // DECISION E1: ADD carry = unsigned carry out of bit 23
            return r;
        }
        case Op::SUB: {
            const int64_t exact = sa - sb;
            const int32_t s = sat(exact, 24);
            r.value = mask24(s);
            r.flags.n = neg(r.value);
            r.flags.z = r.value == 0;
            r.flags.v = exact != s;
            r.flags.lt = exact < 0;
            r.flags.c = a < b;  // DECISION E1: SUB carry = unsigned borrow (A < B)
            return r;
        }
        case Op::ADDU:
            return wrapped(sa, sb, false, (a + b) > 0xFFFFFF);  // DECISION E1: carry out of bit 23
        case Op::SUBU:
            return wrapped(sa, sb, true, a < b);  // DECISION E1: borrow
        case Op::CMP: {
            AluResult c = wrapped(sa, sb, true, a < b);  // DECISION E1: borrow
            c.writes = false;
            return c;
        }
        case Op::AND:
        case Op::OR:
        case Op::XOR:
            r.value = op == Op::AND ? (a & b) : op == Op::OR ? (a | b) : (a ^ b);
            r.flags.n = neg(r.value);  // DECISION E15: logic ops set N, Z only; C, V, LT unchanged
            r.flags.z = r.value == 0;
            return r;
        case Op::ABS:
            // DECISION E3: negative B gives its one's complement; N=0, C=sign of B, Z/V/LT unchanged
            r.value = neg(b) ? (~b & 0xFFFFFF) : b;
            r.flags.n = false;
            r.flags.c = neg(b);
            return r;
        case Op::MOV:
            r.value = b;  // no CCR effect
            return r;
        case Op::ASL2:
            return shiftLeft(b, 2, prev);
        case Op::ASL8:
            return shiftLeft(b, 8, prev);
        case Op::LS15:
            // DECISION E14: A = (B << 15) & 0x7FFFFF, N=0, C = bit 8 of B; Z/V/LT unchanged
            r.value = (b << 15) & 0x7FFFFF;
            r.flags.n = false;
            r.flags.c = ((b >> 8) & 1) != 0;
            return r;
        case Op::DIFF: {
            // DECISION E2: DIFF is unsaturated, A = (0x7FFFFF - B) & 0xFFFFFF, flags as SUBU(0x7FFFFF, B)
            return wrapped(kMax, sb, true, static_cast<uint32_t>(kMax) < b);
        }
        case Op::ASR:
            // DECISION E15: A = B >> 1 (arithmetic), N from result, C = bit 0 of B; Z/V/LT unchanged
            r.value = mask24(sext(b, 24) >> 1);
            r.flags.n = neg(r.value);
            r.flags.c = (b & 1) != 0;
            return r;
        case Op::END:
            r.value = a;
            r.writes = false;
            return r;
    }
    return r;
}

uint32_t packCcr(const Flags& f) {
    return (f.n ? 1u << 23 : 0) | (f.c ? 1u << 22 : 0) | (f.v ? 1u << 21 : 0) |
           (f.lt ? 1u << 20 : 0) | (f.z ? 1u << 19 : 0);
}

Flags unpackCcr(uint32_t ccr) {
    Flags f;
    f.n = (ccr >> 23) & 1;
    f.c = (ccr >> 22) & 1;
    f.v = (ccr >> 21) & 1;
    f.lt = (ccr >> 20) & 1;
    f.z = (ccr >> 19) & 1;
    return f;
}

bool skipCondition(uint32_t ccr, uint32_t cmr) {
    // DECISION E21: literal hardware equation; mask 0x04 (TRUE) always skips, 0x00 (FALSE) never
    const bool any = (ccr & cmr & 0xF80000u) != 0;
    const bool notm = ((cmr >> 18) & 1) != 0;
    return notm != any;
}

}  // namespace ens::esp
