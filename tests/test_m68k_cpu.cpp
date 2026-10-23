#include <cstdint>

#include "check.h"
#include "m68k/m68k.h"
#include "m68k_test_bus.h"

using ens::m68k::Cpu;
using ens::m68k::Registers;

namespace {

constexpr uint32_t kSsp = 0x1000;
constexpr uint32_t kCode = 0x400;

// Reset vectors: SSP at 0, PC at 4.
void vectors(RamBus& bus, uint32_t ssp = kSsp, uint32_t pc = kCode) {
    bus.load32(0, ssp);
    bus.load32(4, pc);
}

}  // namespace

TEST(m68k_reset_loads_ssp_and_pc) {
    RamBus bus;
    vectors(bus);
    bus.load16(kCode, {0x4E71, 0x4E72});
    Cpu cpu(bus);
    cpu.reset();
    const Registers r = cpu.regs();
    CHECK_EQ(r.ssp, 0x1000u);
    CHECK_EQ(r.pc, 0x400u);  // M3: the address of the next instruction
    CHECK_EQ(r.prefetch[0], 0x4E71);
    CHECK_EQ(r.prefetch[1], 0x4E72);
    CHECK_EQ(r.sr, 0x2700);
    CHECK_EQ(cpu.totalCycles(), 40u);
    CHECK(!cpu.halted());
    CHECK(!cpu.stopped());
}

TEST(m68k_nop_takes_4_cycles) {
    RamBus bus;
    vectors(bus);
    bus.load16(kCode, {0x4E71, 0x4E71, 0x1234});
    Cpu cpu(bus);
    cpu.reset();
    CHECK_EQ(cpu.step(), 4);
    CHECK_EQ(cpu.instructionAddress(), 0x400u);
    CHECK_EQ(cpu.step(), 4);
    CHECK_EQ(cpu.instructionAddress(), 0x402u);
    const Registers r = cpu.regs();
    CHECK_EQ(r.pc, 0x404u);
    CHECK_EQ(r.prefetch[0], 0x1234);
    CHECK_EQ(r.prefetch[1], 0x0000);
    CHECK_EQ(cpu.totalCycles(), 48u);
}

TEST(m68k_illegal_takes_vector_4) {
    RamBus bus;
    vectors(bus);
    bus.load32(4 * 4, 0x500);
    bus.load16(kCode, {0x4AFC});
    bus.load16(0x500, {0x4E71, 0x4E72});
    Cpu cpu(bus);
    cpu.reset();
    CHECK_EQ(cpu.step(), 34);
    const Registers r = cpu.regs();
    CHECK_EQ(r.pc, 0x500u);
    CHECK_EQ(r.prefetch[0], 0x4E71);
    CHECK_EQ(r.prefetch[1], 0x4E72);
    CHECK_EQ(r.ssp, kSsp - 6);
    CHECK_EQ(r.sr, 0x2700);
    CHECK_EQ(bus.peek16(0xFFA), 0x2700);
    CHECK_EQ(bus.peek32(0xFFC), 0x400u);
    CHECK_EQ(cpu.exceptionCount(4), 1u);
    CHECK_EQ(cpu.exceptionCount(10), 0u);
    CHECK_EQ(cpu.instructionAddress(), 0x400u);
}

TEST(m68k_other_illegal_patterns_take_vector_4) {
    // 4AFA and 4AFB (UM 6.3.6, reserved by Motorola) take vector 4 like ILLEGAL (4AFC).
    for (uint16_t op : {uint16_t(0x4AFA), uint16_t(0x4AFB)}) {
        RamBus bus;
        vectors(bus);
        bus.load32(4 * 4, 0x500);
        bus.load16(kCode, {op});
        Cpu cpu(bus);
        cpu.reset();
        CHECK_EQ(cpu.step(), 34);
        CHECK_EQ(cpu.regs().pc, 0x500u);
        CHECK_EQ(cpu.exceptionCount(4), 1u);
    }
}

TEST(m68k_line_a_and_f_vectors) {
    // A000 at 0x400 -> vector 10 (handler 0x500, which holds F000) -> vector 11 (0x600).
    RamBus bus;
    vectors(bus);
    bus.load32(10 * 4, 0x500);
    bus.load32(11 * 4, 0x600);
    bus.load16(kCode, {0xA000});
    bus.load16(0x500, {0xF000});
    bus.load16(0x600, {0x4E71});
    Cpu cpu(bus);
    cpu.reset();
    CHECK_EQ(cpu.step(), 34);
    CHECK_EQ(cpu.regs().pc, 0x500u);
    CHECK_EQ(cpu.regs().ssp, kSsp - 6);
    CHECK_EQ(bus.peek16(0xFFA), 0x2700);
    CHECK_EQ(bus.peek32(0xFFC), 0x400u);
    CHECK_EQ(cpu.exceptionCount(10), 1u);
    CHECK_EQ(cpu.step(), 34);
    CHECK_EQ(cpu.regs().pc, 0x600u);
    CHECK_EQ(cpu.regs().ssp, kSsp - 12);
    CHECK_EQ(bus.peek16(0xFF4), 0x2700);
    CHECK_EQ(bus.peek32(0xFF6), 0x500u);
    CHECK_EQ(cpu.exceptionCount(11), 1u);
    CHECK_EQ(cpu.exceptionCount(4), 0u);
}

