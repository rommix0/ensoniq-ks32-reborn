#include <cstdint>

#include "check.h"
#include "m68k/m68k.h"
#include "m68k/m68k_alu.h"
#include "m68k_test_bus.h"

namespace m68k = ens::m68k;

namespace {

constexpr uint16_t kX = 0x10, kN = 0x08, kZ = 0x04, kV = 0x02, kC = 0x01;

// A CPU at 0x400 in supervisor mode with the given words there and the given data registers.
m68k::Registers stateAt(RamBus& bus, uint32_t pc) {
    m68k::Registers r{};
    r.ssp = 0x1000;
    r.usp = 0x3000;
    r.pc = pc;
    r.sr = 0x2700;
    r.prefetch[0] = bus.peek16(pc);
    r.prefetch[1] = bus.peek16(pc + 2);
    return r;
}

}  // namespace

TEST(m68k_alu_add_flags) {
    const m68k::AluOut a = m68k::add(0x7F, 1, 1, false, false);
    CHECK_EQ(a.value, 0x80u);
    CHECK_EQ(a.ccr, kN | kV);
    const m68k::AluOut b = m68k::add(0xFFFFFFFF, 1, 4, false, false);
    CHECK_EQ(b.value, 0u);
    CHECK_EQ(b.ccr, kX | kZ | kC);
    // ADDX adds X; the result's Z means "zero", the caller keeps Z only if it was set.
    const m68k::AluOut c = m68k::add(0xFFFF, 0, 2, true, true);
    CHECK_EQ(c.value, 0u);
    CHECK_EQ(c.ccr, kX | kZ | kC);
    // X is not added by plain ADD.
    CHECK_EQ(m68k::add(1, 1, 1, true, false).value, 2u);
    // a - b: borrow sets C and X; signed overflow sets V.
    const m68k::AluOut d = m68k::sub(0, 1, 1, false, false);
    CHECK_EQ(d.value, 0xFFu);
    CHECK_EQ(d.ccr, kX | kN | kC);
    const m68k::AluOut e = m68k::sub(0x80000000, 1, 4, false, false);
    CHECK_EQ(e.value, 0x7FFFFFFFu);
    CHECK_EQ(e.ccr, kV);
    CHECK_EQ(m68k::sub(5, 2, 2, true, true).value, 2u);  // SUBX subtracts X
}

TEST(m68k_alu_bcd) {
    const m68k::AluOut a = m68k::abcd(0x45, 0x38, false, 0);
    CHECK_EQ(a.value, 0x83u);
    CHECK_EQ(a.ccr & kC, 0);
    CHECK_EQ(m68k::abcd(0x45, 0x38, true, 0).value, 0x84u);
    const m68k::AluOut s = m68k::sbcd(0x10, 0x01, false, 0);
    CHECK_EQ(s.value, 0x09u);
    CHECK_EQ(s.ccr & kC, 0);
    // NBCD is 0 - operand - X.
    const m68k::AluOut n = m68k::sbcd(0x00, 0x01, false, kZ);
    CHECK_EQ(n.value, 0x99u);
    CHECK_EQ(n.ccr & (kX | kC | kZ), kX | kC);  // non-zero result clears Z
    // Z is only cleared: a zero result keeps it as it was.
    CHECK_EQ(m68k::abcd(0x99, 0x01, false, kZ).ccr & (kZ | kC | kX), kZ | kC | kX);
    CHECK_EQ(m68k::abcd(0x99, 0x01, false, 0).ccr & kZ, 0);
}

TEST(m68k_alu_mul) {
    CHECK_EQ(m68k::mulCycles(0xFFFF, false), 70);  // 38 + 2 * 16 ones
    CHECK_EQ(m68k::mulCycles(0x0000, false), 38);
    CHECK_EQ(m68k::mulCycles(0x5555, true), 70);   // 16 "01"/"10" pairs in 0x5555:0
    CHECK_EQ(m68k::mulCycles(0x8000, true), 40);   // 1000...0:0 has one "10"/"01" boundary
    // mulu.w d1,d0 and muls.w d1,d0 on the CPU.
    RamBus bus;
    bus.load16(0x400, {0xC0C1, 0xC1C1, 0x4E71, 0x4E71});
    m68k::Cpu cpu(bus);
    m68k::Registers r = stateAt(bus, 0x400);
    r.d[0] = 0x1234FFFF;
    r.d[1] = 0x0000FFFF;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 70);
    CHECK_EQ(cpu.regs().d[0], 0xFFFE0001u);
    CHECK_EQ(cpu.regs().sr & 0x1F, kN);
    r = cpu.regs();
    r.d[0] = 0x8000;
    r.d[1] = 0x8000;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 40);
    CHECK_EQ(cpu.regs().d[0], 0x40000000u);
    CHECK_EQ(cpu.regs().sr & 0x1F, 0);
}

