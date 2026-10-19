#include <cstdint>
#include <cstdio>
#include <string>

#include "check.h"
#include "esp/esp_isa.h"

using ens::esp::disassemble;
using ens::esp::Instr;
using ens::esp::Op;
using ens::esp::RamOp;
using ens::esp::regName;

namespace {

void expectLine(const Instr& in, int pc, const char* want) {
    const std::string got = disassemble(in, pc);
    if (got != want) std::printf("  got : %s\n  want: %s\n", got.c_str(), want);
    CHECK(got == want);
}

}  // namespace

TEST(isa_roundtrip) {
    uint64_t x = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 10000; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        const uint64_t w = x & 0xFFFFFFFFFFF8ull;  // 48 bits, low 3 bits clear
        CHECK_EQ(Instr::decode(w).encode(), w);
    }
}

TEST(isa_fields) {
    const Instr i = Instr::decode(0x0102'0304'D5A8ull);
    CHECK_EQ(i.d, 1);
    CHECK_EQ(i.c, 2);
    CHECK_EQ(i.b, 3);
    CHECK_EQ(i.a, 4);
    CHECK(i.op == Op::DIFF);
    CHECK_EQ(static_cast<int>(i.op), 0xD);
    CHECK_EQ(i.opsel, 5);
    CHECK(i.skip);
    CHECK(!i.mac);
    CHECK_EQ(static_cast<int>(i.ram), 5);
    CHECK(i.ram == RamOp::ReadDelayDump);
    // The unused low three bits are ignored by decode and written as 0 by encode.
    CHECK_EQ(Instr::decode(0x0102'0304'D5AFull).encode(), 0x0102'0304'D5A8ull);
}

TEST(dis_line) {
    // MOV gpr5 > gpr7 with a multiplier NOP (ZERO x ZERO > ZERO).
    expectLine(Instr{255, 255, 5, 7, Op::MOV, 0, false, false, RamOp::ReadDelay}, 12,
               "012  MOV   gpr5, gpr7 > gpr7 | MUL ZERO x ZERO > ZERO | RAM read-delay");
    // Operand select D: ALU source DIL, MUL source DIL, MUL dest C and DOL; MAC and Skip set.
    expectLine(Instr{10, 11, 3, 4, Op::ADD, 0xD, true, true, RamOp::WriteDelay}, 5,
               "005  ADD   gpr3, DIL > gpr4 | MUL gpr10 x DIL > gpr11,DOL +MAC"
               " | RAM write-delay SKIP");
    // Operand select 6: ALU dest DOL only, MUL source DIL.
    expectLine(Instr{20, 21, 1, 2, Op::XOR, 6, false, false, RamOp::ReadIo}, 100,
               "100  XOR   gpr1, gpr2 > DOL | MUL gpr20 x DIL > gpr21 | RAM read-io");
    // END has no ALU part; its multiplier and RAM fields still print.
    expectLine(Instr{255, 255, 255, 255, Op::END, 0, false, true, RamOp::ReadDelay}, 88,
               "088  END   | MUL ZERO x ZERO > ZERO +MAC | RAM read-delay");
    // CMP writes nothing; address 244 reads as DIL as a source and is MEMSIZ as a destination.
    expectLine(Instr{244, 244, 244, 244, Op::CMP, 0, false, false, RamOp::ReadTableA}, 1,
               "001  CMP   DIL, DIL | MUL DIL x DIL > MEMSIZ | RAM read-A");
}

TEST(regname_all_spr) {
    CHECK(regName(243) == "MACH");
    CHECK(regName(244) == "DIL/MEMSIZ");
    CHECK(regName(250) == "CCR");
    CHECK(regName(200) == "?200");
    CHECK(regName(0) == "gpr0");
    CHECK(regName(191) == "gpr191");
    CHECK(regName(192) == "?192");
    CHECK(regName(233) == "?233");
    CHECK(regName(234) == "SER0R");
    CHECK(regName(235) == "SER0L");
    CHECK(regName(241) == "SER3L");
    CHECK(regName(242) == "MACL");
    CHECK(regName(245) == "DLENGTH");
    CHECK(regName(246) == "ABASE");
    CHECK(regName(247) == "BBASE");
    CHECK(regName(248) == "DBASE");
    CHECK(regName(249) == "SIGREG");
    CHECK(regName(251) == "CMR");
    CHECK(regName(252) == "-1");
    CHECK(regName(253) == "MIN");
    CHECK(regName(254) == "MAX");
    CHECK(regName(255) == "ZERO");
}
