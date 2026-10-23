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

// ---- Task 3: effective addresses, data movement, address errors ---------------------------

namespace {

// Supervisor state at `pc` with the prefetch queue read from memory (M3), SSP = kSsp.
Registers stateAt(RamBus& bus, uint32_t pc, uint16_t sr = 0x2700) {
    Registers r{};
    r.ssp = kSsp;
    r.usp = 0x3000;
    r.pc = pc;
    r.sr = sr;
    r.prefetch[0] = bus.peek16(pc);
    r.prefetch[1] = bus.peek16(pc + 2);
    return r;
}

// RamBus that records every access, to check bus order and that no word access is odd.
struct LogBus : RamBus {
    struct Access {
        char kind;  // 'r' read16, 'w' write16, 'b' read8, 'B' write8
        uint32_t addr;
        uint16_t value;
    };
    std::vector<Access> log;
    int oddWordAccesses = 0;
    uint16_t read16(uint32_t addr) override {
        if (addr & 1) ++oddWordAccesses;
        const uint16_t v = RamBus::read16(addr);
        log.push_back({'r', addr, v});
        return v;
    }
    void write16(uint32_t addr, uint16_t v) override {
        if (addr & 1) ++oddWordAccesses;
        log.push_back({'w', addr, v});
        RamBus::write16(addr, v);
    }
};

}  // namespace

TEST(m68k_move_long_abs_to_reg) {
    RamBus bus;
    bus.load16(kCode, {0x2039, 0x0000, 0x2000, 0x4E71});  // move.l ($2000).l,d0
    bus.load32(0x2000, 0x12345678);
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x2713));  // X, V, C set beforehand
    CHECK_EQ(cpu.step(), 20);
    const Registers r = cpu.regs();
    CHECK_EQ(r.d[0], 0x12345678u);
    CHECK_EQ(r.sr, 0x2710);  // N = Z = V = C = 0, X unchanged
    CHECK_EQ(r.pc, kCode + 6);
    CHECK_EQ(r.prefetch[0], 0x4E71);
}

TEST(m68k_moveq_sets_flags) {
    RamBus bus;
    bus.load16(kCode, {0x70FF});  // moveq #-1,d0
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x2700));
    CHECK_EQ(cpu.step(), 4);
    const Registers r = cpu.regs();
    CHECK_EQ(r.d[0], 0xFFFFFFFFu);
    CHECK_EQ(r.sr, 0x2708);  // N
}

TEST(m68k_movem_round_trip) {
    RamBus bus;
    bus.load16(kCode, {0x48E7, 0xFFFE, 0x4CDF, 0x7FFF, 0x4E71});
    Cpu cpu(bus);
    Registers r = stateAt(bus, kCode);
    for (int i = 0; i < 8; ++i) r.d[i] = 0x10101010u * static_cast<uint32_t>(i + 1);
    for (int i = 0; i < 7; ++i) r.a[i] = 0xA0000000u + 0x01010101u * static_cast<uint32_t>(i);
    const Registers before = r;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 8 + 8 * 15);  // movem.l d0-d7/a0-a6,-(sp): UM 8+8n
    Registers mid = cpu.regs();
    CHECK_EQ(mid.ssp, kSsp - 60);
    CHECK_EQ(bus.peek32(kSsp - 60), before.d[0]);  // d0 lowest, a6 highest
    CHECK_EQ(bus.peek32(kSsp - 4), before.a[6]);
    for (int i = 0; i < 8; ++i) mid.d[i] = 0;
    for (int i = 0; i < 7; ++i) mid.a[i] = 0;
    cpu.setRegs(mid);
    CHECK_EQ(cpu.step(), 12 + 8 * 15);  // movem.l (sp)+,d0-d7/a0-a6: UM 12+8n
    const Registers s = cpu.regs();
    for (int i = 0; i < 8; ++i) CHECK_EQ(s.d[i], before.d[i]);
    for (int i = 0; i < 7; ++i) CHECK_EQ(s.a[i], before.a[i]);
    CHECK_EQ(s.ssp, kSsp);
    CHECK_EQ(s.pc, kCode + 8);
}

TEST(m68k_address_error_frame) {
    LogBus bus;
    bus.load32(3 * 4, 0x600);
    bus.load16(kCode, {0x3010});  // move.w (a0),d0
    bus.load16(0x600, {0x4E71, 0x4E72});
    Cpu cpu(bus);
    Registers r = stateAt(bus, kCode, 0x2704);
    r.a[0] = 0x1001;
    cpu.setRegs(r);
    // 50 cycles: suite MOVE.w "3010 [MOVE.w (A0), D0] 4754" (length 50: the faulting read is
    // not run, then n4, 7 writes, 2 vector reads, 2 prefetch reads and n2).
    CHECK_EQ(cpu.step(), 50);
    const Registers s = cpu.regs();
    CHECK_EQ(s.pc, 0x600u);
    CHECK_EQ(s.prefetch[0], 0x4E71);
    CHECK_EQ(s.ssp, kSsp - 14);
    const uint32_t sp = kSsp - 14;
    CHECK_EQ(bus.peek16(sp) & 0x10, 0x10);   // R/W = 1 (read)
    CHECK_EQ(bus.peek16(sp) & 0x07, 0x05);   // FC = supervisor data
    CHECK_EQ(bus.peek32(sp + 2), 0x1001u);   // access address
    CHECK_EQ(bus.peek16(sp + 6), 0x3010);    // IR
    CHECK_EQ(bus.peek16(sp + 8), 0x2704);    // SR
    CHECK_EQ(bus.peek32(sp + 10), kCode);    // PC (M11: as the suite stacks it)
    CHECK_EQ(cpu.exceptionCount(3), 1u);
    CHECK_EQ(bus.oddWordAccesses, 0);
}

