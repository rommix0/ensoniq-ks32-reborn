// 68000 interrupts, STOP wake-up and slice invariance of run() (Plan 5 Task 7; DECISIONS M4, M6, M7, M24).
#include <cstdint>

#include "check.h"
#include "m68k/m68k.h"
#include "m68k_test_bus.h"

using ens::m68k::Cpu;
using ens::m68k::Registers;

namespace {

constexpr uint32_t kSsp = 0x1000;
constexpr uint32_t kCode = 0x400;
constexpr uint32_t kHandler = 0x600;
constexpr uint16_t kNop = 0x4E71;
constexpr uint16_t kRte = 0x4E73;

// Supervisor state at `pc` with the prefetch queue read from memory (M3), SSP = kSsp.
Registers stateAt(RamBus& bus, uint32_t pc, uint16_t sr) {
    Registers r{};
    r.ssp = kSsp;
    r.usp = 0x3000;
    r.pc = pc;
    r.sr = sr;
    r.prefetch[0] = bus.peek16(pc);
    r.prefetch[1] = bus.peek16(pc + 2);
    return r;
}

// NOPs from kCode and from kHandler, so every program counter in the tests holds a NOP.
void fillNops(RamBus& bus) {
    for (uint32_t a = kCode; a < kCode + 0x40; a += 2) bus.write16(a, kNop);
    for (uint32_t a = kHandler; a < kHandler + 0x40; a += 2) bus.write16(a, kNop);
}

}  // namespace

TEST(m68k_autovector_interrupt) {
    RamBus bus;
    fillNops(bus);
    bus.load32(27 * 4, kHandler);  // level 3 autovector: 24 + 3
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x2000));
    CHECK_EQ(cpu.step(), 4);  // one NOP first: the interrupt stacks the next one
    cpu.setIpl(3);
    CHECK_EQ(cpu.step(), 44);  // M4
    const Registers r = cpu.regs();
    CHECK_EQ(r.pc, kHandler);
    CHECK_EQ(r.prefetch[0], kNop);
    CHECK_EQ(r.sr, 0x2300);
    CHECK_EQ(r.ssp, kSsp - 6);
    CHECK_EQ(bus.peek16(kSsp - 6), 0x2000);     // stacked SR
    CHECK_EQ(bus.peek32(kSsp - 4), kCode + 2);  // stacked PC: the next NOP
    CHECK_EQ(cpu.exceptionCount(27), 1u);
    CHECK(!cpu.stopped());
    CHECK_EQ(cpu.totalCycles(), 48u);
}

TEST(m68k_interrupt_from_user_mode_and_trace) {
    // S = 1, T = 0 and the mask are set after the SR is copied; the frame goes on the SSP.
    RamBus bus;
    fillNops(bus);
    bus.load32(26 * 4, kHandler);  // level 2 autovector
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x0000));  // user mode, mask 0
    cpu.setIpl(2);
    CHECK_EQ(cpu.step(), 44);
    const Registers r = cpu.regs();
    CHECK_EQ(r.sr, 0x2200);
    CHECK_EQ(r.usp, 0x3000u);
    CHECK_EQ(r.ssp, kSsp - 6);
    CHECK_EQ(bus.peek16(kSsp - 6), 0x0000);
    CHECK_EQ(bus.peek32(kSsp - 4), kCode);
    // T set: the trace exception of the previous instruction comes first (review note 4).
    RamBus bus2;
    fillNops(bus2);
    bus2.load32(9 * 4, kHandler + 0x20);
    bus2.load32(27 * 4, kHandler);
    Cpu cpu2(bus2);
    cpu2.setRegs(stateAt(bus2, kCode, 0xA000));
    CHECK_EQ(cpu2.step(), 4);  // NOP with T = 1: trace pending
    cpu2.setIpl(3);
    CHECK_EQ(cpu2.step(), 34);  // trace first
    CHECK_EQ(cpu2.exceptionCount(9), 1u);
    CHECK_EQ(cpu2.regs().pc, kHandler + 0x20);
    CHECK_EQ(cpu2.step(), 44);  // then the interrupt, before the trace handler's first instruction
    CHECK_EQ(cpu2.regs().pc, kHandler);
    CHECK_EQ(cpu2.regs().sr, 0x2300);  // T was cleared by the trace exception
    CHECK_EQ(bus2.peek16(kSsp - 12), 0x2000);
    CHECK_EQ(bus2.peek32(kSsp - 10), kHandler + 0x20);
}

