#include <cstdint>
#include <vector>

#include "check.h"
#include "esp/esp.h"
#include "esp/esp_isa.h"

using ens::Esp;
using ens::EspBus;
using ens::esp::Instr;
using ens::esp::Op;
using ens::esp::RamOp;

namespace {

constexpr uint8_t kZero = 255;  // constant 0: a source of 0 and a discarding destination

// ALU word with a multiplier NOP (0 x+ 0 > ZERO). MOV has no CCR effect.
uint64_t alu(Op op, uint8_t b, uint8_t a, uint8_t opsel = 0, bool skip = false,
             RamOp ram = RamOp::ReadDelay) {
    return Instr{kZero, kZero, b, a, op, opsel, skip, true, ram}.encode();
}
uint64_t nop(RamOp ram = RamOp::ReadDelay) { return alu(Op::MOV, kZero, kZero, 0, false, ram); }
uint64_t end() {
    return Instr{kZero, kZero, kZero, kZero, Op::END, 0, false, true, RamOp::ReadDelay}.encode();
}
// Multiplier word (C x D > C) with an ALU NOP.
uint64_t mul(uint8_t c, uint8_t d, bool mac, uint8_t opsel = 0) {
    return Instr{d, c, kZero, kZero, Op::MOV, opsel, false, mac, RamOp::ReadDelay}.encode();
}

struct Access {
    bool io;
    uint32_t addr;
    uint16_t v;
};

// 64K-word DRAM; addresses are masked to the configured size (out-of-contract offsets).
struct FakeBus : EspBus {
    std::vector<uint16_t> ram = std::vector<uint16_t>(0x10000, 0);
    std::vector<Access> writes;
    int reads = 0;
    uint16_t read(bool, uint32_t a) override {
        ++reads;
        return ram[a & 0xFFFF];
    }
    void write(bool io, uint32_t a, uint16_t v) override {
        writes.push_back({io, a, v});
        if (!io) ram[a & 0xFFFF] = v;
    }
};

constexpr uint32_t kShiftOne = 0x400000;  // SIGREG bit 22 (KS-32 firmware setting)

}  // namespace

TEST(esp_mov_and_end) {
    FakeBus bus;
    Esp e(bus);
    e.setInstr(0, alu(Op::MOV, 1, 2));
    e.setInstr(1, end());
    e.setGpr(1, 0x123456);
    CHECK_EQ(e.runSample(), 2);
    CHECK_EQ(e.gpr(2), 0x123456u);  // committed in the END step (E17 ordering 3)
}

TEST(esp_alu_result_not_visible_to_next_mul) {
    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, kShiftOne);
    e.setInstr(0, alu(Op::ADD, 2, 3));  // gpr3 = gpr2 + gpr3 = 0x100000 + 0x200000
    e.setInstr(1, mul(3, 4, false));     // reads the OLD gpr3 (0x200000) x 0.5
    e.setInstr(2, end());
    e.setGpr(2, 0x100000);
    e.setGpr(3, 0x200000);
    e.setGpr(4, 0x400000);
    e.runSample();
    // DECISION E17: 0x200000 * 0x400000 << 1 -> MACH 0x100000 (new value would give 0x180000).
    CHECK_EQ(e.gpr(3), 0x100000u);
}

TEST(esp_mul_result_visible_to_next_alu) {
    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, kShiftOne);
    e.setInstr(0, mul(5, 6, false));
    e.setInstr(1, alu(Op::MOV, 5, 7));
    e.setInstr(2, end());
    e.setGpr(5, 0x200000);
    e.setGpr(6, 0x400000);
    e.runSample();
    CHECK_EQ(e.gpr(5), 0x100000u);
    CHECK_EQ(e.gpr(7), 0x100000u);  // MOV in the next step saw the product
}

TEST(esp_alu_result_visible_two_steps_later_to_mul) {
    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, kShiftOne);
    e.setInstr(0, alu(Op::MOV, 1, 3));  // gpr3 = 0x400000
    e.setInstr(1, nop());
    e.setInstr(2, mul(3, 4, false));    // sees the new gpr3
    e.setInstr(3, end());
    e.setGpr(1, 0x400000);
    e.setGpr(3, 0x200000);
    e.setGpr(4, 0x400000);
    e.runSample();
    CHECK_EQ(e.gpr(3), 0x200000u);
}

