#pragma once
// ES5510 ESP ALU: pure functions over 24-bit two's-complement words.
#include <cstdint>

namespace ens::esp {

enum class Op : uint8_t {
    ADD, SUB, ADDU, SUBU, CMP, AND, OR, XOR, ABS, MOV, ASL2, ASL8, LS15, DIFF, ASR, END
};

// CCR flags. CCR/CMR bit layout: N 23, C 22, V 21, LT 20, Z 19 (CMR only: NOT 18).
struct Flags {
    bool n = false, c = false, v = false, lt = false, z = false;
};

struct AluResult {
    uint32_t value;  // 24-bit result (meaningless when writes == false, e.g. END)
    Flags flags;     // CCR after the operation (equal to `prev` where the op leaves a flag alone)
    bool writes;     // false for CMP and END
};

// a and b are 24-bit; one-operand ops (ABS, MOV, ASL2, ASL8, LS15, DIFF, ASR) use only b.
AluResult alu(Op op, uint32_t a, uint32_t b, Flags prev);

uint32_t packCcr(const Flags& f);  // -> bits 23..19
Flags unpackCcr(uint32_t ccr);

// SKIP = NOTm xor ((N&Nm)|(Z&Zm)|(V&Vm)|(C&Cm)|(LT&LTm)); CMR NOT is bit 18.
bool skipCondition(uint32_t ccr, uint32_t cmr);

}  // namespace ens::esp