TEST(m68k_vectored_and_spurious) {
    RamBus bus;
    fillNops(bus);
    bus.load32(64 * 4, kHandler);
    bus.ackVector = 64;
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x2000));
    cpu.setIpl(1);
    CHECK_EQ(cpu.step(), 44);
    CHECK_EQ(cpu.regs().pc, kHandler);
    CHECK_EQ(cpu.exceptionCount(64), 1u);
    CHECK_EQ(cpu.regs().sr, 0x2100);
    // kSpurious: vector 24.
    RamBus bus2;
    fillNops(bus2);
    bus2.load32(24 * 4, kHandler + 0x20);
    bus2.ackVector = ens::m68k::kSpurious;
    Cpu cpu2(bus2);
    cpu2.setRegs(stateAt(bus2, kCode, 0x2000));
    cpu2.setIpl(5);
    CHECK_EQ(cpu2.step(), 44);
    CHECK_EQ(cpu2.regs().pc, kHandler + 0x20);
    CHECK_EQ(cpu2.exceptionCount(24), 1u);
    CHECK_EQ(cpu2.regs().sr, 0x2500);
    // A device vector in 32..47 (the range of TRAP) takes the interrupt entry all the same; an
    // answer outside 0..255 counts as spurious (M24).
    RamBus bus3;
    fillNops(bus3);
    bus3.load32(40 * 4, kHandler);
    bus3.load32(24 * 4, kHandler + 0x20);
    bus3.ackVector = 40;
    Cpu cpu3(bus3);
    cpu3.setRegs(stateAt(bus3, kCode, 0x2000));
    cpu3.setIpl(6);
    CHECK_EQ(cpu3.step(), 44);
    CHECK_EQ(cpu3.regs().pc, kHandler);
    CHECK_EQ(cpu3.exceptionCount(40), 1u);
    cpu3.setRegs(stateAt(bus3, kCode, 0x2000));
    bus3.ackVector = 300;
    CHECK_EQ(cpu3.step(), 44);
    CHECK_EQ(cpu3.regs().pc, kHandler + 0x20);
    CHECK_EQ(cpu3.exceptionCount(24), 1u);
}

TEST(m68k_masked_level_waits) {
    RamBus bus;
    fillNops(bus);
    bus.load32(28 * 4, kHandler);
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x2300));  // mask 3
    cpu.setIpl(3);
    CHECK_EQ(cpu.step(), 4);  // equal to the mask: not taken
    CHECK_EQ(cpu.step(), 4);
    CHECK_EQ(cpu.regs().pc, kCode + 4);
    cpu.setIpl(4);
    CHECK_EQ(cpu.step(), 44);
    CHECK_EQ(cpu.regs().pc, kHandler);
    CHECK_EQ(cpu.regs().sr, 0x2400);
    CHECK_EQ(cpu.exceptionCount(28), 1u);
    // The level is still 4 and the mask now 4, so nothing more is taken.
    CHECK_EQ(cpu.step(), 4);
}

