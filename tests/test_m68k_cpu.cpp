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

TEST(m68k_alu_divs_overflow_leaves_operand) {
    // divs.w d1,d0: 0x80000000 / -1 = +2^31 does not fit in 16 bits. V = 1, C = 0, N and Z
    // unchanged (DECISION M2), d0 unchanged. Well-defined arithmetic on both builds.
    RamBus bus;
    bus.load16(kCode, {0x81C1, 0x4E71, 0x4E71});
    Cpu cpu(bus);
    Registers r = stateAt(bus, kCode, 0x271D);  // X N Z C set
    r.d[0] = 0x80000000;
    r.d[1] = 0x0000FFFF;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 18);  // M10: overflow found early, negative dividend
    const Registers s = cpu.regs();
    CHECK_EQ(s.d[0], 0x80000000u);
    CHECK_EQ(s.sr & 0x1F, 0x1E);  // X N Z kept, V set, C cleared
    CHECK_EQ(s.pc, kCode + 2);
}

TEST(m68k_divu_by_zero_traps) {
    // divu.w d1,d0 with d1 = 0: vector 5, the next instruction's address stacked (UM 6.3.5),
    // 38 cycles (UM table 8-14), N Z V C cleared (DECISION M2), d0 unchanged.
    RamBus bus;
    bus.load32(5 * 4, 0x700);
    bus.load16(kCode, {0x80C1, 0x4E71});
    bus.load16(0x700, {0x4E71, 0x4E71});
    Cpu cpu(bus);
    Registers r = stateAt(bus, kCode, 0x271F);
    r.d[0] = 0x12345678;
    r.d[1] = 0xFFFF0000;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 38);
    const Registers s = cpu.regs();
    CHECK_EQ(s.pc, 0x700u);
    CHECK_EQ(s.d[0], 0x12345678u);
    CHECK_EQ(s.ssp, kSsp - 6);
    CHECK_EQ(bus.peek16(kSsp - 6), 0x2710);  // the stacked SR has the cleared flags
    CHECK_EQ(bus.peek32(kSsp - 4), kCode + 2);
    CHECK_EQ(cpu.exceptionCount(5), 1u);
}

TEST(m68k_add_reg_long_cycles) {
    RamBus bus;
    bus.load16(kCode, {0xD280, 0x4E71, 0x4E71});  // add.l d0,d1
    Cpu cpu(bus);
    Registers r = stateAt(bus, kCode, 0x2700);
    r.d[0] = 0x80000000;
    r.d[1] = 0x80000000;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 8);
    CHECK_EQ(cpu.regs().d[1], 0u);
    CHECK_EQ(cpu.regs().sr & 0x1F, 0x17);  // X Z V C
}

TEST(m68k_chk_traps_with_next_pc) {
    // chk.w d1,d0 with d0 = 5 > d1 = 3: vector 6, next instruction stacked, N = 0, 38 cycles
    // (suite: Dn above the bound traps 2 clocks earlier than a negative Dn, 40).
    RamBus bus;
    bus.load32(6 * 4, 0x700);
    bus.load16(kCode, {0x4181, 0x4181, 0x4E71});
    bus.load16(0x700, {0x4E71, 0x4E71});
    Cpu cpu(bus);
    Registers r = stateAt(bus, kCode, 0x2708);
    r.d[0] = 5;
    r.d[1] = 3;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 38);
    CHECK_EQ(cpu.regs().pc, 0x700u);
    CHECK_EQ(bus.peek32(kSsp - 4), kCode + 2);
    CHECK_EQ(bus.peek16(kSsp - 6) & 0x1F, 0);
    // In range: 10 cycles, no trap.
    Registers q = stateAt(bus, kCode + 2, 0x2700);
    q.d[0] = 3;
    q.d[1] = 3;
    cpu.setRegs(q);
    CHECK_EQ(cpu.step(), 10);
    CHECK_EQ(cpu.regs().pc, kCode + 4);
    // Negative: 40 cycles, N = 1.
    Registers n = stateAt(bus, kCode + 2, 0x2700);
    n.d[0] = 0xFFFF;
    n.d[1] = 3;
    cpu.setRegs(n);
    CHECK_EQ(cpu.step(), 40);
    CHECK_EQ(cpu.regs().sr & 0x1F, 0x08);
}

