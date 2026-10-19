#include <cstdint>
#include <functional>
#include <utility>

#include "check.h"
#include "otis/otis.h"

using namespace ens;

namespace {

struct FnWave : WaveMemory {
    std::function<int16_t(uint32_t)> fn;
    int reads = 0;
    explicit FnWave(std::function<int16_t(uint32_t)> f) : fn(std::move(f)) {}
    uint16_t read(int, uint32_t addr) override {
        ++reads;
        return static_cast<uint16_t>(fn(addr));
    }
};

FnWave constWave(int16_t v) {
    return FnWave([v](uint32_t) { return v; });
}

constexpr uint16_t STOP1 = 0x0002, LP3 = 0x0400, LP4 = 0x0800;

void writeAcc(Otis& o, uint32_t pos29) {
    o.write(10, static_cast<uint16_t>(pos29 >> 16));
    o.write(11, static_cast<uint16_t>(pos29 & 0xFFFF));
}

// A stopped voice (address frozen at `accSamples`) with the given filter/volume setup.
void setVoice(Otis& o, int voice, uint16_t cr, uint16_t k1, uint16_t k2, uint16_t lvol,
              uint16_t rvol, uint32_t accSamples = 0) {
    o.write(15, static_cast<uint16_t>(voice));
    o.write(0, static_cast<uint16_t>(cr | STOP1));
    o.write(7, k1);
    o.write(6, k2);
    o.write(8, static_cast<uint16_t>(lvol << 8));  // volume registers are left-justified
    o.write(9, static_cast<uint16_t>(rvol << 8));
    writeAcc(o, accSamples << 9);
}

uint16_t filterReg(Otis& o, int voice, int reg) {
    o.write(15, static_cast<uint16_t>(32 + voice));
    return o.read(static_cast<uint8_t>(reg));
}

// All four poles low-pass with the cutoff registers wide open.
constexpr uint16_t kLpAll = LP3 | LP4;
constexpr uint16_t kOpen = 0xFFF0;  // K12 = 4095

}  // namespace

TEST(otis_interp_values) {
    CHECK_EQ(otis_interp(0, 512, 256), 256);
    CHECK_EQ(otis_interp(100, -100, 511), -100);
    CHECK_EQ(otis_interp(-1, 0, 1), -1);  // floor, not truncation
    CHECK_EQ(otis_interp(7, 9, 0), 7);
    CHECK_EQ(otis_interp(-32768, 32767, 511), 32639);  // 65535*511 >> 9 = 65407
}

TEST(otis_lp_values) {
    CHECK_EQ(otis_lp(1000, 0, 4095), 999);
    CHECK_EQ(otis_lp(1000, 0, 0), 0);
    CHECK_EQ(otis_lp(-32768, 32767, 4095), -32753);
}

TEST(otis_hp_values) {
    CHECK_EQ(otis_hp(1000, 0, 0, 0), 1000);
    CHECK_EQ(otis_hp(0, 0, 1000, 0), 500);
    CHECK_EQ(otis_hp(0, 0, 1000, 4095), 999);
    // Pole outputs saturate to 16 bits instead of wrapping.
    CHECK_EQ(otis_hp(32767, -32768, 0, 0), 32767);
    CHECK_EQ(otis_hp(-32768, 32767, 0, 0), -32768);
}

TEST(otis_vol_table) {
    CHECK_EQ(otis_vol(0x4000, 0xF0), 0x4000 * 16 >> 5);
    for (int x = -32768; x <= 32767; x += 257) CHECK_EQ(otis_vol(static_cast<int16_t>(x), 0x00), 0);
    for (int m = 0; m < 16; ++m) CHECK_EQ(otis_vol(12345, static_cast<uint8_t>(m)), 0);  // exp 0 mutes
    CHECK_EQ(otis_vol(-32768, 0x10), (-32768 * 16 >> 5) >> 14);
    CHECK_EQ(otis_vol(-32768, 0x10), -1);  // sign fill
    CHECK_EQ(otis_vol(1024, 0xFF), (1024 * 31) >> 5);
    CHECK_EQ(otis_vol(1024, 0xFF), 992);
    CHECK_EQ(otis_vol(-32768, 0xFF), -31744);  // -32768*31 >> 5, passed through unshifted
}

namespace {

// Run one voice with a DC input and the given filter mode / K2; return O3 and O4 once settled.
std::pair<int16_t, int16_t> runMode(uint16_t lpBits, uint16_t k2) {
    FnWave m = constWave(0x2000);
    Otis o(m);
    o.write(13, 0);
    setVoice(o, 0, lpBits, kOpen, k2, 0xF0, 0xF0);
    for (int i = 0; i < 300; ++i) o.runFrame();
    return {static_cast<int16_t>(filterReg(o, 0, 2)), static_cast<int16_t>(filterReg(o, 0, 1))};
}

}  // namespace