TEST(m68k_level7_is_edge_triggered) {
    RamBus bus;
    fillNops(bus);
    bus.load32(31 * 4, kHandler);
    bus.write16(kHandler, kRte);
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x2700));  // mask 7
    cpu.setIpl(7);
    CHECK_EQ(cpu.step(), 44);  // taken although the mask is 7
    CHECK_EQ(cpu.regs().pc, kHandler);
    CHECK_EQ(cpu.step(), 20);  // RTE
    CHECK_EQ(cpu.regs().pc, kCode);
    CHECK_EQ(cpu.regs().sr, 0x2700);
    for (int i = 0; i < 5; ++i) CHECK_EQ(cpu.step(), 4);  // the level stays 7: nothing more
    CHECK_EQ(cpu.exceptionCount(31), 1u);
    cpu.setIpl(0);
    CHECK_EQ(cpu.step(), 4);
    cpu.setIpl(7);
    CHECK_EQ(cpu.step(), 44);  // a new rise: a second interrupt
    CHECK_EQ(cpu.exceptionCount(31), 2u);
    // A rise and fall between two instruction boundaries is never seen (M7: sampled at boundaries).
    RamBus bus2;
    fillNops(bus2);
    Cpu cpu2(bus2);
    cpu2.setRegs(stateAt(bus2, kCode, 0x2700));
    cpu2.setIpl(7);
    cpu2.setIpl(0);
    CHECK_EQ(cpu2.step(), 4);
    CHECK_EQ(cpu2.exceptionCount(31), 0u);
    // A fresh rise at a mask below 7 is delivered too (here by the edge latch; the level
    // comparison is pinned by m68k_level7_held_retaken_when_mask_drops).
    RamBus bus3;
    fillNops(bus3);
    bus3.load32(31 * 4, kHandler);
    Cpu cpu3(bus3);
    cpu3.setRegs(stateAt(bus3, kCode, 0x2000));
    cpu3.setIpl(7);
    CHECK_EQ(cpu3.step(), 44);
    CHECK_EQ(cpu3.regs().sr, 0x2700);
}

TEST(m68k_level7_held_retaken_when_mask_drops) {
    // R13 / UM 6.3.2: a request that stays at 7 is taken again by the level comparison once an
    // instruction lowers the mask below 7. Under an edge-only reading none of these retakes.
    // (a) The handler lowers the mask itself: move #$2000,sr.
    RamBus bus;
    fillNops(bus);
    bus.load32(31 * 4, kHandler);
    bus.load16(kHandler, {0x46FC, 0x2000});
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x2700));
    cpu.setIpl(7);
    CHECK_EQ(cpu.step(), 44);  // the rise, taken at mask 7
    CHECK_EQ(cpu.exceptionCount(31), 1u);
    CHECK_EQ(cpu.step(), 16);  // move #$2000,sr: the mask drops to 0, the line is still 7
    CHECK_EQ(cpu.regs().sr, 0x2000);
    CHECK_EQ(cpu.step(), 44);  // retaken by the level comparison, no new rise
    CHECK_EQ(cpu.exceptionCount(31), 2u);
    CHECK_EQ(bus.peek16(kSsp - 12), 0x2000);  // the second frame stacks the lowered SR
    // (b) The handler rewrites the stacked SR to mask 0 (move.w #$2000,(a7)) and returns.
    RamBus bus2;
    fillNops(bus2);
    bus2.load32(31 * 4, kHandler);
    bus2.load16(kHandler, {0x3EBC, 0x2000, kRte});
    Cpu cpu2(bus2);
    cpu2.setRegs(stateAt(bus2, kCode, 0x2700));
    cpu2.setIpl(7);
    CHECK_EQ(cpu2.step(), 44);
    CHECK_EQ(cpu2.step(), 12);  // move.w #imm,(a7)
    CHECK_EQ(cpu2.step(), 20);  // rte to mask 0
    CHECK_EQ(cpu2.regs().sr, 0x2000);
    CHECK_EQ(cpu2.regs().pc, kCode);
    CHECK_EQ(cpu2.step(), 44);  // the line is still 7: taken again
    CHECK_EQ(cpu2.exceptionCount(31), 2u);
    CHECK_EQ(bus2.peek32(kSsp - 4), kCode);  // no instruction ran in between
    // (c) A held 7 at mask 7 (the handler returns to mask 7) is not retaken.
    RamBus bus3;
    fillNops(bus3);
    bus3.load32(31 * 4, kHandler);
    bus3.write16(kHandler, kRte);
    Cpu cpu3(bus3);
    cpu3.setRegs(stateAt(bus3, kCode, 0x2700));
    cpu3.setIpl(7);
    CHECK_EQ(cpu3.step(), 44);
    CHECK_EQ(cpu3.step(), 20);
    for (int i = 0; i < 4; ++i) CHECK_EQ(cpu3.step(), 4);
    CHECK_EQ(cpu3.exceptionCount(31), 1u);
}

