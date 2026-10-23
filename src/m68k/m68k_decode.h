#pragma once
// 68000 opcode classification. decodeOp is pure: the Cpu builds its dispatch table from it
// and the debug-trace disassembler uses it to decide what is a valid first word.
#include <cstdint>

namespace ens::m68k {

// Instruction class of a first opcode word. Later tasks add enumerators and decode cases.
enum class Op : uint16_t {
    Illegal,  // not the first word of a legal instruction, incl. 4AFA/4AFB/4AFC (vector 4)
    LineA,    // 1010 xxxx xxxx xxxx: unimplemented instruction, vector 10
    LineF,    // 1111 xxxx xxxx xxxx: unimplemented instruction, vector 11
    Nop,      // 4E71
};

Op decodeOp(uint16_t opcode);

}  // namespace ens::m68k
