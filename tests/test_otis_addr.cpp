#include <utility>
#include <vector>

#include "check.h"
#include "otis/otis.h"

using namespace ens;

namespace {

struct FakeWave : WaveMemory {
    std::vector<std::pair<int, uint32_t>> reads;
    uint16_t read(int bank, uint32_t addr) override {
        reads.emplace_back(bank, addr);
        return static_cast<uint16_t>(addr & 0xFFFF);
    }
    bool saw(uint32_t addr) const {
        for (const auto& r : reads)
            if (r.second == addr) return true;
        return false;
    }
};

// CR bits
constexpr uint16_t BS = 0x0004, LPE = 0x0008, BLE = 0x0010, IRQE = 0x0020, DIR = 0x0040,
                   IRQ = 0x0080, STOP0 = 0x0001, STOP1 = 0x0002;

// Write a 29-bit position (samples << 9, plus optional half-sample) into a register pair.
void writePos(Otis& o, int regHi, uint32_t samples) {
    const uint32_t p = samples << 9;
    o.write(static_cast<uint8_t>(regHi), static_cast<uint16_t>(p >> 16));
    o.write(static_cast<uint8_t>(regHi + 1), static_cast<uint16_t>(p & 0xFFFF));
}

struct Setup {
    uint16_t cr = 0;
    uint16_t fc = 0x0400;
    uint32_t start = 0, end = 100;
    uint32_t acc = 0;
};

void setupVoice(Otis& o, int voice, const Setup& s) {
    o.write(15, static_cast<uint16_t>(voice));
    o.write(0, s.cr);
    o.write(1, s.fc);
    writePos(o, 2, s.start);
    writePos(o, 4, s.end);
    writePos(o, 10, s.acc);
}

}  // namespace

TEST(otis_forward_step) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);  // ACT=0: only voice 0 runs
    Setup s;
    s.fc = 0x0400;
    s.acc = 10;
    s.end = 100;
    setupVoice(o, 0, s);
    o.runFrame();
    CHECK_EQ(o.acc29(0), 11u << 9);
}

TEST(otis_fraction_step) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.fc = 0x0300;
    s.acc = 0;
    setupVoice(o, 0, s);
    for (int i = 0; i < 4; ++i) o.runFrame();
    CHECK_EQ(o.acc29(0), 3u << 9);
}

TEST(otis_oneshot_stops_at_end) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.acc = 98;
    s.end = 100;
    setupVoice(o, 0, s);
    o.runFrame();
    o.runFrame();
    o.write(15, 0);
    CHECK(o.read(0) & STOP0);
    CHECK_EQ(o.acc29(0), 100u << 9);
    o.runFrame();
    CHECK_EQ(o.acc29(0), 100u << 9);
}

TEST(otis_forward_loop_keeps_remainder) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.cr = LPE;
    s.fc = 0x0600;
    s.start = 10;
    s.end = 20;
    s.acc = 19;
    setupVoice(o, 0, s);
    o.runFrame();
    CHECK_EQ(o.acc29(0), (10u << 9) + 256u);
}

TEST(otis_bidi_reflects_and_toggles_dir) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.cr = LPE | BLE;
    s.fc = 0x0600;
    s.start = 10;
    s.end = 20;
    s.acc = 19;
    setupVoice(o, 0, s);
    o.runFrame();
    CHECK_EQ(o.acc29(0), (19u << 9) + 256u);
    o.write(15, 0);
    CHECK(o.read(0) & DIR);
    o.runFrame();
    CHECK_EQ(o.acc29(0), 18u << 9);
    CHECK(o.read(0) & DIR);
}

TEST(otis_bidi_reflects_at_start_and_toggles_back) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.cr = LPE | BLE | DIR;
    s.fc = 0x0600;
    s.start = 10;
    s.end = 20;
    s.acc = 11;
    setupVoice(o, 0, s);
    o.runFrame();  // n = 9.5, 0.5 below START -> 10.5, DIR cleared
    CHECK_EQ(o.acc29(0), (10u << 9) + 256u);
    o.write(15, 0);
    CHECK(!(o.read(0) & DIR));
}

TEST(otis_reverse_loop) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.cr = DIR | LPE;
    s.fc = 0x0400;
    s.start = 10;
    s.end = 20;
    s.acc = 10;
    setupVoice(o, 0, s);
    o.runFrame();
    CHECK_EQ(o.acc29(0), 19u << 9);
}

TEST(otis_reverse_oneshot_stops_at_start) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.cr = DIR;
    s.start = 10;
    s.end = 20;
    s.acc = 10;
    setupVoice(o, 0, s);
    o.runFrame();
    o.write(15, 0);
    CHECK(o.read(0) & STOP0);
    CHECK_EQ(o.acc29(0), 10u << 9);
}

TEST(otis_accumulator_wraps_in_29_bits) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.start = 0;
    s.end = 0x80000;  // 2^19 samples: well below the wrapped result's complement
    s.acc = 0xFFFFF;  // highest 20-bit integer address
    setupVoice(o, 0, s);
    o.runFrame();  // 2^29 wraps to 0, which is below END: no boundary
    CHECK_EQ(o.acc29(0), 0u);
}

TEST(otis_stop1_freezes_address) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.cr = STOP1;
    s.acc = 50;
    setupVoice(o, 0, s);
    o.runFrame();
    CHECK_EQ(o.acc29(0), 50u << 9);
    CHECK(m.saw(50));
    CHECK(m.saw(51));
}