TEST(m68k_alu_div_cycles) {
    // DECISION M10 (see docs/decisions.md): totals without the operand's address calculation.
    CHECK_EQ(m68k::divCycles(0x00010000, 1, false), 10);   // DIVU overflow is found at once
    CHECK_EQ(m68k::divCycles(0, 1, false), 136);           // DIVU: 15 quotient bits without carry
    CHECK_EQ(m68k::divCycles(0xFFFE0001, 0xFFFF, false), 76);  // DIVU: every step carries
    CHECK_EQ(m68k::divCycles(0, 1, true), 150);            // DIVS: 2 * (6 + 55 - 1 + 15 zero bits)
    CHECK_EQ(m68k::divCycles(0x80000000, 0xFFFF, true), 18);  // DIVS overflow, negative dividend
    CHECK_EQ(m68k::divCycles(0x7FFFFFFF, 0x0001, true), 16);  // DIVS overflow, positive dividend
    CHECK_EQ(m68k::divCycles(0xFFFF8000, 0x0001, true), 154);  // -32768 fits: no overflow
    CHECK_EQ(m68k::divCycles(1234, 0, false), 38);            // divide by zero (exception included)
}

TEST(m68k_decode_arith_modes) {
    using m68k::Op;
    CHECK(m68k::decodeOp(0xD280) == Op::AddL);       // add.l d0,d1
    CHECK(m68k::decodeOp(0xD008) == Op::Illegal);    // add.b a0,d0: no byte An source
    CHECK(m68k::decodeOp(0xD048) == Op::AddW);       // add.w a0,d0
    CHECK(m68k::decodeOp(0xD150) == Op::AddToMemW);  // add.w d0,(a0)
    CHECK(m68k::decodeOp(0xD17A) == Op::Illegal);    // add.w d0,(d16,pc): not alterable
    CHECK(m68k::decodeOp(0xD189) == Op::AddxL);      // addx.l -(a1),-(a0)
    CHECK(m68k::decodeOp(0x93C8) == Op::SubaL);      // suba.l a0,a1
    CHECK(m68k::decodeOp(0x5008) == Op::Illegal);    // addq.b #8,a0
    CHECK(m68k::decodeOp(0x5048) == Op::AddqW);      // addq.w #8,a0
    CHECK(m68k::decodeOp(0x5148) == Op::SubqW);      // subq.w #8,a0
    CHECK(m68k::decodeOp(0x50C0) == Op::Scc);        // st d0
    CHECK(m68k::decodeOp(0x50C8) == Op::Illegal);    // dbt d0 (Task 6)
    CHECK(m68k::decodeOp(0x0C3A) == Op::Illegal);    // cmpi.b #,(d16,pc): not on the 68000
    CHECK(m68k::decodeOp(0x0680) == Op::AddiL);      // addi.l #,d0
    CHECK(m68k::decodeOp(0x0000) == Op::OriB);       // ori.b #,d0
    CHECK(m68k::decodeOp(0x003C) == Op::Illegal);    // ori to ccr (Task 6)
    CHECK(m68k::decodeOp(0xB308) == Op::CmpmB);      // cmpm.b (a0)+,(a1)+
    CHECK(m68k::decodeOp(0xB300) == Op::EorB);       // eor.b d1,d0
    CHECK(m68k::decodeOp(0xB1C8) == Op::CmpaL);      // cmpa.l a0,a0
    CHECK(m68k::decodeOp(0x4A08) == Op::Illegal);    // tst.b a0: not on the 68000
    CHECK(m68k::decodeOp(0x4A3C) == Op::Illegal);    // tst.b #imm: not on the 68000
    CHECK(m68k::decodeOp(0x4AC0) == Op::Tas);        // tas d0
    CHECK(m68k::decodeOp(0x4AFC) == Op::Illegal);    // ILLEGAL, not tas #imm
    CHECK(m68k::decodeOp(0x4808) == Op::Illegal);    // nbcd a0
    CHECK(m68k::decodeOp(0x41BC) == Op::Chk);        // chk #,d0
    CHECK(m68k::decodeOp(0x4188) == Op::Illegal);    // chk a0,d0
    CHECK(m68k::decodeOp(0x80FC) == Op::Divu);       // divu #,d0
    CHECK(m68k::decodeOp(0x81C8) == Op::Illegal);    // divs a0,d0
    CHECK(m68k::decodeOp(0xC10F) == Op::Abcd);       // abcd -(a7),-(a0)
    CHECK(m68k::decodeOp(0xC141) == Op::Exg);        // exg d0,d1 is not ABCD
    CHECK(m68k::decodeOp(0x8101) == Op::Sbcd);       // sbcd d1,d0
    CHECK(m68k::decodeOp(0x8001) == Op::OrB);        // or.b d1,d0
}