TEST(esp_mac_preload_slot) {
    FakeBus bus;
    Esp e(bus);
    e.setInstr(0, alu(Op::MOV, 10, 243));  // MACH = gpr10
    e.setInstr(1, mul(11, kZero, true));   // gpr11 = MACH + 0 x 0
    e.setInstr(2, end());
    e.setGpr(10, 0x876543);
    e.runSample();
    CHECK_EQ(e.gpr(11), 0x876543u);

    // A plain multiply in the slot discards the preload.
    FakeBus bus2;
    Esp p(bus2);
    p.setInstr(0, alu(Op::MOV, 10, 243));
    p.setInstr(1, mul(11, kZero, false));
    p.setInstr(2, end());
    p.setGpr(10, 0x123456);
    p.runSample();
    CHECK_EQ(p.gpr(11), 0u);

    // Preload in the same word as the accumulate is too late: the MAC sees the old value.
    FakeBus bus3;
    Esp s(bus3);
    s.setInstr(0, Instr{kZero, 11, 10, 243, Op::MOV, 0, false, true, RamOp::ReadDelay}.encode());
    s.setInstr(1, end());
    s.setGpr(10, 0x123456);
    s.runSample();
    CHECK_EQ(s.gpr(11), 0u);
    CHECK_EQ(s.gpr(243), 0x123456u);  // the preload itself happened (END step)
}

TEST(esp_skip_suppresses_writes_not_ram) {
    // DECISION E5: a skipped word writes no register, pushes no DOL word and leaves the
    // accumulator alone, but its RAM-control field still runs.
    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, kShiftOne);
    e.setGpr(250, 0x080000);  // CCR Z=1
    e.setGpr(251, 0x080000);  // CMR = NEQ (0x08 << 16): skip when Z=1
    // ADD gpr1 + gpr2 > gpr2,DOL (opsel 7); MUL gpr3 x gpr4 > gpr3; RAM write delay @ gpr0.
    e.setInstr(0, Instr{4, 3, 1, 2, Op::ADD, 7, true, true, RamOp::WriteDelay}.encode());
    e.setInstr(1, end());
    e.setGpr(1, 0x111100);
    e.setGpr(2, 0x222200);
    e.setGpr(3, 0x400000);
    e.setGpr(4, 0x400000);
    e.runSample();
    CHECK_EQ(e.gpr(2), 0x222200u);
    CHECK_EQ(e.gpr(3), 0x400000u);
    CHECK_EQ(e.mac().raw(), 0);
    CHECK_EQ(e.gpr(250), 0x080000u);  // CCR untouched
    CHECK_EQ(bus.writes.size(), 1u);  // the RAM write still happened...
    if (bus.writes.size() == 1) {
        CHECK(!bus.writes[0].io);
        CHECK_EQ(bus.writes[0].addr, 0u);
        CHECK_EQ(bus.writes[0].v, 0u);  // ...from an empty FIFO (no push)
    }

    // Control: same program with CMR = FALSE (0x00) executes.
    FakeBus bus2;
    Esp x(bus2);
    x.setGpr(249, kShiftOne);
    x.setGpr(250, 0x080000);
    x.setInstr(0, Instr{4, 3, 1, 2, Op::ADD, 7, true, true, RamOp::WriteDelay}.encode());
    x.setInstr(1, end());
    x.setGpr(1, 0x111100);
    x.setGpr(2, 0x222200);
    x.setGpr(3, 0x400000);
    x.setGpr(4, 0x400000);
    x.runSample();
    CHECK_EQ(x.gpr(2), 0x333300u);
    CHECK_EQ(x.gpr(3), 0x200000u);
    CHECK_EQ(bus2.writes.size(), 1u);
    if (bus2.writes.size() == 1) CHECK_EQ(bus2.writes[0].v, 0x3333u);
    // DECISION E4: a skippable instruction never sets the CCR, even when it executes.
    CHECK_EQ(x.gpr(250), 0x080000u);
}

TEST(esp_cmp_ignores_skip) {
    // DECISION E4: CMP with Skip=1 and CMR = TRUE (0x04: always skip) still sets the CCR.
    FakeBus bus;
    Esp e(bus);
    e.setGpr(251, 0x040000);
    e.setInstr(0, alu(Op::CMP, 2, 1, 0, true));  // gpr1 - gpr2 = 1 - 2
    e.setInstr(1, end());
    e.setGpr(1, 1);
    e.setGpr(2, 2);
    e.runSample();
    CHECK_EQ(e.gpr(250), 0xD00000u);  // N | C(borrow) | LT
    CHECK_EQ(e.gpr(1), 1u);
}