TEST(m68k_stop_wakes_on_unmasked_interrupt_only) {
    RamBus bus;
    fillNops(bus);
    bus.load16(kCode, {0x4E72, 0x2300});  // stop #$2300
    bus.load32(29 * 4, kHandler);
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x2700));
    CHECK_EQ(cpu.run(1000), 1000);
    CHECK(cpu.stopped());
    CHECK_EQ(cpu.regs().sr, 0x2300);
    cpu.setIpl(2);
    CHECK_EQ(cpu.run(1000), 1000);
    CHECK(cpu.stopped());
    CHECK_EQ(cpu.step(), 4);  // step() on a stopped CPU with nothing to take idles
    CHECK(cpu.stopped());
    cpu.setIpl(5);
    CHECK_EQ(cpu.run(100), 100);  // 44 for the interrupt, then 14 NOPs of the handler
    CHECK(!cpu.stopped());
    CHECK_EQ(cpu.exceptionCount(29), 1u);
    CHECK_EQ(bus.peek32(kSsp - 4), kCode + 4);  // stacked PC: after the STOP
    CHECK_EQ(bus.peek16(kSsp - 6), 0x2300);
    CHECK_EQ(cpu.regs().sr, 0x2500);
    // step() alone wakes it, too.
    RamBus bus2;
    fillNops(bus2);
    bus2.load16(kCode, {0x4E72, 0x2000});
    bus2.load32(25 * 4, kHandler);
    Cpu cpu2(bus2);
    cpu2.setRegs(stateAt(bus2, kCode, 0x2700));
    CHECK_EQ(cpu2.step(), 4);
    CHECK(cpu2.stopped());
    cpu2.setIpl(1);
    CHECK_EQ(cpu2.step(), 44);
    CHECK(!cpu2.stopped());
}

TEST(m68k_halted_cpu_ignores_interrupts) {
    RamBus bus;
    bus.load32(0, 0x1000);
    bus.load32(4, 0x401);  // an odd PC halts the reset (M9)
    Cpu cpu(bus);
    cpu.reset();
    CHECK(cpu.halted());
    cpu.setIpl(7);
    CHECK_EQ(cpu.run(100), 100);
    CHECK(cpu.halted());
    CHECK_EQ(cpu.exceptionCount(31), 0u);
}

TEST(m68k_run_is_overflow_safe) {
    RamBus bus;
    fillNops(bus);
    bus.load16(kCode, {0x4E72, 0x2700});  // stop #$2700
    Cpu cpu(bus);
    cpu.setRegs(stateAt(bus, kCode, 0x2700));
    CHECK_EQ(cpu.run(2147483647), 2147483647);
    CHECK_EQ(cpu.totalCycles(), 2147483647u);
    CHECK_EQ(cpu.run(0), 0);
    CHECK_EQ(cpu.run(-5), 0);
    CHECK_EQ(cpu.totalCycles(), 2147483647u);
}