TEST(m68k_addx_long_predecrement_order) {
    // addx.l -(a1),-(a2): one 2-clock predecrement, both operands read low word first, the
    // result's low word written before the closing fetch, then its high word (suite ADDX.l).
    LogBus bus;
    bus.load16(kCode, {0xD589, 0x4E71, 0x4E71});
    bus.load32(0x2000, 0x00000001);
    bus.load32(0x3000, 0xFFFFFFFF);
    Cpu cpu(bus);
    Registers r = stateAt(bus, kCode, 0x2714);  // X and Z set
    r.a[1] = 0x2004;
    r.a[2] = 0x3004;
    cpu.setRegs(r);
    bus.log.clear();
    CHECK_EQ(cpu.step(), 30);
    CHECK_EQ(cpu.regs().a[1], 0x2000u);
    CHECK_EQ(cpu.regs().a[2], 0x3000u);
    CHECK_EQ(cpu.regs().sr & 0x1F, 0x11);  // X C, Z cleared by the non-zero result
    CHECK_EQ(bus.log.size(), 7u);
    if (bus.log.size() == 7) {
        CHECK_EQ(bus.log[0].addr, 0x2002u);
        CHECK_EQ(bus.log[1].addr, 0x2000u);
        CHECK_EQ(bus.log[2].addr, 0x3002u);
        CHECK_EQ(bus.log[3].addr, 0x3000u);
        CHECK_EQ(bus.log[4].kind, 'w');
        CHECK_EQ(bus.log[4].addr, 0x3002u);
        CHECK_EQ(bus.log[5].addr, kCode + 4);
        CHECK_EQ(bus.log[6].addr, 0x3000u);
    }
    CHECK_EQ(bus.peek32(0x3000), 1u);  // 0xFFFFFFFF + 1 + X
}

// ---- Task 6: program control, traps, SR access, STOP/RESET, trace and privilege -----------

TEST(m68k_decode_flow_and_system) {
    using ens::m68k::decodeOp;
    using ens::m68k::Op;
    CHECK(decodeOp(0x6000) == Op::Bcc);       // bra.w
    CHECK(decodeOp(0x67FF) == Op::Bcc);       // beq.s with displacement -1 (68020: .l)
    CHECK(decodeOp(0x6100) == Op::Bsr);
    CHECK(decodeOp(0x51CF) == Op::Dbcc);      // dbf d7
    CHECK(decodeOp(0x51D7) == Op::Scc);       // sf (a7), not dbcc
    CHECK(decodeOp(0x4ED0) == Op::Jmp);
    CHECK(decodeOp(0x4EFB) == Op::Jmp);       // jmp (d8,pc,xn)
    CHECK(decodeOp(0x4ED8) == Op::Illegal);   // jmp (a0)+: not a control mode
    CHECK(decodeOp(0x4EBA) == Op::Jsr);
    CHECK(decodeOp(0x4EA0) == Op::Illegal);   // jsr -(a0)
    CHECK(decodeOp(0x4E75) == Op::Rts);
    CHECK(decodeOp(0x4E77) == Op::Rtr);
    CHECK(decodeOp(0x4E73) == Op::Rte);
    CHECK(decodeOp(0x4E74) == Op::Illegal);   // rtd: 68010
    CHECK(decodeOp(0x4E40) == Op::Trap);
    CHECK(decodeOp(0x4E4F) == Op::Trap);
    CHECK(decodeOp(0x4E76) == Op::Trapv);
    CHECK(decodeOp(0x4E70) == Op::Reset);
    CHECK(decodeOp(0x4E72) == Op::Stop);
    CHECK(decodeOp(0x40C0) == Op::MoveFromSr);
    CHECK(decodeOp(0x40FA) == Op::Illegal);   // move sr,(d16,pc): not alterable
    CHECK(decodeOp(0x40C8) == Op::Illegal);   // move sr,a0
    CHECK(decodeOp(0x44FC) == Op::MoveToCcr); // move #imm,ccr
    CHECK(decodeOp(0x44C8) == Op::Illegal);   // move a0,ccr
    CHECK(decodeOp(0x46FB) == Op::MoveToSr);  // move (d8,pc,xn),sr
    CHECK(decodeOp(0x007C) == Op::OriToSr);
    CHECK(decodeOp(0x023C) == Op::AndiToCcr);
    CHECK(decodeOp(0x027C) == Op::AndiToSr);
    CHECK(decodeOp(0x0A3C) == Op::EoriToCcr);
    CHECK(decodeOp(0x0A7C) == Op::EoriToSr);
    CHECK(decodeOp(0x0C3C) == Op::Illegal);   // cmpi to ccr does not exist
    CHECK(decodeOp(0x00BC) == Op::Illegal);   // ori.l #,#
}

