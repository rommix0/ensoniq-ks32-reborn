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
    CHECK(m68k::decodeOp(0x50C0) == Op::Illegal);    // size 3 is Scc (Task 5)
    CHECK(m68k::decodeOp(0x0C3A) == Op::Illegal);    // cmpi.b #,(d16,pc): not on the 68000
    CHECK(m68k::decodeOp(0x0680) == Op::AddiL);      // addi.l #,d0
    CHECK(m68k::decodeOp(0x0000) == Op::Illegal);    // ori (Task 5)
    CHECK(m68k::decodeOp(0xB308) == Op::CmpmB);      // cmpm.b (a0)+,(a1)+
    CHECK(m68k::decodeOp(0xB300) == Op::Illegal);    // eor.b d1,d0 (Task 5)
    CHECK(m68k::decodeOp(0xB1C8) == Op::CmpaL);      // cmpa.l a0,a0
    CHECK(m68k::decodeOp(0x4A08) == Op::Illegal);    // tst.b a0: not on the 68000
    CHECK(m68k::decodeOp(0x4A3C) == Op::Illegal);    // tst.b #imm: not on the 68000
    CHECK(m68k::decodeOp(0x4AC0) == Op::Illegal);    // tas (Task 5)
    CHECK(m68k::decodeOp(0x4808) == Op::Illegal);    // nbcd a0
    CHECK(m68k::decodeOp(0x41BC) == Op::Chk);        // chk #,d0
    CHECK(m68k::decodeOp(0x4188) == Op::Illegal);    // chk a0,d0
    CHECK(m68k::decodeOp(0x80FC) == Op::Divu);       // divu #,d0
    CHECK(m68k::decodeOp(0x81C8) == Op::Illegal);    // divs a0,d0
    CHECK(m68k::decodeOp(0xC10F) == Op::Abcd);       // abcd -(a7),-(a0)
    CHECK(m68k::decodeOp(0xC141) == Op::Exg);        // exg d0,d1 is not ABCD
    CHECK(m68k::decodeOp(0x8101) == Op::Sbcd);       // sbcd d1,d0
    CHECK(m68k::decodeOp(0x8001) == Op::Illegal);    // or.b d1,d0 (Task 5)
}
