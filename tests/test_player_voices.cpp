#include <cstdint>
#include <vector>

#include "check.h"
#include "otis/otis.h"
#include "player/voices.h"

using ens::Otis;
using ens::OtisFrame;
using ens::WaveMemory;
using ens::player::Adsr;
using ens::player::Voices;
using ens::player::VoiceConfig;
using ens::player::Wave;
using ens::player::Zone;
using ens::player::fcForPitch;
using ens::player::makeFcTable;

namespace {

// Wave memory: bank 0 and bank 1 each hold a vector of words; anything outside reads 0.
struct TestMem : WaveMemory {
    std::vector<uint16_t> bank[2];
    uint16_t read(int b, uint32_t addr) override {
        const auto& v = bank[b & 1];
        return addr < v.size() ? v[addr] : 0;
    }
};

constexpr int kFramesPerMs = 29762;  // per 1000 ms

int frames(int ms) { return ms * kFramesPerMs / 1000; }

// A wave with one zone whose root key 60 plays at one sample per frame (pitchWord -key*256).
Wave oneZone(uint32_t start, uint32_t loop, uint32_t end, uint8_t crLow, int bank = 0, int endFrac = 0) {
    Wave w;
    Zone z;
    z.topKey = 127;
    z.pitchWord = -15360;
    z.bank = bank;
    z.start = start;
    z.loopStart = loop;
    z.end = end;
    z.endFrac = endFrac;
    z.crLow = crLow;
    w.zones.push_back(z);
    return w;
}

struct Rig {
    TestMem mem;
    Otis otis;
    Voices voices;
    explicit Rig(VoiceConfig cfg = VoiceConfig())
        : otis((mem.bank[0].assign(64, 0x4000), mem.bank[1].assign(64, 0x4000), mem)),
          voices(otis, makeFcTable(), cfg) {
        voices.init();
    }
    // Runs n frames (tick then OTIS frame); returns the last frame.
    OtisFrame run(int n) {
        OtisFrame f{};
        for (int i = 0; i < n; ++i) {
            voices.tick();
            f = otis.runFrame();
        }
        return f;
    }
    uint16_t reg(int page, int r) {
        otis.write(15, static_cast<uint16_t>(page));
        return otis.read(static_cast<uint8_t>(r));
    }
};

}  // namespace

TEST(voices_fc_table_and_clamps) {
    const auto t = makeFcTable();
    CHECK_EQ(t.size(), 768u);
    CHECK_EQ(t[767], 0x7FE);
    CHECK_EQ(fcForPitch(t, 0), 0x400);
    CHECK_EQ(fcForPitch(t, 1536), 0x5A8);
    CHECK_EQ(fcForPitch(t, 3072), 0x800);
    CHECK_EQ(fcForPitch(t, -3072), 0x200);
    CHECK_EQ(fcForPitch(t, -0x4800), 0x10);
    CHECK_EQ(fcForPitch(t, 0x5400), 0xFFFE);
    CHECK_EQ(fcForPitch(t, 0x7FFF), 0xFFFE);
    CHECK_EQ(fcForPitch(t, -0x7FFF), 0x10);  // clamped at the bottom
}

TEST(voices_note_on_programs_firmware_registers) {
    Rig r;
    Wave w = oneZone(0x100, 0x200, 0x300, 0x0B, 1, 5);
    r.voices.setWave(&w);
    r.voices.noteOn(0, 60, 127);
    r.voices.tick();
    CHECK_EQ(r.reg(0, 1), 0x0400);   // FC
    CHECK_EQ(r.reg(0, 10), 0x0002);  // ACC-H  (zone start 0x100 << 9)
    CHECK_EQ(r.reg(0, 11), 0x0000);  // ACC-L
    CHECK_EQ(r.reg(0, 2), 0x0004);   // STRT-H (loop start 0x200 << 9)
    CHECK_EQ(r.reg(0, 3), 0x0000);   // STRT-L
    CHECK_EQ(r.reg(0, 4), 0x0006);   // END-H  (0x300 << 9)
    CHECK_EQ(r.reg(0, 5), 0x00A0);   // END-L  (endFrac 5 << 5)
    // CR reads back with the unused top bits set (O9); mask to the 12 implemented bits.
    // LP4|LP3|CA=2|BS|LPE, STOP bits cleared.
    CHECK_EQ(r.reg(0, 0) & 0x0FFF, 0x0E0C);
    CHECK_EQ(r.reg(0, 7), 0xFFF0);  // K1
    CHECK_EQ(r.reg(0, 6), 0xFFF0);  // K2
    CHECK_EQ(r.voices.activeCount(), 1);
}