TEST(m68k_decode_logic_modes) {
    using m68k::Op;
    CHECK(m68k::decodeOp(0xC041) == Op::AndW);         // and.w d1,d0
    CHECK(m68k::decodeOp(0xC050) == Op::AndW);         // and.w (a0),d0
    CHECK(m68k::decodeOp(0xC048) == Op::Illegal);      // and.w a0,d0
    CHECK(m68k::decodeOp(0xC150) == Op::AndToMemW);    // and.w d0,(a0)
    CHECK(m68k::decodeOp(0xC140) == Op::Exg);          // exg d0,d0, not and.w d0,d0
    CHECK(m68k::decodeOp(0xC180) == Op::Illegal);      // and.l d0,d0: no Dn destination form
    CHECK(m68k::decodeOp(0x8150) == Op::OrToMemW);     // or.w d0,(a0)
    CHECK(m68k::decodeOp(0x8140) == Op::Illegal);      // pack: 68020
    CHECK(m68k::decodeOp(0xB340) == Op::EorW);         // eor.w d1,d0
    CHECK(m68k::decodeOp(0xB35A) == Op::EorW);         // eor.w d1,(a2)+
    CHECK(m68k::decodeOp(0xB37A) == Op::Illegal);      // eor.w d1,(d16,pc)
    CHECK(m68k::decodeOp(0x0200) == Op::AndiB);
    CHECK(m68k::decodeOp(0x0A80) == Op::EoriL);
    CHECK(m68k::decodeOp(0x023C) == Op::Illegal);      // andi to ccr (Task 6)
    CHECK(m68k::decodeOp(0x4640) == Op::NotW);
    CHECK(m68k::decodeOp(0x46C0) == Op::Illegal);      // move to sr (Task 6)
    CHECK(m68k::decodeOp(0xE300) == Op::ShiftRegB);
    CHECK(m68k::decodeOp(0xE3A8) == Op::ShiftRegL);
    CHECK(m68k::decodeOp(0xE2D0) == Op::ShiftMem);     // lsr.w (a0)
    CHECK(m68k::decodeOp(0xE0C0) == Op::Illegal);      // asr.w d0: memory form needs memory
    CHECK(m68k::decodeOp(0xE8C0) == Op::Illegal);      // bfxxx: 68020
    CHECK(m68k::decodeOp(0x0300) == Op::BtstDyn);      // btst d1,d0
    CHECK(m68k::decodeOp(0x033C) == Op::BtstDyn);      // btst d1,#imm is allowed
    CHECK(m68k::decodeOp(0x0340) == Op::BchgDyn);
    CHECK(m68k::decodeOp(0x037C) == Op::Illegal);      // bchg d1,#imm is not
    CHECK(m68k::decodeOp(0x0800) == Op::BtstImm);      // btst #n,d0
    CHECK(m68k::decodeOp(0x083A) == Op::BtstImm);      // btst #n,(d16,pc)
    CHECK(m68k::decodeOp(0x083C) == Op::Illegal);      // btst #n,#imm
    CHECK(m68k::decodeOp(0x08FA) == Op::Illegal);      // bset #n,(d16,pc)
    CHECK(m68k::decodeOp(0x0108) == Op::MovepToRegW);  // not btst d0,a0
}