TEST(m68k_branch_cycles) {
    RamBus bus;
    bus.load16(kCode, {0x6004});  // bra.s *+6
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode));
    CHECK_EQ(cpu.step(), 10);
    CHECK_EQ(cpu.regs().pc, kCode + 6);
    // bne.s: Z = 1 not taken (8), Z = 0 taken (10).
    RamBus bus2;
    bus2.load16(kCode, {0x6610, 0x4E71});
    Cpu cpu2(bus2);
    cpu2.setRegs(stateAt(bus2, kCode, 0x2704));
    CHECK_EQ(cpu2.step(), 8);
    CHECK_EQ(cpu2.regs().pc, kCode + 2);
    CHECK_EQ(cpu2.regs().prefetch[0], 0x4E71);
    cpu2.setRegs(stateAt(bus2, kCode, 0x2700));
    CHECK_EQ(cpu2.step(), 10);
    CHECK_EQ(cpu2.regs().pc, kCode + 2 + 0x10);
    // dbf d0,*: 10 while looping, 14 when the count expires.
    RamBus bus3;
    bus3.load16(kCode, {0x51C8, 0xFFFE, 0x4E71});
    Cpu cpu3(bus3);
    Registers r = stateAt(bus3, kCode);
    r.d[0] = 0xABCD0001;
    cpu3.setRegs(r);
    CHECK_EQ(cpu3.step(), 10);
    CHECK_EQ(cpu3.regs().pc, kCode);
    CHECK_EQ(cpu3.regs().d[0], 0xABCD0000u);
    CHECK_EQ(cpu3.step(), 14);
    CHECK_EQ(cpu3.regs().pc, kCode + 4);
    CHECK_EQ(cpu3.regs().d[0], 0xABCDFFFFu);
    CHECK_EQ(cpu3.regs().prefetch[0], 0x4E71);
}

TEST(m68k_bsr_rts_round_trip) {
    RamBus bus;
    bus.load16(kCode, {0x6100, 0x0010, 0x4E71});  // bsr.w *+0x12
    bus.load16(kCode + 0x12, {0x4E75});           // rts
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode));
    CHECK_EQ(cpu.step(), 18);
    CHECK_EQ(cpu.regs().pc, kCode + 0x12);
    CHECK_EQ(cpu.regs().ssp, kSsp - 4);
    CHECK_EQ(bus.peek32(kSsp - 4), kCode + 4);
    CHECK_EQ(cpu.step(), 16);
    CHECK_EQ(cpu.regs().pc, kCode + 4);
    CHECK_EQ(cpu.regs().ssp, kSsp);
    CHECK_EQ(cpu.regs().prefetch[0], 0x4E71);
}

TEST(m68k_trap_vector_and_rte) {
    // From user mode: TRAP #1 -> vector 33 (handler 0x500: rte), back to 0x402 in user mode.
    RamBus bus;
    bus.load32(33 * 4, 0x500);
    bus.load16(kCode, {0x4E41, 0x4E71});
    bus.load16(0x500, {0x4E73});
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x0015));
    CHECK_EQ(cpu.step(), 34);
    Registers s = cpu.regs();
    CHECK_EQ(s.pc, 0x500u);
    CHECK_EQ(s.sr, 0x2015);
    CHECK_EQ(s.ssp, kSsp - 6);
    CHECK_EQ(bus.peek16(kSsp - 6), 0x0015);
    CHECK_EQ(bus.peek32(kSsp - 4), kCode + 2);
    CHECK_EQ(cpu.exceptionCount(33), 1u);
    CHECK_EQ(cpu.step(), 20);
    s = cpu.regs();
    CHECK_EQ(s.pc, kCode + 2);
    CHECK_EQ(s.sr, 0x0015);
    CHECK_EQ(s.ssp, kSsp);
    CHECK_EQ(s.usp, 0x3000u);
    CHECK_EQ(s.prefetch[0], 0x4E71);
}

TEST(m68k_privilege_violation) {
    RamBus bus;
    bus.load32(8 * 4, 0x700);
    bus.load16(kCode, {0x46FC, 0x2700});  // move #$2700,sr in user mode
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x0000));
    CHECK_EQ(cpu.step(), 34);
    const Registers s = cpu.regs();
    CHECK_EQ(s.pc, 0x700u);
    CHECK_EQ(s.sr, 0x2000);
    CHECK_EQ(bus.peek32(kSsp - 4), kCode);  // the instruction's own address
    CHECK_EQ(cpu.exceptionCount(8), 1u);
    // The other privileged instructions of this task do the same in user mode.
    for (uint16_t op : {uint16_t(0x027C), uint16_t(0x007C), uint16_t(0x0A7C), uint16_t(0x4E70),
                        uint16_t(0x4E72), uint16_t(0x4E73), uint16_t(0x46C0)}) {
        RamBus b;
        b.load32(8 * 4, 0x700);
        b.load16(kCode, {op, 0x0000});
        Cpu c(b);
        c.setRegs(stateAt(b, kCode, 0x0000));
        CHECK_EQ(c.step(), 34);
        CHECK_EQ(c.regs().pc, 0x700u);
        CHECK_EQ(b.peek32(kSsp - 4), kCode);
        CHECK_EQ(c.exceptionCount(8), 1u);
        CHECK_EQ(b.resets, 0);
        CHECK(!c.stopped());
    }
}