TEST(otis_s2_address_in_reverse_is_int_plus_1) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.cr = DIR;
    s.start = 0;
    s.acc = 50;
    setupVoice(o, 0, s);
    o.runFrame();
    CHECK(m.saw(50));
    CHECK(m.saw(51));
}

TEST(otis_bank_select_follows_bs) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.cr = STOP1 | BS;
    s.acc = 7;
    setupVoice(o, 0, s);
    o.runFrame();
    CHECK_EQ(m.reads.size(), 2u);
    CHECK_EQ(m.reads[0].first, 1);
    CHECK_EQ(m.reads[0].second, 7u);
    CHECK_EQ(m.reads[1].first, 1);
    CHECK_EQ(m.reads[1].second, 8u);
}

TEST(otis_s2_wraps_in_20_bits) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.cr = STOP1;
    s.acc = 0xFFFFF;
    setupVoice(o, 0, s);
    o.runFrame();
    CHECK(m.saw(0xFFFFF));
    CHECK(m.saw(0));
}

TEST(otis_frame_ready_after_act_slot) {
    FakeWave m;
    Otis o(m);
    o.write(13, 2);  // slots 0..2
    CHECK(!o.frameReady());
    o.stepVoice();
    o.stepVoice();
    CHECK(!o.frameReady());
    o.stepVoice();
    CHECK(o.frameReady());
    o.takeFrame();
    CHECK(!o.frameReady());
}

TEST(otis_irq_sequence) {
    FakeWave m;
    Otis o(m);
    Setup s;
    s.cr = IRQE;
    s.acc = 99;
    s.end = 100;
    setupVoice(o, 3, s);
    o.runFrame();
    CHECK(o.irqAsserted());
    const uint16_t v = o.read(14);
    CHECK_EQ(v & 0x1F, 3);
    CHECK_EQ(v & 0x80, 0);
    CHECK(!o.irqAsserted());
    CHECK_EQ(o.read(14) & 0x80, 0x80);  // vector now free
    o.write(15, 3);
    CHECK(o.read(0) & IRQ);  // still set until voice 3 is next processed
    o.runFrame();
    CHECK(!(o.read(0) & IRQ));
    CHECK(!o.irqAsserted());
}

TEST(otis_irq_two_voices_stack) {
    FakeWave m;
    Otis o(m);
    Setup s;
    s.cr = IRQE;
    s.acc = 99;
    s.end = 100;
    setupVoice(o, 2, s);
    setupVoice(o, 5, s);
    o.runFrame();
    CHECK(o.irqAsserted());
    CHECK_EQ(o.read(14) & 0x1F, 2);
    CHECK(!o.irqAsserted());
    o.runFrame();
    CHECK(o.irqAsserted());
    const uint16_t v = o.read(14);
    CHECK_EQ(v & 0x1F, 5);
    CHECK_EQ(v & 0x80, 0);
}

TEST(otis_irq_not_replaced_while_pending) {
    FakeWave m;
    Otis o(m);
    Setup s;
    s.cr = IRQE;
    s.acc = 99;
    s.end = 100;
    setupVoice(o, 2, s);
    setupVoice(o, 5, s);
    o.runFrame();
    o.runFrame();  // host has not read: the vector must still hold voice 2
    CHECK_EQ(o.read(14) & 0x1F, 2);
}

TEST(otis_irq_requires_irqe) {
    FakeWave m;
    Otis o(m);
    Setup s;
    s.cr = 0;
    s.acc = 99;
    s.end = 100;
    setupVoice(o, 3, s);
    o.runFrame();
    CHECK(!o.irqAsserted());
    o.write(15, 3);
    CHECK(!(o.read(0) & IRQ));
}

TEST(otis_irq_raised_in_loop_mode) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.cr = LPE | IRQE;
    s.start = 10;
    s.end = 20;
    s.acc = 19;
    setupVoice(o, 0, s);
    o.runFrame();
    CHECK(o.irqAsserted());
    CHECK_EQ(o.read(14) & 0x1F, 0);
}

TEST(otis_forced_irq) {
    FakeWave m;
    Otis o(m);
    o.write(15, 7);
    o.write(0, IRQE | IRQ | STOP1);
    CHECK(!o.irqAsserted());
    o.runFrame();
    CHECK(o.irqAsserted());
    CHECK_EQ(o.read(14) & 0x1F, 7);
}

TEST(otis_reverse_boundary_equality_oneshot) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.cr = DIR;
    s.fc = 0x0400;
    s.start = 10;
    s.end = 20;
    s.acc = 11;
    setupVoice(o, 0, s);
    o.runFrame();  // n = 10.0 == START: the boundary is hit
    o.write(15, 0);
    CHECK(o.read(0) & STOP0);
    CHECK_EQ(o.acc29(0), 10u << 9);
}

TEST(otis_reverse_boundary_equality_loop) {
    FakeWave m;
    Otis o(m);
    o.write(13, 0);
    Setup s;
    s.cr = DIR | LPE;
    s.fc = 0x0400;
    s.start = 10;
    s.end = 20;
    s.acc = 11;
    setupVoice(o, 0, s);
    o.runFrame();  // n == START with zero overshoot: acc = END - 0
    CHECK_EQ(o.acc29(0), 20u << 9);
}