TEST(m68k_shift_helper) {
    // ASR keeps the sign and C = X = the last bit out; V stays clear.
    const m68k::AluOut a = m68k::shift(m68k::kAsr, 0x80, 3, 1, false);
    CHECK_EQ(a.value, 0xF0u);
    CHECK_EQ(a.ccr, kN);  // the bit shifted out last is 0
    const m68k::AluOut b = m68k::shift(m68k::kAsr, 0x81, 8, 1, false);
    CHECK_EQ(b.value, 0xFFu);
    CHECK_EQ(b.ccr, kX | kN | kC);  // count = size: the sign bit is the last bit out
    // DECISION M18: a count above the size clears C and X (the suite), the value stays all ones.
    const m68k::AluOut b2 = m68k::shift(m68k::kAsr, 0x81, 63, 1, true);
    CHECK_EQ(b2.value, 0xFFu);
    CHECK_EQ(b2.ccr, kN);
    // ASL: V when the sign changes at any step, also if it changes back.
    const m68k::AluOut c = m68k::shift(m68k::kAsl, 0x60, 2, 1, false);
    CHECK_EQ(c.value, 0x80u);
    CHECK_EQ(c.ccr, kX | kN | kV | kC);  // 0x60 -> 0xC0 -> 0x80, the last bit out is 1
    CHECK_EQ(m68k::shift(m68k::kAsl, 0xC0, 1, 1, false).ccr, kX | kN | kC);
    CHECK_EQ(m68k::shift(m68k::kAsl, 0x90, 2, 1, false).ccr, kV);  // 0x90 -> 0x20 -> 0x40
    // Count 0: X unchanged, C cleared (ROXx: C = X), V cleared.
    CHECK_EQ(m68k::shift(m68k::kLsr, 0x81, 0, 1, true).ccr, kX | kN);
    CHECK_EQ(m68k::shift(m68k::kRoxr, 0x00, 0, 1, true).ccr, kX | kZ | kC);
    // Rotates leave X; ROXx rotate through it.
    CHECK_EQ(m68k::shift(m68k::kRol, 0x81, 1, 1, false).value, 0x03u);
    CHECK_EQ(m68k::shift(m68k::kRol, 0x81, 1, 1, false).ccr, kC);
    CHECK_EQ(m68k::shift(m68k::kRor, 0x0001, 1, 2, true).ccr, kX | kN | kC);
    CHECK_EQ(m68k::shift(m68k::kRoxr, 0x01, 1, 1, false).ccr, kX | kZ | kC);
    CHECK_EQ(m68k::shift(m68k::kRoxr, 0x00, 1, 1, true).value, 0x80u);
    // ROXL of a long takes 33 steps to come back: 33 rotates are the identity.
    const m68k::AluOut d = m68k::shift(m68k::kRoxl, 0x12345678, 33, 4, true);
    CHECK_EQ(d.value, 0x12345678u);
    CHECK_EQ(d.ccr & kX, kX);
    // Counts of the operand width and beyond.
    CHECK_EQ(m68k::shift(m68k::kLsl, 0x80, 8, 1, false).ccr, kZ);  // the 1 leaves at step 1: C = X = 0 at the end
    CHECK_EQ(m68k::shift(m68k::kLsr, 0x80000000u, 32, 4, false).ccr, kX | kZ | kC);  // the 1 leaves at step 32
    CHECK_EQ(m68k::shift(m68k::kLsr, 0x00000001u, 32, 4, false).ccr, kZ);  // the 1 leaves at step 1
    CHECK_EQ(m68k::shift(m68k::kRol, 0x80000001u, 32, 4, false).ccr, kN | kC);
}

