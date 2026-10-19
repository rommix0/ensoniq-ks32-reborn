#pragma once
// ES5510 ESP microinstruction word: field encode/decode and a one-line disassembler
// (ESP notes section 4.1). Pure functions, no I/O.
#include <cstdint>
#include <string>

#include "esp/esp_agen.h"
#include "esp/esp_alu.h"

namespace ens::esp {

// 48-bit word: D 47..40, C 39..32, B 31..24, A 23..16, opcode 15..12, operand select 11..8,
// Skip 7, MAC 6, RAM control 5..3, bits 2..0 unused (decode ignores them, encode writes 0).
struct Instr {
    uint8_t d{}, c{}, b{}, a{};
    Op op{Op::ADD};
    uint8_t opsel{};  // 0..15, the operand select table row
    bool skip{}, mac{};
    RamOp ram{RamOp::ReadDelay};

    static Instr decode(uint64_t w48);
    uint64_t encode() const;
};

// Operand select field (bits 11..8): notes section 4.1.3 table rows 0..F. The single copy of
// the table, shared by the execution core and the disassembler.
struct OpSelect {
    bool aluSrcDil;  // ALU X-bus (A-side) input is DIL instead of reg A
    bool aluToA, aluToFifo;
    bool mulSrcDil;  // MUL X-bus (C-side) input is DIL instead of reg C
    bool mulToC, mulToFifo;
};
const OpSelect& opSelect(unsigned row);  // row is taken modulo 16

// One line, no newline:
//   "PC  OP    srcB, srcA > dest[,DOL] | MUL D x C > dest[,DOL] [+MAC] | RAM <op> [SKIP]"
// PC is 3 decimal digits. DIL/DOL appear as the operand-select table says. One-operand ops
// (ABS MOV ASL2 ASL8 LS15 DIFF ASR) print "OP srcB > dest"; CMP has no destination; END has
// no ALU part.
std::string disassemble(const Instr& in, int pc);

// Name of a register address 0..255: "gpr12", "SER0L", "MACH", "DIL/MEMSIZ", "-1", "?200".
std::string regName(uint8_t addr);

}  // namespace ens::esp