TEST(esp_delay_line_roundtrip) {
    FakeBus bus;
    Esp e(bus);
    e.setGpr(244, 0x0000FF);  // MEMSIZ (write side of 244): 64K, unit 0x100
    e.setGpr(245, 0x00FF00);  // DLENGTH: 256-word ring
    e.setGpr(0, 0x000000);    // write offset
    e.setGpr(1, 0x000400);    // read offset: 4 samples back
    e.setInstr(0, alu(Op::MOV, 235, kZero, 5, false, RamOp::WriteDelay));  // SER0L > DOL
    e.setInstr(1, nop(RamOp::ReadDelay));
    e.setInstr(2, nop());
    e.setInstr(3, alu(Op::MOV, 244, 241));  // DIL > SER3L (1-operand, case ii)
    e.setInstr(4, end());

    const int kN = 24;
    std::vector<int16_t> in, out;
    for (int n = 0; n < kN; ++n) {
        const int16_t x = static_cast<int16_t>((n & 1) ? -1000 * (n + 1) : 1000 * (n + 1));
        in.push_back(x);
        e.setSerialIn(0, x, 0);
        CHECK_EQ(e.runSample(), 5);
        int16_t l = 0, r = 0;
        e.serialOut(3, l, r);
        out.push_back(l);
    }
    int lag = -1;
    for (int L = 0; L <= 8 && lag < 0; ++L) {
        bool ok = true;
        for (int n = 0; n < kN; ++n) ok = ok && out[n] == (n >= L ? in[n - L] : 0);
        if (ok) lag = L;
    }
    // DECISION E18: the delay of `off` = 4 units reads the word written 4 samples earlier.
    CHECK_EQ(lag, 4);
}

TEST(esp_ram_read_visible_two_words_later) {
    // DECISION E18: a read issued by word k lands in DIL after word k+1's operand fetch.
    FakeBus bus;
    bus.ram[0x10] = 0xABCD;
    bus.ram[0x20] = 0x1234;
    Esp e(bus);
    e.setGpr(244, 0x0000FF);
    e.setGpr(0, 0x001000);  // word 0 reads table A @ 0x10
    e.setGpr(1, 0x002000);  // word 1 reads table A @ 0x20
    e.setInstr(0, nop(RamOp::ReadTableA));
    e.setInstr(1, alu(Op::MOV, 244, 20, 0, false, RamOp::ReadTableA));  // DIL not loaded yet
    e.setInstr(2, alu(Op::MOV, 244, 21));                               // word 0's data
    e.setInstr(3, alu(Op::MOV, 244, 22));                               // word 1's data
    e.setInstr(4, end());
    e.runSample();
    CHECK_EQ(e.gpr(20), 0u);
    CHECK_EQ(e.gpr(21), 0xABCD00u);
    CHECK_EQ(e.gpr(22), 0x123400u);
}

TEST(esp_mul_to_fifo_write) {
    // MUL C x D > C,DOL (opsel 2) with a write in the same word stores the product.
    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, kShiftOne);
    e.setGpr(244, 0x0000FF);
    e.setGpr(245, 0x00FF00);
    e.setGpr(0, 0x000500);
    e.setGpr(5, 0x200000);
    e.setGpr(6, 0x400000);
    e.setInstr(0, Instr{6, 5, kZero, kZero, Op::MOV, 2, false, false, RamOp::WriteDelay}.encode());
    e.setInstr(1, end());
    e.runSample();
    CHECK_EQ(bus.writes.size(), 1u);
    if (bus.writes.size() == 1) {
        CHECK_EQ(bus.writes[0].addr, 5u);
        CHECK_EQ(bus.writes[0].v, 0x1000u);
    }
    CHECK_EQ(e.gpr(5), 0x100000u);
}

TEST(esp_ser_regs_left_justified) {
    // DECISION E8: SER registers are 16-bit, left-justified at bits 23..8.
    FakeBus bus;
    Esp e(bus);
    e.setSerialIn(0, -2, 5);
    CHECK_EQ(e.gpr(235), 0xFFFE00u);
    CHECK_EQ(e.gpr(234), 0x000500u);
    e.setGpr(241, 0x8001FF);  // SER3L: bits 7..0 dropped
    int16_t l = 0, r = 0;
    e.serialOut(3, l, r);
    CHECK_EQ(l, -0x7FFF);
    CHECK_EQ(e.gpr(241), 0x800100u);
}