TEST(m68k_shift_counts_32_to_63) {
    RamBus bus;
    bus.load16(0x400, {0xE3A8, 0x4E71});  // lsl.l d1,d0
    m68k::Cpu cpu(bus);
    const uint32_t counts[3] = {32, 33, 64};
    for (int i = 0; i < 3; ++i) {
        m68k::Registers r = stateAt(bus, 0x400);
        r.sr = 0x2710;  // X set beforehand
        r.d[0] = 1;
        r.d[1] = counts[i];
        cpu.setRegs(r);
        const int cycles = cpu.step();
        const m68k::Registers q = cpu.regs();
        if (i == 0) {
            CHECK_EQ(cycles, 72);
            CHECK_EQ(q.d[0], 0u);
            CHECK_EQ(q.sr & 0x1F, kX | kZ | kC);
        } else if (i == 1) {
            CHECK_EQ(cycles, 74);
            CHECK_EQ(q.d[0], 0u);
            CHECK_EQ(q.sr & 0x1F, kZ);  // C = X = 0
        } else {
            CHECK_EQ(cycles, 8);  // 64 mod 64 = 0
            CHECK_EQ(q.d[0], 1u);
            CHECK_EQ(q.sr & 0x1F, kX);  // C = 0, X unchanged
        }
    }
}

TEST(m68k_asl_sets_v_on_sign_change) {
    RamBus bus;
    bus.load16(0x400, {0xE300, 0x4E71});  // asl.b #1,d0
    m68k::Cpu cpu(bus);
    m68k::Registers r = stateAt(bus, 0x400);
    r.d[0] = 0x12345640;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 8);
    CHECK_EQ(cpu.regs().d[0], 0x12345680u);  // only the byte changes
    CHECK_EQ(cpu.regs().sr & 0x1F, kN | kV);
}

TEST(m68k_roxl_through_x) {
    RamBus bus;
    bus.load16(0x400, {0xE310, 0x4E71});  // roxl.b #1,d0
    m68k::Cpu cpu(bus);
    m68k::Registers r = stateAt(bus, 0x400);
    r.d[0] = 0x80;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 8);
    CHECK_EQ(cpu.regs().d[0], 0u);
    CHECK_EQ(cpu.regs().sr & 0x1F, kX | kZ | kC);
    r.sr = 0x2710;  // X = 1
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 8);
    CHECK_EQ(cpu.regs().d[0], 1u);
    CHECK_EQ(cpu.regs().sr & 0x1F, kX | kC);
}

TEST(m68k_shift_memory_is_word_by_one) {
    RamBus bus;
    bus.load16(0x400, {0xE2D0, 0x4E71});  // lsr.w (a0)
    bus.load16(0x2000, {0x8001});
    m68k::Cpu cpu(bus);
    m68k::Registers r = stateAt(bus, 0x400);
    r.a[0] = 0x2000;
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 12);
    CHECK_EQ(bus.peek16(0x2000), 0x4000u);
    CHECK_EQ(cpu.regs().sr & 0x1F, kX | kC);
}

TEST(m68k_btst_modulo) {
    RamBus bus;
    bus.load16(0x400, {0x0300, 0x0810, 0x0009, 0x4E71});  // btst d1,d0; btst #9,(a0)
    bus.write8(0x2000, 0x02);
    m68k::Cpu cpu(bus);
    m68k::Registers r = stateAt(bus, 0x400);
    r.d[0] = 0x2;     // bit 1 only
    r.d[1] = 33;      // 33 mod 32 = 1
    r.a[0] = 0x2000;
    r.sr = 0x2704;  // Z set beforehand
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 6);
    CHECK_EQ(cpu.regs().sr & kZ, 0);  // bit 1 is set
    CHECK_EQ(cpu.regs().d[0], 2u);
    CHECK_EQ(cpu.step(), 12);  // 8(2/0) + (a0) 4: 9 mod 8 = 1, bit 1 of the byte at (a0)
    CHECK_EQ(cpu.regs().sr & kZ, 0);
    bus.write8(0x2000, 0x00);
    r = stateAt(bus, 0x402);
    r.a[0] = 0x2000;
    cpu.setRegs(r);
    cpu.step();
    CHECK_EQ(cpu.regs().sr & kZ, kZ);
}