TEST(m68k_exception_from_user_mode_switches_stack_and_clears_t) {
    // User mode with T set: the frame goes on the SSP, the USP is kept, S = 1 and T = 0.
    RamBus bus;
    bus.load32(4 * 4, 0x500);
    bus.load16(0x800, {0x4AFC});
    Cpu cpu(bus);
    Registers r{};
    r.usp = 0x3000;
    r.ssp = 0x2000;
    r.pc = 0x800;
    r.sr = 0x8015;  // T, user, X N C
    r.prefetch[0] = 0x4AFC;
    r.prefetch[1] = 0x0000;
    r.d[3] = 0xDEADBEEF;
    r.a[6] = 0x12345678;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 34);
    const Registers s = cpu.regs();
    CHECK_EQ(s.sr, 0x2015);
    CHECK_EQ(s.usp, 0x3000u);
    CHECK_EQ(s.ssp, 0x2000u - 6);
    CHECK_EQ(s.pc, 0x500u);
    CHECK_EQ(s.d[3], 0xDEADBEEFu);
    CHECK_EQ(s.a[6], 0x12345678u);
    CHECK_EQ(bus.peek16(0x1FFA), 0x8015);
    CHECK_EQ(bus.peek32(0x1FFC), 0x800u);
}

TEST(m68k_set_regs_round_trips) {
    RamBus bus;
    Cpu cpu(bus);
    Registers r{};
    for (int i = 0; i < 8; ++i) r.d[i] = 0x11111111u * static_cast<uint32_t>(i);
    for (int i = 0; i < 7; ++i) r.a[i] = 0x01010101u * static_cast<uint32_t>(i + 1);
    r.usp = 0xAAAA;
    r.ssp = 0xBBBB;
    r.pc = 0xC00;
    r.sr = 0x0004;  // user mode, Z
    r.prefetch[0] = 0x4E71;
    r.prefetch[1] = 0x5555;
    cpu.setRegs(r);
    const Registers s = cpu.regs();
    for (int i = 0; i < 8; ++i) CHECK_EQ(s.d[i], r.d[i]);
    for (int i = 0; i < 7; ++i) CHECK_EQ(s.a[i], r.a[i]);
    CHECK_EQ(s.usp, 0xAAAAu);
    CHECK_EQ(s.ssp, 0xBBBBu);
    CHECK_EQ(s.pc, 0xC00u);
    CHECK_EQ(s.sr, 0x0004);
    CHECK_EQ(s.prefetch[0], 0x4E71);
    CHECK_EQ(s.prefetch[1], 0x5555);
    // The prefetch words come from setRegs, not memory (which is 0 = ORI, illegal for now).
    bus.load16(0xC04, {0x7777});
    CHECK_EQ(cpu.step(), 4);
    CHECK_EQ(cpu.regs().pc, 0xC02u);
    CHECK_EQ(cpu.regs().prefetch[0], 0x5555);
    CHECK_EQ(cpu.regs().prefetch[1], 0x7777);
}

TEST(m68k_run_returns_whole_instructions) {
    {
        RamBus bus;
        vectors(bus);
        bus.load32(4 * 4, 0x500);
        bus.load16(kCode, {0x4AFC});
        Cpu cpu(bus);
        cpu.reset();
        CHECK_EQ(cpu.run(1), 34);
        CHECK_EQ(cpu.totalCycles(), 40u + 34u);
    }
    {
        RamBus bus;
        vectors(bus);
        bus.load16(kCode, {0x4E71, 0x4E71, 0x4E71, 0x4E71});
        Cpu cpu(bus);
        cpu.reset();
        CHECK_EQ(cpu.run(10), 12);
        CHECK_EQ(cpu.instructionAddress(), 0x404u);
        CHECK_EQ(cpu.regs().pc, 0x406u);
        CHECK_EQ(cpu.run(0), 0);
        CHECK_EQ(cpu.totalCycles(), 40u + 12u);
    }
}

TEST(m68k_on_instruction_hook_sees_each_address) {
    RamBus bus;
    vectors(bus);
    bus.load32(4 * 4, 0x500);
    bus.load16(kCode, {0x4E71, 0x4AFC});
    Cpu cpu(bus);
    uint32_t seen[4] = {};
    int n = 0;
    cpu.onInstruction = [&](uint32_t pc) {
        if (n < 4) seen[n] = pc;
        ++n;
    };
    cpu.reset();
    CHECK_EQ(n, 0);  // reset is not an instruction
    cpu.step();
    cpu.step();
    CHECK_EQ(n, 2);
    CHECK_EQ(seen[0], 0x400u);
    CHECK_EQ(seen[1], 0x402u);
}

TEST(m68k_two_instances_independent) {
    RamBus busA, busB;
    vectors(busA, 0x1000, 0x400);
    vectors(busB, 0x2000, 0x800);
    busA.load16(0x400, {0x4E71, 0x4E71, 0x4E71});
    busB.load32(4 * 4, 0x900);
    busB.load16(0x800, {0x4AFC});
    Cpu a(busA), b(busB);
    a.reset();
    b.reset();
    CHECK_EQ(a.step(), 4);
    CHECK_EQ(b.step(), 34);
    CHECK_EQ(a.step(), 4);
    const Registers ra = a.regs(), rb = b.regs();
    CHECK_EQ(ra.pc, 0x404u);
    CHECK_EQ(ra.ssp, 0x1000u);
    CHECK_EQ(a.exceptionCount(4), 0u);
    CHECK_EQ(a.totalCycles(), 48u);
    CHECK_EQ(rb.pc, 0x900u);
    CHECK_EQ(rb.ssp, 0x2000u - 6);
    CHECK_EQ(b.exceptionCount(4), 1u);
    CHECK_EQ(b.totalCycles(), 74u);
    CHECK_EQ(busA.peek32(0xFFC), 0u);  // B's frame did not land in A's memory
    CHECK_EQ(busB.peek32(0x1FFC), 0x800u);
}
