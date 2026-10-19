#pragma once
// Stand-in for MAME's src/devices/cpu/es5510/es5510d.h (tools/mame_ref shim). MAME's own
// es5510d.cpp is a stub whose disassemble() returns 1; this keeps that behaviour inline.
class es5510_disassembler : public util::disasm_interface {
public:
    es5510_disassembler() = default;
    u32 opcode_alignment() const override { return 1; }
    offs_t disassemble(std::ostream&, offs_t, const data_buffer&, const data_buffer&) override { return 1; }
};