// DC step of 0x2000 through one voice, K1 = 4095/4096, after 300 frames. Exact settled (O3, O4):
// floor rounding leaves LP poles 1-3 LSB under DC and strands HP poles at small negatives (O4).
TEST(otis_filter_mode_table) {
    struct Row {
        uint16_t lp;
        uint16_t k2;
        int o3, o4;
    };
    const Row rows[] = {
        // K2 = 0: LP/K2 poles block everything (K = 0); HP/K2 poles (K = 0.5) kill DC.
        {0,         0x0000, 0, -1},      // 00: pole 3 HP/K2, pole 4 HP/K2
        {LP3,       0x0000, 8189, 0},    // 01: pole 3 LP/K1 passes DC, pole 4 HP/K2
        {LP4,       0x0000, 0, 0},       // 10: pole 3 LP/K2 (K = 0), pole 4 LP/K2 (K = 0)
        {LP3 | LP4, 0x0000, 8189, 0},    // 11: pole 3 LP/K1 passes DC, pole 4 LP/K2 blocks
        // K2 = 0x8000 (K12 = 2048): LP K = 0.5, HP K = 0.75.
        {0,         0x8000, 0, -3},
        {LP3,       0x8000, 8189, 0},
        {LP4,       0x8000, 8189, 8188},  // 10: pole 3 is LP/K2 (not HP): both pass DC
        {LP3 | LP4, 0x8000, 8189, 8188},
    };
    for (const Row& r : rows) {
        const auto got = runMode(r.lp, r.k2);
        CHECK_EQ(got.first, r.o3);
        CHECK_EQ(got.second, r.o4);
    }
}

TEST(otis_filter_matches_reference_chain_all_modes) {
    // Independent reference of the cascade built from the pure helpers: wiring, X(n-1) choice
    // for the HP poles and the state-register update order must agree for every mode.
    auto wave = [](uint32_t a) {
        uint32_t h = a * 2654435761u;
        return static_cast<int16_t>((h >> 11) & 0xFFFF);
    };
    const uint16_t k1 = 0xB230, k2 = 0x5A70;
    const uint16_t modes[4] = {0, LP3, LP4, LP3 | LP4};
    for (uint16_t mode : modes) {
        FnWave m(wave);
        Otis o(m);
        o.write(13, 0);
        setVoice(o, 0, mode, k1, k2, 0xF0, 0xF0);
        int16_t r1 = 0, r2 = 0, r2p = 0, r3 = 0, r3p = 0, r4 = 0;
        for (uint32_t i = 0; i < 80; ++i) {
            const uint32_t pos = ((i * 7 + 3) << 9) | ((i * 53) & 0x1FF);
            o.write(15, 0);
            writeAcc(o, pos);
            o.runFrame();

            const int16_t sf = otis_interp(wave(pos >> 9), wave((pos >> 9) + 1), pos & 0x1FF);
            const uint16_t K1 = k1 >> 4, K2 = k2 >> 4;
            const int16_t y1 = otis_lp(sf, r1, K1);
            const int16_t y2 = otis_lp(y1, r2, K1);
            int16_t y3, y4;
            if (mode & LP3) y3 = otis_lp(y2, r3, K1);
            else if (mode & LP4) y3 = otis_lp(y2, r3, K2);
            else y3 = otis_hp(y2, r2, r3, K2);
            if (mode & LP4) y4 = otis_lp(y3, r4, K2);
            else y4 = otis_hp(y3, r3, r4, K2);
            r1 = y1;
            r2p = r2;
            r2 = y2;
            r3p = r3;
            r3 = y3;
            r4 = y4;

            CHECK_EQ(filterReg(o, 0, 6), static_cast<uint16_t>(r1));
            CHECK_EQ(filterReg(o, 0, 4), static_cast<uint16_t>(r2));
            CHECK_EQ(filterReg(o, 0, 5), static_cast<uint16_t>(r2p));
            CHECK_EQ(filterReg(o, 0, 2), static_cast<uint16_t>(r3));
            CHECK_EQ(filterReg(o, 0, 3), static_cast<uint16_t>(r3p));
            CHECK_EQ(filterReg(o, 0, 1), static_cast<uint16_t>(r4));
        }
    }
}