namespace {

// 1000 iterations of a memory write, ADDQ, ADD, MULU and a TRAP #1 whose handler counts in D5,
// writes memory and returns; then a branch to self.
void loadSliceProgram(RamBus& bus) {
    bus.load32(33 * 4, kHandler);
    bus.load16(kHandler, {0x5285, 0x31C6, 0x3000, 0x4E73});  // addq.l #1,d5; move.w d6,$3000.w; rte
    bus.load16(kCode, {0x41F9, 0x0000, 0x2000,  // lea $2000,a0
                       0x3E3C, 0x03E7,          // move.w #999,d7
                       0x3C3C, 0x0007,          // move.w #7,d6
                       0x3087,                  // loop: move.w d7,(a0)
                       0x5448,                  // addq.w #2,a0
                       0xDC47,                  // add.w d7,d6
                       0xCCC7,                  // mulu.w d7,d6
                       0x4E41,                  // trap #1
                       0x51CF, 0xFFF4,          // dbf d7,loop
                       0x60FE});                // bra.s *
}

void sameState(Cpu& a, Cpu& b) {
    CHECK_EQ(a.totalCycles(), b.totalCycles());
    const Registers ra = a.regs(), rb = b.regs();
    for (int i = 0; i < 8; ++i) CHECK_EQ(ra.d[i], rb.d[i]);
    for (int i = 0; i < 7; ++i) CHECK_EQ(ra.a[i], rb.a[i]);
    CHECK_EQ(ra.usp, rb.usp);
    CHECK_EQ(ra.ssp, rb.ssp);
    CHECK_EQ(ra.pc, rb.pc);
    CHECK_EQ(ra.sr, rb.sr);
    CHECK_EQ(ra.prefetch[0], rb.prefetch[0]);
    CHECK_EQ(ra.prefetch[1], rb.prefetch[1]);
}

// run(n) as a loop of run(1): whole instructions until n cycles have passed since the start.
void runByOnes(Cpu& cpu, uint64_t n) {
    const uint64_t start = cpu.totalCycles();
    while (cpu.totalCycles() - start < n) cpu.run(1);
}

}  // namespace

TEST(m68k_slicing_is_invisible) {
    constexpr int kTarget = 200000;
    RamBus busA, busB;
    loadSliceProgram(busA);
    loadSliceProgram(busB);
    Cpu a(busA), b(busB);
    a.setRegs(stateAt(busA, kCode, 0x2000));
    b.setRegs(stateAt(busB, kCode, 0x2000));
    runByOnes(a, kTarget);
    CHECK(b.run(kTarget) >= kTarget);
    sameState(a, b);
    CHECK(a.totalCycles() >= static_cast<uint64_t>(kTarget));
    CHECK(a.totalCycles() < static_cast<uint64_t>(kTarget) + 20);  // overshoot less than one instruction
    int differing = 0;
    for (uint32_t i = 0; i < 0x4000; ++i) differing += busA.mem[i] != busB.mem[i];
    CHECK_EQ(differing, 0);
    // The program did its work: all 1000 iterations ran and it sits at its final branch.
    const Registers rb = b.regs();
    CHECK_EQ(rb.d[5], 1000u);
    CHECK_EQ(rb.d[7] & 0xFFFF, 0xFFFFu);
    CHECK_EQ(b.exceptionCount(33), 1000u);
    CHECK_EQ(busB.peek16(0x2000), 999);
}

TEST(m68k_slicing_is_invisible_with_interrupts) {
    // The interrupt level changes at slice boundaries only; slicing the time inside a slice
    // must not change what the interrupts do.
    auto build = [](RamBus& bus) {
        loadSliceProgram(bus);
        bus.load32(27 * 4, 0x700);
        bus.load16(0x700, {0x5285, 0x4E73});  // addq.l #1,d5; rte
    };
    RamBus busA, busB;
    build(busA);
    build(busB);
    Cpu a(busA), b(busB);
    a.setRegs(stateAt(busA, kCode, 0x2000));
    b.setRegs(stateAt(busB, kCode, 0x2000));
    for (int slice = 0; slice < 300; ++slice) {
        const int level = (slice % 3 == 0) ? 3 : 0;
        a.setIpl(level);
        b.setIpl(level);
        runByOnes(a, 300);
        b.run(300);
        sameState(a, b);
    }
    CHECK(a.exceptionCount(27) > 0u);
    CHECK_EQ(a.exceptionCount(27), b.exceptionCount(27));
}