TEST(m68k_reset_instruction_calls_bus) {
    RamBus bus;
    bus.load16(kCode, {0x4E70, 0x4E71, 0x4E71});
    Cpu cpu(bus);
    Registers r = stateAt(bus, kCode, 0x271F);
    for (int i = 0; i < 8; ++i) r.d[i] = 0x11111111u * static_cast<uint32_t>(i);
    for (int i = 0; i < 7; ++i) r.a[i] = 0x01010101u * static_cast<uint32_t>(i + 1);
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 132);
    CHECK_EQ(bus.resets, 1);
    const Registers s = cpu.regs();
    for (int i = 0; i < 8; ++i) CHECK_EQ(s.d[i], r.d[i]);
    for (int i = 0; i < 7; ++i) CHECK_EQ(s.a[i], r.a[i]);
    CHECK_EQ(s.usp, r.usp);
    CHECK_EQ(s.ssp, r.ssp);
    CHECK_EQ(s.sr, r.sr);
    CHECK_EQ(s.pc, kCode + 2);
    CHECK(!cpu.halted());
}

TEST(m68k_trace_after_each_instruction) {
    // T = 1: nop, trace (handler: rte back with T = 1), nop, trace.
    RamBus bus;
    bus.load32(9 * 4, 0x600);
    bus.load16(kCode, {0x4E71, 0x4E71, 0x4E71});
    bus.load16(0x600, {0x4E73});
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0xA700));
    CHECK_EQ(cpu.step(), 4);  // nop
    CHECK_EQ(cpu.exceptionCount(9), 0u);
    CHECK_EQ(cpu.step(), 34);  // trace
    CHECK_EQ(cpu.exceptionCount(9), 1u);
    CHECK_EQ(cpu.regs().pc, 0x600u);
    CHECK_EQ(cpu.regs().sr, 0x2700);  // T cleared in the handler
    CHECK_EQ(bus.peek16(kSsp - 6), 0xA700);
    CHECK_EQ(bus.peek32(kSsp - 4), kCode + 2);  // the next instruction
    CHECK_EQ(cpu.step(), 20);  // rte (T = 0 at its start: not traced)
    CHECK_EQ(cpu.regs().sr, 0xA700);
    CHECK_EQ(cpu.regs().pc, kCode + 2);
    CHECK_EQ(cpu.step(), 4);   // second nop
    CHECK_EQ(cpu.step(), 34);  // second trace
    CHECK_EQ(cpu.exceptionCount(9), 2u);
    CHECK_EQ(bus.peek32(kSsp - 4), kCode + 4);
}

TEST(m68k_trace_after_trap_and_not_after_illegal) {
    // UM 6.3.8: TRAP with T = 1 takes the trap, then the trace (whose frame holds the trap
    // handler's address and the SR with T = 0); an illegal instruction is not traced.
    RamBus bus;
    bus.load32(9 * 4, 0x600);
    bus.load32(32 * 4, 0x500);
    bus.load32(4 * 4, 0x700);
    bus.load16(kCode, {0x4E40, 0x4AFC});
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0xA700));
    CHECK_EQ(cpu.step(), 34);  // trap #0
    CHECK_EQ(cpu.regs().pc, 0x500u);
    CHECK_EQ(cpu.step(), 34);  // trace
    CHECK_EQ(cpu.regs().pc, 0x600u);
    CHECK_EQ(cpu.exceptionCount(9), 1u);
    CHECK_EQ(bus.peek16(kSsp - 12), 0x2700);
    CHECK_EQ(bus.peek32(kSsp - 10), 0x500u);
    // An illegal instruction with T = 1: vector 4 and no trace after it.
    bus.load16(0x700, {0x4E71, 0x4E71});
    cpu.setRegs(stateAt(bus, kCode + 2, 0xA700));
    CHECK_EQ(cpu.step(), 34);
    CHECK_EQ(cpu.regs().pc, 0x700u);
    CHECK_EQ(cpu.exceptionCount(4), 1u);
    CHECK_EQ(cpu.step(), 4);  // the handler's nop, not a trace
    CHECK_EQ(cpu.exceptionCount(9), 1u);
}