TEST(esp_constants_and_holes) {
    FakeBus bus;
    Esp e(bus);
    // DECISION E12: constants read -1, MIN, MAX, 0; writes are discarded.
    for (int a = 252; a <= 255; ++a) e.setGpr(a, 0x123456);
    CHECK_EQ(e.gpr(252), 0xFFFFFFu);
    CHECK_EQ(e.gpr(253), 0x800000u);
    CHECK_EQ(e.gpr(254), 0x7FFFFFu);
    CHECK_EQ(e.gpr(255), 0u);
    // DECISION E9: 192..233 read 0 and ignore writes (host and program).
    e.setGpr(200, 0x123456);
    CHECK_EQ(e.gpr(200), 0u);
    e.setInstr(0, alu(Op::MOV, 254, 200));
    e.setInstr(1, alu(Op::MOV, 254, 255));
    e.setInstr(2, end());
    e.runSample();
    CHECK_EQ(e.gpr(200), 0u);
    CHECK_EQ(e.gpr(255), 0u);
    // 244 reads the DIL, writes MEMSIZ.
    e.setGpr(244, 0x0000FF);
    CHECK_EQ(e.agen().memsiz, 0xFFu);
    CHECK_EQ(e.gpr(244), 0u);
    // Out-of-range addresses are harmless.
    e.setGpr(-1, 1);
    e.setGpr(256, 1);
    CHECK_EQ(e.gpr(-1), 0u);
    CHECK_EQ(e.gpr(256), 0u);
}

TEST(esp_narrow_spr_widths) {
    // DECISION E22: SIGREG keeps bit 22, CCR bits 23..19, CMR bits 23..18.
    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, 0xFFFFFF);
    e.setGpr(250, 0xFFFFFF);
    e.setGpr(251, 0xFFFFFF);
    CHECK_EQ(e.gpr(249), 0x400000u);
    CHECK_EQ(e.gpr(250), 0xF80000u);
    CHECK_EQ(e.gpr(251), 0xFC0000u);
}

TEST(esp_runsample_terminates_without_end) {
    // DECISION E11: after word 159 the core behaves as if it were END.
    FakeBus bus;
    Esp e(bus);
    e.setGpr(20, 1);
    e.setInstr(0, alu(Op::ADD, 20, 21));  // counts passes through pc 0
    for (int i = 1; i < 160; ++i) e.setInstr(i, nop());
    CHECK_EQ(e.runSample(), 160);
    CHECK_EQ(e.runSample(), 160);
    CHECK_EQ(e.gpr(21), 2u);

    // Mangled words (no END) must not hang or index out of range.
    FakeBus bus2;
    Esp m(bus2);
    uint64_t s = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 160; ++i) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        uint64_t w = (s >> 16) & 0xFFFFFFFFFFF8ull;
        w = (w & ~uint64_t(0xF000)) | (uint64_t((s >> 3) % 15) << 12);  // op 0..14, never END
        m.setInstr(i, w);
    }
    for (int k = 0; k < 3; ++k) CHECK_EQ(m.runSample(), 160);
    for (int a = 0; a < 256; ++a) CHECK(m.gpr(a) <= 0xFFFFFFu);
}

TEST(esp_end_step_executes_mul_and_ram_fields) {
    // DECISION E19: the END word's MUL and RAM fields run; its MUL result lands next sample.
    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, kShiftOne);
    e.setGpr(244, 0x0000FF);
    e.setGpr(245, 0x00FF00);
    e.setGpr(0, 0x000700);
    e.setGpr(5, 0x200000);
    e.setGpr(6, 0x400000);
    e.setInstr(0, Instr{6, 5, kZero, kZero, Op::END, 2, false, false, RamOp::WriteDelay}.encode());
    CHECK_EQ(e.runSample(), 1);
    CHECK_EQ(e.gpr(5), 0x200000u);  // not committed yet
    CHECK_EQ(e.agen().dbase, 0x00FF00u);  // DBASE 0 - unit reloads DLENGTH
    CHECK_EQ(bus.writes.size(), 0u);
    CHECK_EQ(e.runSample(), 1);
    CHECK_EQ(e.gpr(5), 0x100000u);  // committed in step 1 of the next sample
    CHECK_EQ(bus.writes.size(), 1u);
    if (bus.writes.size() == 1) {
        CHECK_EQ(bus.writes[0].addr, 7u);  // address computed with the old DBASE (0)
        CHECK_EQ(bus.writes[0].v, 0x1000u);
    }
    CHECK(!e.halted());
}

