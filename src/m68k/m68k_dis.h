#pragma once
// 68000 disassembler for the emulator's debug trace (design section 5): it shows which
// instruction the emulated machine is on when it hangs. Motorola syntax, lower case. Pure
// text from a word reader; it knows nothing of the ROM and is not a listing tool.
#include <cstdint>
#include <functional>
#include <string>

namespace ens::m68k {

struct DisLine {
    int length;        // bytes consumed, 2..10
    std::string text;  // e.g. "move.l  #$c11138,($100).w"
};

// Disassembles the instruction at `addr`, reading its words (and only those) through `read16`.
// Words decodeOp calls Illegal, LineA or LineF print as "dc.w    $xxxx" (length 2); 4AFC, the
// architected ILLEGAL instruction, prints as "illegal".
DisLine disassemble(const std::function<uint16_t(uint32_t)>& read16, uint32_t addr);

}  // namespace ens::m68k