TEST(m68k_double_fault_halts) {
    LogBus bus;
    bus.load32(3 * 4, 0x601);  // odd address-error vector
    bus.load16(kCode, {0x3010});
    Cpu cpu(bus);
    Registers r = stateAt(bus, kCode);
    r.a[0] = 0x1001;
    cpu.setRegs(r);
    cpu.step();
    CHECK(cpu.halted());
    CHECK_EQ(cpu.exceptionCount(3), 1u);
    CHECK_EQ(cpu.run(1000), 1000);
    CHECK_EQ(cpu.step(), 4);  // M6
    CHECK_EQ(bus.oddWordAccesses, 0);
    // Only reset() clears it.
    vectors(bus);
    bus.load16(kCode, {0x4E71});
    cpu.reset();
    CHECK(!cpu.halted());
}

TEST(m68k_reset_with_odd_pc_halts) {
    LogBus bus;
    vectors(bus, kSsp, 0x401);
    Cpu cpu(bus);
    cpu.reset();
    CHECK(cpu.halted());  // M9: an address error during reset is a double fault
    CHECK_EQ(bus.oddWordAccesses, 0);
}

TEST(m68k_odd_exception_vector_takes_address_error) {
    // Illegal with vector 4 = 0x501: the first prefetch of the handler faults, a group-1
    // exception is in progress (I/N = 1) and the address-error handler at 0x600 runs.
    LogBus bus;
    bus.load32(4 * 4, 0x501);
    bus.load32(3 * 4, 0x600);
    bus.load16(kCode, {0x4AFC});
    bus.load16(0x600, {0x4E71, 0x4E71});
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode));
    cpu.step();
    CHECK(!cpu.halted());
    CHECK_EQ(cpu.regs().pc, 0x600u);
    CHECK_EQ(cpu.exceptionCount(4), 1u);
    CHECK_EQ(cpu.exceptionCount(3), 1u);
    const uint32_t sp = kSsp - 6 - 14;
    CHECK_EQ(cpu.regs().ssp, sp);
    CHECK_EQ(bus.peek16(sp) & 0x1F, 0x1E);  // read, not an instruction, supervisor program
    CHECK_EQ(bus.peek32(sp + 2), 0x501u);
    CHECK_EQ(bus.oddWordAccesses, 0);
}

TEST(m68k_predecrement_long_write_goes_low_word_first) {
    LogBus bus;
    bus.load16(kCode, {0x2100});  // move.l d0,-(a0)
    Cpu cpu(bus);
    Registers r = stateAt(bus, kCode);
    r.d[0] = 0x11223344;
    r.a[0] = 0x2004;
    cpu.setRegs(r);
    bus.log.clear();  // stateAt's peeks
    CHECK_EQ(cpu.step(), 12);
    CHECK_EQ(cpu.regs().a[0], 0x2000u);
    // Suite MOVE.l "2100 [MOVE.l D0, -(A0)]": prefetch, low word at An+2, then high word at An.
    CHECK_EQ(bus.log.size(), 3u);
    if (bus.log.size() == 3) {
        CHECK_EQ(bus.log[0].kind, 'r');
        CHECK_EQ(bus.log[1].addr, 0x2002u);
        CHECK_EQ(bus.log[1].value, 0x3344);
        CHECK_EQ(bus.log[2].addr, 0x2000u);
        CHECK_EQ(bus.log[2].value, 0x1122);
    }
    CHECK_EQ(bus.peek32(0x2000), 0x11223344u);
}

TEST(m68k_move_usp_privilege_violation) {
    RamBus bus;
    bus.load32(8 * 4, 0x700);
    bus.load16(kCode, {0x4E60});  // move a0,usp in user mode
    Cpu cpu(bus);
    Registers r = stateAt(bus, kCode, 0x0000);
    r.a[0] = 0x1234;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 34);
    const Registers s = cpu.regs();
    CHECK_EQ(s.pc, 0x700u);
    CHECK_EQ(s.usp, 0x3000u);
    CHECK_EQ(s.ssp, kSsp - 6);
    CHECK_EQ(bus.peek32(kSsp - 4), kCode);  // the instruction's own address
    CHECK_EQ(cpu.exceptionCount(8), 1u);
    // In supervisor mode it moves.
    RamBus bus2;
    bus2.load16(kCode, {0x4E60, 0x4E69});  // move a0,usp; move usp,a1
    Cpu cpu2(bus2);
    Registers q = stateAt(bus2, kCode);
    q.a[0] = 0x1234;
    cpu2.setRegs(q);
    CHECK_EQ(cpu2.step(), 4);
    CHECK_EQ(cpu2.regs().usp, 0x1234u);
    CHECK_EQ(cpu2.step(), 4);
    CHECK_EQ(cpu2.regs().a[1], 0x1234u);
}