TEST(m68k_bset_bclr_bchg_register) {
    RamBus bus;
    bus.load16(0x400, {0x03C0, 0x0380, 0x0340, 0x4E71});  // bset d1,d0; bclr d1,d0; bchg d1,d0
    m68k::Cpu cpu(bus);
    m68k::Registers r = stateAt(bus, 0x400);
    r.d[1] = 3;
    cpu.setRegs(r);
    cpu.step();
    CHECK_EQ(cpu.regs().d[0], 8u);
    CHECK_EQ(cpu.regs().sr & kZ, kZ);  // the bit was clear
    cpu.step();
    CHECK_EQ(cpu.regs().d[0], 0u);
    CHECK_EQ(cpu.regs().sr & kZ, 0);
    cpu.step();
    CHECK_EQ(cpu.regs().d[0], 8u);
}

TEST(m68k_tas_sets_bit7) {
    RamBus bus;
    bus.load16(0x400, {0x4AD0, 0x4AD0, 0x4E71});  // tas (a0) twice
    m68k::Cpu cpu(bus);
    m68k::Registers r = stateAt(bus, 0x400);
    r.a[0] = 0x2000;
    r.sr = 0x2713;  // X V C set beforehand
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 14);
    CHECK_EQ(bus.mem[0x2000], 0x80);
    CHECK_EQ(cpu.regs().sr & 0x1F, kX | kZ);  // V and C cleared, X kept
    CHECK_EQ(cpu.step(), 14);
    CHECK_EQ(bus.mem[0x2000], 0x80);
    CHECK_EQ(cpu.regs().sr & 0x1F, kX | kN);
}

TEST(m68k_scc_dn_and_memory) {
    RamBus bus;
    bus.load16(0x400, {0x57C0, 0x56C0, 0x57D0, 0x4E71});  // seq d0; sne d0; seq (a0)
    m68k::Cpu cpu(bus);
    m68k::Registers r = stateAt(bus, 0x400);
    r.d[0] = 0x12345600;
    r.a[0] = 0x2000;
    r.sr = 0x2704;  // Z
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 6);
    CHECK_EQ(cpu.regs().d[0], 0x123456FFu);
    CHECK_EQ(cpu.step(), 4);
    CHECK_EQ(cpu.regs().d[0], 0x12345600u);  // sne with Z set: false, the low byte is cleared
    CHECK_EQ(cpu.step(), 12);
    CHECK_EQ(bus.mem[0x2000], 0xFF);
}

TEST(m68k_logic_cycles_and_flags) {
    RamBus bus;
    // and.w d1,d0; or.l d1,d0; eor.l d1,d0; not.l d0; andi.w #$00FF,d0; eori.b #$FF,(a0)
    bus.load16(0x400, {0xC041, 0x8081, 0xB380, 0x4680, 0x0240, 0x00FF, 0x0A10, 0x00FF, 0x4E71});
    bus.write8(0x2000, 0x0F);
    m68k::Cpu cpu(bus);
    m68k::Registers r = stateAt(bus, 0x400);
    r.d[0] = 0xFFFF8000;
    r.d[1] = 0x00FF00FF;
    r.a[0] = 0x2000;
    r.sr = 0x2713;  // X V C: logic clears V and C, keeps X
    cpu.setRegs(r);
    CHECK_EQ(cpu.step(), 4);  // and.w
    CHECK_EQ(cpu.regs().d[0], 0xFFFF0000u);
    CHECK_EQ(cpu.regs().sr & 0x1F, kX | kZ);
    CHECK_EQ(cpu.step(), 8);  // or.l d1,d0 (register source)
    CHECK_EQ(cpu.regs().d[0], 0xFFFF00FFu);
    CHECK_EQ(cpu.regs().sr & 0x1F, kX | kN);
    CHECK_EQ(cpu.step(), 8);  // eor.l d1,d0
    CHECK_EQ(cpu.regs().d[0], 0xFF000000u);
    CHECK_EQ(cpu.step(), 6);  // not.l d0
    CHECK_EQ(cpu.regs().d[0], 0x00FFFFFFu);
    CHECK_EQ(cpu.step(), 8);  // andi.w #$00FF,d0: the low word only
    CHECK_EQ(cpu.regs().d[0], 0x00FF00FFu);
    CHECK_EQ(cpu.step(), 16);  // eori.b #$FF,(a0): 12(2/1) + (An) 4
    CHECK_EQ(bus.mem[0x2000], 0xF0);
    CHECK_EQ(cpu.regs().sr & 0x1F, kX | kN);
}

