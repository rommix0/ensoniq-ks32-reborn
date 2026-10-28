#pragma once
// 68000 integer and BCD arithmetic with its condition codes, and the data-dependent cycle
// counts of MULU/MULS and DIVU/DIVS. Pure functions: no CPU state.
#include <cstdint>

namespace ens::m68k {

// A result and its condition codes: X N Z V C in bits 4..0.
struct AluOut {
    uint32_t value;
    uint16_t ccr;
};

// a + b (+ X when addx) on the low `size` bytes (1, 2 or 4); value is masked to the size.
// X = C = carry out, V = signed overflow, N = sign, Z = result zero. With addx (ADDX,
// NEGX via sub) the PRM's Z is "cleared if the result is non-zero, unchanged otherwise",
// so the caller ANDs the returned Z (result zero) with the old Z.
AluOut add(uint32_t a, uint32_t b, int size, bool x, bool addx);
// a - b (- X when subx), flags as for add with borrow for carry (SUB, SUBX, CMP, NEG, NEGX).
AluOut sub(uint32_t a, uint32_t b, int size, bool x, bool subx);

// Packed-BCD a + b + X (ABCD) and a - b - X (SBCD; NBCD is sbcd(0, b, X)). X = C = decimal
// carry or borrow; Z is cleared by a non-zero result and kept (from ccrIn) otherwise.
// DECISION M2: the PRM leaves N and V undefined; they follow the suite (N = bit 7 of the
// result, V = bit 7 changed by the decimal correction).
AluOut abcd(uint8_t a, uint8_t b, bool x, uint16_t ccrIn);
AluOut sbcd(uint8_t a, uint8_t b, bool x, uint16_t ccrIn);

// Shift and rotate kinds for shift(); a left/right pair is kind and kind + 1.
enum ShiftKind : int { kAsl, kAsr, kLsl, kLsr, kRol, kRor, kRoxl, kRoxr };

// ASx, LSx, ROx and ROXx of the low `size` bytes of v by `count` places (0..63; the caller
// has applied the modulo 64 of a register count). x is the old X flag. The returned ccr has
// the new X (X is unchanged for count 0 and for ROL/ROR), N, Z, V (ASL only: the MSB changed
// at some step) and C (the last bit shifted out; 0 for count 0, but X for ROXL/ROXR).
AluOut shift(int kind, uint32_t v, int count, int size, bool x);

// MULU/MULS total clocks without the source's address calculation: 38 + 2n (UM 8.3), n the
// ones of the source (MULU) or the 01/10 pairs of the source with a 0 appended below (MULS).
int mulCycles(uint16_t src, bool isSigned);
// DIVU/DIVS total clocks without the source's address calculation, including the closing
// prefetch, for every outcome: a quotient, an overflow, or a zero divisor (38: the
// divide-by-zero exception of UM table 8-14). DECISION M10: the algorithm in decisions.md.
int divCycles(uint32_t dividend, uint16_t divisor, bool isSigned);

}  // namespace ens::m68k