TEST(voices_attack_release_reaches_exact_silence) {
    VoiceConfig cfg;
    cfg.adsr.attackMs = 5;
    cfg.adsr.releaseMs = 50;
    Rig r(cfg);
    Wave w = oneZone(0, 0, 16, 0x0B);  // looped
    r.voices.setWave(&w);
    r.voices.noteOn(0, 60, 127);
    OtisFrame f = r.run(frames(10));
    CHECK(f.ch[2][0] != 0);
    CHECK(f.ch[2][1] != 0);
    r.voices.noteOff(0, 60);
    f = r.run(frames(60));
    CHECK_EQ(f.ch[2][0], 0);
    CHECK_EQ(f.ch[2][1], 0);
    CHECK_EQ(r.voices.activeCount(), 0);
}

TEST(voices_oneshot_end_mutes) {
    Rig r;
    Wave w = oneZone(0, 0, 100, 0x03);  // one-shot, 100 words
    r.voices.setWave(&w);
    r.voices.noteOn(0, 60, 127);
    OtisFrame f = r.run(50);
    CHECK(f.ch[2][0] != 0);
    f = r.run(100 + 60 + 4 - 50);
    CHECK_EQ(f.ch[2][0], 0);
    CHECK_EQ(f.ch[2][1], 0);
    CHECK_EQ(r.voices.activeCount(), 0);
}

// FC bit 0 is not implemented by the chip (FC [15:1]), so register read-back is `fc & 0xFFFE`.
TEST(voices_steal_33rd_note_and_release_all) {
    Rig r;
    Wave w = oneZone(0, 0, 16, 0x0B);
    r.voices.setWave(&w);
    const auto t = makeFcTable();
    for (int k = 30; k < 62; ++k) r.voices.noteOn(0, k, 100);
    CHECK_EQ(r.voices.activeCount(), 32);
    CHECK_EQ(r.reg(0, 1), fcForPitch(t, 30 * 256 - 15360) & 0xFFFE);
    r.voices.noteOn(0, 62, 100);  // 33rd: steals the oldest (voice 0, key 30)
    CHECK_EQ(r.voices.activeCount(), 32);
    CHECK_EQ(r.reg(0, 1), fcForPitch(t, 62 * 256 - 15360) & 0xFFFE);
    CHECK(r.reg(0, 1) != (fcForPitch(t, 30 * 256 - 15360) & 0xFFFE));
    for (int k = 30; k <= 62; ++k) r.voices.noteOff(0, k);
    r.run(frames(300));
    CHECK_EQ(r.voices.activeCount(), 0);
}

TEST(voices_sustain_holds_until_pedal_up) {
    Rig r;
    Wave w = oneZone(0, 0, 16, 0x0B);
    r.voices.setWave(&w);
    r.voices.noteOn(0, 60, 100);
    r.voices.sustain(0, true);
    r.voices.noteOff(0, 60);
    r.run(frames(300));
    CHECK_EQ(r.voices.activeCount(), 1);
    r.voices.sustain(0, false);
    r.run(frames(300));
    CHECK_EQ(r.voices.activeCount(), 0);
}

TEST(voices_extreme_keys_and_bend_clamp) {
    const auto t = makeFcTable();
    {
        Rig r;
        Wave w = oneZone(0, 0, 16, 0x0B);
        w.zones[0].pitchWord = 0;
        r.voices.setWave(&w);
        r.voices.pitchBend(0, 16383);
        r.voices.noteOn(0, 127, 100);
        CHECK_EQ(r.reg(0, 1), 0xFFFE);
    }
    {
        Rig r;
        Wave w = oneZone(0, 0, 16, 0x0B);  // pitchWord -15360
        r.voices.setWave(&w);
        r.voices.pitchBend(0, 0);
        r.voices.noteOn(0, 0, 100);
        const uint16_t fc = r.reg(0, 1);
        CHECK(fc >= 0x10);
        CHECK(fc <= 0x40);
        // Bend retunes a sounding voice: centre restores the unbent FC.
        r.voices.pitchBend(0, 8192);
        CHECK_EQ(r.reg(0, 1), fcForPitch(t, -15360) & 0xFFFE);
    }
}