TEST(m68k_stop_loads_sr_and_stops) {
    RamBus bus;
    bus.load16(kCode, {0x4E72, 0x2015, 0x4E71});  // stop #$2015
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode));
    CHECK_EQ(cpu.step(), 4);
    CHECK(cpu.stopped());
    CHECK_EQ(cpu.regs().sr, 0x2015);
    CHECK_EQ(cpu.regs().pc, kCode + 4);
    CHECK_EQ(cpu.run(1000), 1000);  // M6: a stopped CPU consumes the slice
    CHECK_EQ(cpu.step(), 4);
    CHECK(cpu.stopped());
    // With T = 1 at its start the trace exception follows and resumes execution (PRM STOP).
    RamBus bus2;
    bus2.load32(9 * 4, 0x600);
    bus2.load16(kCode, {0x4E72, 0x2700});
    Cpu cpu2(bus2);
    cpu2.setRegs(stateAt(bus2, kCode, 0xA700));
    CHECK_EQ(cpu2.step(), 4);
    CHECK_EQ(cpu2.step(), 34);
    CHECK(!cpu2.stopped());
    CHECK_EQ(cpu2.regs().pc, 0x600u);
    CHECK_EQ(bus2.peek32(kSsp - 4), kCode + 4);
}

TEST(m68k_sr_immediate_ops_switch_stack) {
    // andi #$dfff,sr: to user mode, A7 becomes the USP; 20 cycles, the queue refetched.
    RamBus bus;
    bus.load16(kCode, {0x027C, 0xDFFF, 0x4E71, 0x4E71});
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x271F));
    CHECK_EQ(cpu.step(), 20);
    const Registers s = cpu.regs();
    CHECK_EQ(s.sr, 0x071F);
    CHECK_EQ(s.pc, kCode + 4);
    CHECK_EQ(s.prefetch[0], 0x4E71);
    CHECK_EQ(s.usp, 0x3000u);
    CHECK_EQ(s.ssp, kSsp);
    // move #$1f,ccr; eori #$11,ccr; ori #$02,ccr; move sr,d0 (user mode: none is privileged).
    RamBus bus2;
    bus2.load16(kCode, {0x44FC, 0x001F, 0x0A3C, 0x0011, 0x003C, 0x0002, 0x40C0});
    Cpu cpu2(bus2);
    cpu2.setRegs(stateAt(bus2, kCode, 0x0000));
    CHECK_EQ(cpu2.step(), 16);
    CHECK_EQ(cpu2.regs().sr, 0x001F);
    CHECK_EQ(cpu2.step(), 20);
    CHECK_EQ(cpu2.regs().sr, 0x000E);
    CHECK_EQ(cpu2.step(), 20);
    CHECK_EQ(cpu2.regs().sr, 0x000E);
    CHECK_EQ(cpu2.step(), 6);  // move sr,d0 is not privileged on the 68000
    CHECK_EQ(cpu2.regs().d[0] & 0xFFFF, 0x000Eu);
    CHECK_EQ(cpu2.exceptionCount(8), 0u);
}

TEST(m68k_jsr_jmp) {
    RamBus bus;
    bus.load16(kCode, {0x4EB9, 0x0000, 0x0800});  // jsr ($800).l
    bus.load16(0x800, {0x4EE8, 0x0010});          // jmp ($10,a0)
    Cpu cpu(bus);
    Registers r = stateAt(bus, kCode);
    r.a[0] = 0x900;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 20);
    CHECK_EQ(cpu.regs().pc, 0x800u);
    CHECK_EQ(bus.peek32(kSsp - 4), kCode + 6);
    CHECK_EQ(cpu.step(), 10);
    CHECK_EQ(cpu.regs().pc, 0x910u);
}

TEST(m68k_runaway_into_fill_halts_cleanly) {
    // 0xFFFF fill: line F with vector 11 = 0xFFFFFFFF (odd) -> address error, whose vector is
    // odd too -> double fault -> halt (M9). run() still returns the whole slice.
    RamBus bus;
    for (uint32_t a = 8; a < (1u << 24); ++a) bus.mem[a] = 0xFF;
    vectors(bus);
    Cpu cpu(bus);
    cpu.reset();
    CHECK_EQ(cpu.regs().pc, kCode);
    CHECK_EQ(cpu.run(10000), 10000);
    CHECK(cpu.halted());
    CHECK(cpu.exceptionCount(11) >= 1u);
}