TEST(otis_channel_saturation) {
    for (int sign = 0; sign < 2; ++sign) {
        const int16_t in = sign ? -0x7000 : 0x7000;
        FnWave m = constWave(in);
        Otis o(m);
        o.write(13, 3);
        for (int v = 0; v < 4; ++v) setVoice(o, v, kLpAll | 0x0100, kOpen, kOpen, 0xF0, 0xF0);
        OtisFrame f{};
        for (int i = 0; i < 20; ++i) f = o.runFrame();
        CHECK_EQ(f.ch[1][0], sign ? -32768 : 32767);
        CHECK_EQ(f.ch[1][1], sign ? -32768 : 32767);
        CHECK_EQ(f.ch[0][0], 0);
        CHECK_EQ(f.ch[0][1], 0);
        CHECK_EQ(f.ch[2][0], 0);
        CHECK_EQ(f.ch[3][1], 0);
    }
    // Two voices do not saturate: the frame is exactly the sum of the two voice outputs.
    FnWave m = constWave(0x7000);
    Otis o(m);
    o.write(13, 1);
    for (int v = 0; v < 2; ++v) setVoice(o, v, kLpAll | 0x0200, kOpen, kOpen, 0xF0, 0x00);
    OtisFrame f{};
    for (int i = 0; i < 20; ++i) f = o.runFrame();
    const int one = (static_cast<int16_t>(filterReg(o, 0, 1)) * 16) >> 5;
    CHECK_EQ(f.ch[2][0], 2 * one);
    CHECK_EQ(f.ch[2][0], 0x6FFC);
    CHECK_EQ(f.ch[2][1], 0);  // RVOL exponent 0 mutes
}

TEST(otis_channel_sums_are_cleared_each_frame) {
    FnWave m = constWave(0x1000);
    Otis o(m);
    o.write(13, 0);
    setVoice(o, 0, kLpAll, kOpen, kOpen, 0xF0, 0xF0);
    OtisFrame a{}, b{};
    for (int i = 0; i < 10; ++i) a = o.runFrame();
    b = o.runFrame();
    CHECK_EQ(a.ch[0][0], b.ch[0][0]);  // settled, so not growing frame over frame
    CHECK_EQ(b.ch[0][0], 0x7FE);
}

TEST(otis_stopped_voice_still_outputs) {
    FnWave m = constWave(0x2000);
    Otis o(m);
    o.write(13, 0);
    setVoice(o, 0, kLpAll, kOpen, kOpen, 0xF0, 0xF0);  // setVoice sets STOP1
    OtisFrame f{};
    for (int i = 0; i < 20; ++i) f = o.runFrame();
    CHECK_EQ(f.ch[0][0], 0xFFE);
    CHECK_EQ(f.ch[0][1], 0xFFE);
}

TEST(otis_stopped_voice_o1_tracks_acc) {
    FnWave m([](uint32_t a) { return static_cast<int16_t>(a < 327 ? a * 100 : 0); });
    Otis o(m);
    o.write(13, 7);
    setVoice(o, 5, 0, kOpen, 0, 0, 0, 100);
    for (int i = 0; i < 8; ++i) o.runFrame();
    CHECK_EQ(filterReg(o, 5, 6), 9999);  // within 1 LSB of sample 100 (floor leaves it 1 under)
    // The host moves the accumulator of the stopped voice; reg 6 follows it.
    o.write(15, 5);
    writeAcc(o, 200u << 9);
    for (int i = 0; i < 8; ++i) o.runFrame();
    CHECK_EQ(filterReg(o, 5, 6), 19999);
    CHECK_EQ(o.acc29(5), 200u << 9);  // and the address itself never moved
}

TEST(otis_interpolation_in_signal_path) {
    // S1 = 0 at address 10, S2 = 512 at address 11; accumulator halfway -> SF = 256.
    FnWave m([](uint32_t a) { return static_cast<int16_t>(a == 11 ? 512 : 0); });
    Otis o(m);
    o.write(13, 0);
    setVoice(o, 0, kLpAll, kOpen, kOpen, 0, 0);
    writeAcc(o, (10u << 9) | 256u);
    o.runFrame();
    // Pole 1 with K = 4095/4096: (4095 * 256) >> 12 = 255.
    CHECK_EQ(filterReg(o, 0, 6), 255);
}