TEST(esp_feedback_loop_saturates_not_wraps) {
    // y = sat(sat(x + y_prev) + y_prev * 0.5), x = 0.5 FS: loop gain 1.5 must clip, not wrap.
    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, kShiftOne);
    e.setGpr(12, 0x400000);  // 0.5
    e.setInstr(0, alu(Op::MOV, 10, 11));      // h = y
    e.setInstr(1, alu(Op::MOV, 235, 13));     // t = x (ALU result not yet visible to MUL)
    e.setInstr(2, mul(11, 12, false));        // h = h * 0.5
    e.setInstr(3, alu(Op::ADD, 10, 13));      // t = t + y
    e.setInstr(4, alu(Op::ADD, 11, 13));      // t = t + h
    e.setInstr(5, alu(Op::MOV, 13, 10));      // y = t
    e.setInstr(6, alu(Op::MOV, 13, 241));     // SER3L = t
    e.setInstr(7, end());
    int reached = -1;
    bool negative = false;
    for (int n = 0; n < 100; ++n) {
        e.setSerialIn(0, 0x4000, 0);
        e.runSample();
        int16_t l = 0, r = 0;
        e.serialOut(3, l, r);
        if (l < 0) negative = true;
        if (l == 0x7FFF && reached < 0) reached = n;
    }
    CHECK(reached >= 0 && reached < 10);
    CHECK(!negative);
    CHECK_EQ(e.gpr(10), 0x7FFFFFu);
}

TEST(esp_reset_clears_pipeline) {
    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, kShiftOne);
    e.setGpr(5, 0x200000);
    e.setGpr(6, 0x400000);
    e.setInstr(0, Instr{6, 5, kZero, kZero, Op::END, 0, false, false, RamOp::ReadDelay}.encode());
    e.runSample();  // MUL result pending
    e.reset();
    CHECK_EQ(e.gpr(5), 0u);  // DECISION E23: reset clears GPRs
    e.setGpr(5, 0x200000);
    e.setInstr(0, end());
    e.runSample();
    CHECK_EQ(e.gpr(5), 0x200000u);  // nothing stale committed
    CHECK_EQ(e.instr(0), end());
}

TEST(esp_mul_result_visible_to_next_mul) {
    // DECISION E17: MUL(k-1) commits before MUL(k) fetches (§4.3.3: the destination of the
    // previous multiply can be a multiplier source).
    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, kShiftOne);
    e.setInstr(0, mul(5, 6, false));  // gpr5 = 0.5 x 0.5 = 0x200000
    e.setInstr(1, mul(5, 6, false));  // gpr5 = 0.25 x 0.5 = 0x100000
    e.setInstr(2, end());
    e.setGpr(5, 0x400000);
    e.setGpr(6, 0x400000);
    e.runSample();
    CHECK_EQ(e.gpr(5), 0x100000u);  // stale operand would give 0x200000
}

TEST(esp_ccr_from_previous_alu_drives_skip) {
    // DECISION E17: ALU(k-1)'s CCR commit precedes word k's skip evaluation. No CCR preset.
    // [0] CMP equal -> Z=1; [1] skippable MOV with CMR = NEQ -> skipped.
    {
        FakeBus bus;
        Esp e(bus);
        e.setGpr(251, 0x080000);
        e.setInstr(0, alu(Op::CMP, 2, 1));
        e.setInstr(1, alu(Op::MOV, 3, 4, 0, true));
        e.setInstr(2, end());
        e.setGpr(1, 7);
        e.setGpr(2, 7);
        e.setGpr(3, 0x111111);
        e.runSample();
        CHECK_EQ(e.gpr(4), 0u);
    }
    // Converse: [0] CMP equal (Z=1), [1] CMP unequal (Z=0), [2] skippable MOV -> executes.
    {
        FakeBus bus;
        Esp e(bus);
        e.setGpr(251, 0x080000);
        e.setInstr(0, alu(Op::CMP, 2, 1));
        e.setInstr(1, alu(Op::CMP, 3, 1));
        e.setInstr(2, alu(Op::MOV, 3, 4, 0, true));
        e.setInstr(3, end());
        e.setGpr(1, 7);
        e.setGpr(2, 7);
        e.setGpr(3, 0x111111);
        e.runSample();
        CHECK_EQ(e.gpr(4), 0x111111u);
    }
}

