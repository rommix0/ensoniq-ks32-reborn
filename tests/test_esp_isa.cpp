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
               "012  MOV   gpr5 > gpr7 | MUL ZERO x ZERO > ZERO | RAM read-delay");
    // Operand select D: ALU source DIL, MUL source DIL, MUL dest C and DOL; MAC and Skip set.
    expectLine(Instr{10, 11, 3, 4, Op::ADD, 0xD, true, true, RamOp::WriteDelay}, 5,
               "005  ADD   gpr3, DIL > gpr4 | MUL gpr10 x DIL > gpr11,DOL +MAC"
               " | RAM write-delay SKIP");
    // Every one-operand op omits the unused A source.
    expectLine(Instr{255, 255, 5, 7, Op::ASR, 0xC, false, false, RamOp::ReadDelay}, 0,
               "000  ASR   gpr5 > gpr7 | MUL ZERO x DIL > ZERO | RAM read-delay");
    expectLine(Instr{255, 255, 244, 7, Op::MOV, 0, false, false, RamOp::ReadDelay}, 0,
               "000  MOV   DIL > gpr7 | MUL ZERO x ZERO > ZERO | RAM read-delay");
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

// Operand select rows 0..F against the notes section 4.1.3 table, written as letters
// (ALU src, ALU dest, MUL src, MUL dest): src A = register, I = DIL; dest A/C = register,
// F = DOL only, B = register and DOL.
TEST(dis_opsel_table) {
    static const char* const kRows[16] = {
        "AACC", "AACF", "AACB", "AAIC", "AAIB", "AFCC", "AFIC", "ABCC",
        "ABIC", "IACC", "IACF", "IACB", "IAIC", "IAIB", "IBCC", "IBIC"};
    for (unsigned row = 0; row < 16; ++row) {
        const char* r = kRows[row];
        const std::string aluSrc = r[0] == 'I' ? "DIL" : "gpr1";
        const std::string aluDst = r[1] == 'A' ? "gpr1" : r[1] == 'F' ? "DOL" : "gpr1,DOL";
        const std::string mulSrc = r[2] == 'I' ? "DIL" : "gpr3";
        const std::string mulDst = r[3] == 'C' ? "gpr3" : r[3] == 'F' ? "DOL" : "gpr3,DOL";
        const std::string want = "000  ADD   gpr2, " + aluSrc + " > " + aluDst +
                                 " | MUL gpr4 x " + mulSrc + " > " + mulDst +
                                 " | RAM read-delay";
        const std::string got = disassemble(
            Instr{4, 3, 2, 1, Op::ADD, static_cast<uint8_t>(row), false, false,
                  RamOp::ReadDelay},
            0);
        if (got != want) std::printf("  row %u\n  got : %s\n  want: %s\n", row, got.c_str(), want.c_str());
        CHECK(got == want);
    }
}

TEST(regname_all_spr) {
    static const char* const kSpr[22] = {
        "SER0R", "SER0L", "SER1R", "SER1L", "SER2R", "SER2L", "SER3R", "SER3L",
        "MACL",  "MACH",  "DIL/MEMSIZ", "DLENGTH", "ABASE", "BBASE", "DBASE", "SIGREG",
        "CCR",   "CMR",   "-1",   "MIN",   "MAX",   "ZERO"};
    for (int a = 0; a < 256; ++a) {
        const std::string got = regName(static_cast<uint8_t>(a));
        const std::string want = a < 192   ? "gpr" + std::to_string(a)
                                 : a < 234 ? "?" + std::to_string(a)
                                           : kSpr[a - 234];
        if (got != want) std::printf("  addr %d: got %s want %s\n", a, got.c_str(), want.c_str());
        CHECK(got == want);
    }
    CHECK(regName(243) == "MACH");
    CHECK(regName(244) == "DIL/MEMSIZ");
    CHECK(regName(250) == "CCR");
    CHECK(regName(200) == "?200");
}