TEST(otis_midframe_write_affects_later_voices_only) {
    FnWave m = constWave(0x2000);
    Otis o(m);
    o.write(13, 7);
    for (int v = 0; v < 8; ++v) setVoice(o, v, kLpAll, kOpen, kOpen, 0xF0, 0xF0);
    OtisFrame f0{};
    for (int i = 0; i < 20; ++i) f0 = o.runFrame();
    CHECK_EQ(f0.ch[0][0], 8 * 0xFFE);
    CHECK_EQ(f0.ch[0][0] % 8, 0);
    const int c = f0.ch[0][0] / 8;  // identical settled contribution per voice

    for (int i = 0; i < 4; ++i) o.stepVoice();  // slots 0..3 done
    for (int v : {2, 6}) {
        o.write(15, static_cast<uint16_t>(v));
        o.write(8, 0);
    }
    while (!o.frameReady()) o.stepVoice();
    const OtisFrame f1 = o.takeFrame();
    CHECK_EQ(f1.ch[0][0], f0.ch[0][0] - c);      // voice 6 gone, voice 2 still counted
    CHECK_EQ(f1.ch[0][1], f0.ch[0][1]);           // RVOL was not touched
    const OtisFrame f2 = o.runFrame();
    CHECK_EQ(f2.ch[0][0], f0.ch[0][0] - 2 * c);   // next frame: both gone
}

TEST(otis_act_below_8_still_produces_frames) {
    // DECISION O13: ACT < 7 is allowed; the frame simply has fewer voices.
    FnWave m = constWave(0x2000);
    Otis o(m);
    o.write(13, 2);  // 3 voices
    for (int v = 0; v < 3; ++v) setVoice(o, v, kLpAll, kOpen, kOpen, 0xF0, 0xF0);
    OtisFrame f{};
    for (int i = 0; i < 20; ++i) f = o.runFrame();
    m.reads = 0;
    f = o.runFrame();
    CHECK_EQ(m.reads, 6);  // 3 slots x (S1, S2)
    CHECK_EQ(f.ch[0][0], 3 * 0xFFE);
    CHECK_EQ(f.ch[0][0] % 3, 0);
}

TEST(otis_stale_frame_is_dropped_at_next_frame_start) {
    FnWave m = constWave(0x1000);
    Otis o(m);
    o.write(13, 1);
    o.stepVoice();
    o.stepVoice();
    CHECK(o.frameReady());  // never taken...
    o.stepVoice();          // ...slot 0 of the next frame clears it
    CHECK(!o.frameReady());
    o.stepVoice();
    CHECK(o.frameReady());
    // runFrame() after an untaken frame runs a whole new frame.
    m.reads = 0;
    o.runFrame();
    CHECK_EQ(m.reads, 4);
}

TEST(otis_serial_test_mode_latches_channel_regs) {
    FnWave m = constWave(0x2000);
    Otis o(m);
    o.write(13, 0);
    setVoice(o, 0, kLpAll, kOpen, kOpen, 0xF0, 0xF0);
    o.write(15, 64);
    o.write(8, 0x0002);  // TEST=1, MSB = 0
    o.write(2, 0x1234);  // CH1L
    o.write(3, 0x7FFF);  // CH1R
    OtisFrame f{};
    for (int i = 0; i < 20; ++i) f = o.runFrame();
    CHECK_EQ(f.ch[1][0], 0x1234);
    CHECK_EQ(f.ch[1][1], 0x7FFF);
    CHECK_EQ(f.ch[0][0], 0);  // accumulation is off: voice 0 does not reach CH0
    o.write(8, 0x0802);       // MSB = 00001; the next write forms 0x10000: overflow, +saturate
    o.write(2, 0x0000);
    f = o.runFrame();
    CHECK_EQ(f.ch[1][0], 32767);
    o.write(8, 0xF802);       // MSB = 11111 with data 0x8001: in range after sign extension
    o.write(3, 0x8001);
    f = o.runFrame();
    CHECK_EQ(f.ch[1][1], -32767);
    o.write(8, 0x8002);       // MSB = 10000: negative overflow
    o.write(2, 0x0000);
    f = o.runFrame();
    CHECK_EQ(f.ch[1][0], -32768);
}

TEST(otis_ch3_reads_back_latched_output) {
    FnWave m = constWave(0x2000);
    Otis o(m);
    o.write(13, 0);
    o.write(15, 64);
    o.write(8, 0x0000);  // A/D = 0: port 3 is an output
    setVoice(o, 0, kLpAll | 0x0300, kOpen, kOpen, 0xF0, 0xF0);
    OtisFrame f{};
    for (int i = 0; i < 20; ++i) f = o.runFrame();
    o.write(15, 64);
    CHECK_EQ(o.read(6), static_cast<uint16_t>(f.ch[3][0]));
    CHECK_EQ(o.read(7), static_cast<uint16_t>(f.ch[3][1]));
    CHECK_EQ(f.ch[3][0], 0xFFE);
}