TEST(esp_alu_reads_mac_before_same_word_accumulates) {
    // DECISION E17: ALU(k) fetches MACH/MACL before MUL(k) accumulates.
    const uint32_t x0 = 0x123456, y0 = 0x345678, x1 = 0x2468AC, y1 = 0x13579B;
    const uint32_t x2 = 0x0F0F0F, y2 = 0x5A5A5A;
    auto halves = [](uint32_t x, uint32_t y, uint32_t& hi, uint32_t& lo) {
        ens::esp::Mac m;
        m.accumulate(m.product(x, y, true), false);
        hi = m.readHigh();
        lo = m.readLow();
    };
    uint32_t h0, l0, h1, l1, h2, l2;
    halves(x0, y0, h0, l0);
    halves(x1, y1, h1, l1);
    halves(x2, y2, h2, l2);
    CHECK(l0 != l1 && h1 != h2);

    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, kShiftOne);
    e.setGpr(20, x0); e.setGpr(21, y0);
    e.setGpr(22, x1); e.setGpr(23, y1);
    e.setGpr(24, x2); e.setGpr(25, y2);
    // Each MUL writes MACH back to its own x register; no word reads another word's x.
    e.setInstr(0, Instr{21, 20, kZero, kZero, Op::MOV, 0, false, false, RamOp::ReadDelay}.encode());
    e.setInstr(1, Instr{23, 22, 242, 7, Op::MOV, 0, false, false, RamOp::ReadDelay}.encode());  // r7 = MACL
    e.setInstr(2, Instr{25, 24, 243, 8, Op::MOV, 0, false, false, RamOp::ReadDelay}.encode());  // r8 = MACH
    e.setInstr(3, end());
    e.runSample();
    CHECK_EQ(e.gpr(7), l0);  // word 0's low half, not word 1's
    CHECK_EQ(e.gpr(8), h1);  // word 1's high half, not word 2's
}

TEST(esp_alu_reads_preloaded_mach) {
    // DECISION E17: ALU(k-1) preloads MACH in phase 3; ALU(k) reads it in phase 5, before
    // MUL(k) accumulates. MUL(k) is a plain multiply, so the preload is then discarded.
    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, kShiftOne);
    e.setGpr(10, 0x123456);
    e.setGpr(5, 0x400000);
    e.setGpr(6, 0x400000);
    e.setInstr(0, alu(Op::MOV, 10, 243));                                         // MACH = gpr10
    e.setInstr(1, Instr{6, 5, 243, 11, Op::MOV, 0, false, false, RamOp::ReadDelay}.encode());  // r11 = MACH
    e.setInstr(2, alu(Op::MOV, 243, 12));                                         // r12 = MACH
    e.setInstr(3, end());
    e.runSample();
    CHECK_EQ(e.gpr(11), 0x123456u);  // the preload
    CHECK_EQ(e.gpr(12), 0x200000u);  // word 1's plain multiply (0.5 x 0.5)
}

TEST(esp_mac_saturates_in_core) {
    // DECISION E6: accumulating past full scale is held by the guard bits and saturated on
    // readout; the C destination gets MAX and an ALU read of MACL gives 0xFFFFFF.
    FakeBus bus;
    Esp e(bus);
    e.setGpr(249, kShiftOne);
    e.setGpr(5, 0x7FFFFF);
    e.setGpr(6, 0);
    e.setInstr(0, Instr{5, 5, kZero, kZero, Op::MOV, 1, false, false, RamOp::ReadDelay}.encode());  // ~1.0
    e.setInstr(1, Instr{5, 5, kZero, kZero, Op::MOV, 1, false, true, RamOp::ReadDelay}.encode());   // ~2.0
    e.setInstr(2, Instr{5, 6, kZero, kZero, Op::MOV, 0, false, true, RamOp::ReadDelay}.encode());   // ~3.0 > C=6
    e.setInstr(3, alu(Op::MOV, 242, 7));  // r7 = MACL (MUL NOP keeps the accumulator)
    e.setInstr(4, end());
    e.runSample();
    CHECK(e.mac().raw() > (int64_t(1) << 47));
    CHECK_EQ(e.gpr(6), 0x7FFFFFu);
    CHECK_EQ(e.gpr(7), 0xFFFFFFu);
}
