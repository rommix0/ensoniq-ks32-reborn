#pragma once
// 68000 opcode classification. decodeOp is pure: the Cpu builds its dispatch table from it
// and the debug-trace disassembler uses it to decide what is a valid first word.
#include <cstdint>

namespace ens::m68k {

// Instruction class of a first opcode word. Later tasks add enumerators and decode cases.
// decodeOp has already checked the effective-address modes an instruction allows (PRM), so
// an executing instruction never sees a mode it does not support.
enum class Op : uint16_t {
    Illegal,  // not the first word of a legal instruction, incl. 4AFA/4AFB/4AFC (vector 4)
    LineA,    // 1010 xxxx xxxx xxxx: unimplemented instruction, vector 10
    LineF,    // 1111 xxxx xxxx xxxx: unimplemented instruction, vector 11
    Nop,      // 4E71
    // Data movement (Task 3).
    MoveB, MoveW, MoveL,  // 00ss DDD MMM <ea>, destination not An
    MoveaW, MoveaL,       // 00ss AAA 001 <ea>
    Moveq,                // 0111 DDD0 dddddddd
    Lea,                  // 0100 AAA1 11 <control ea>
    Pea,                  // 0100 1000 01 <control ea>
    ClrB, ClrW, ClrL,     // 0100 0010 ss <data alterable ea>
    Exg,                  // 1100 xxx1 oooo oyyy: Dx/Dy, Ax/Ay, Dx/Ay
    Swap,                 // 4840 + Dn
    ExtW, ExtL,           // 4880 / 48C0 + Dn
    Link,                 // 4E50 + An, #d16
    Unlk,                 // 4E58 + An
    MoveToUsp,            // 4E60 + An (privileged)
    MoveFromUsp,          // 4E68 + An (privileged)
    MovemToMemW, MovemToMemL,  // 0100 1000 1s <ea>: registers to memory
    MovemToRegW, MovemToRegL,  // 0100 1100 1s <ea>: memory to registers
    MovepToRegW, MovepToRegL,  // 0000 DDD1 0s00 1AAA, #d16
    MovepToMemW, MovepToMemL,  // 0000 DDD1 1s00 1AAA, #d16
};

Op decodeOp(uint16_t opcode);

// Index of an effective-address field (mode in bits 5..3, register in bits 2..0):
// 0 Dn, 1 An, 2 (An), 3 (An)+, 4 -(An), 5 (d16,An), 6 (d8,An,Xn), 7 (xxx).W, 8 (xxx).L,
// 9 (d16,PC), 10 (d8,PC,Xn), 11 #imm; -1 for the reserved mode 7 registers 5..7.
constexpr int eaIndex(uint16_t field) {
    const int mode = (field >> 3) & 7;
    const int reg = field & 7;
    if (mode < 7) return mode;
    return reg <= 4 ? 7 + reg : -1;
}

// Sets of eaIndex values (bit i = index i), the PRM's addressing categories.
constexpr uint16_t kEaAll = 0x0FFF;
constexpr uint16_t kEaData = 0x0FFD;              // all but An
constexpr uint16_t kEaControl = 0x07E4;           // (An), (d16,An), (d8,An,Xn), abs, PC-relative
constexpr uint16_t kEaDataAlterable = 0x01FD;     // Dn and the alterable memory modes
constexpr uint16_t kEaMemoryAlterable = 0x01FC;
constexpr uint16_t kEaControlAlterable = 0x01E4;

constexpr bool eaIn(uint16_t field, uint16_t set) {
    const int i = eaIndex(field);
    return i >= 0 && ((set >> i) & 1) != 0;
}

}  // namespace ens::m68k
