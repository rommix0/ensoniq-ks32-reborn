// Effect-style ESP programs (Plan 1 Task 13): a Dattorro/Griesinger-style reverb and the linear
// interpolation kernel from the ESP2 document (Part III §7 and §1.2), re-expressed with ES5510
// opcodes and its pipeline (E17/E18: an ALU result reaches the MUL two words later, a RAM read
// lands in DIL two words after the issuing word, the MAC preload slot is the word before the
// accumulate). The reverb program is also the program of tests/data/reverb_tank.trace.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "esp/esp.h"
#include "esp/esp_isa.h"
#include "trace/trace.h"

using ens::Esp;
using ens::EspBus;
using ens::esp::Instr;
using ens::esp::Op;
using ens::esp::RamOp;

namespace {

constexpr uint8_t kZero = 255;  // constant 0: a source of 0 and a discarding destination
constexpr uint8_t kSer0L = 235, kSer3R = 240, kSer3L = 241, kMach = 243, kDil = 244;
constexpr uint8_t kMemsiz = 244, kDlength = 245, kDbase = 248, kSigreg = 249;
constexpr uint32_t kShiftOne = 0x400000;  // SIGREG bit 22 (KS-32 firmware setting)
// KS-32 frame rate: OTIS 15.238 MHz / 512 (ESP2 document Fs = 29761.894531 Hz).
constexpr double kFrameRate = 29761.9;

// 64K-word DRAM (MEMSIZ 0x0000FF); addresses are masked to the size.
struct Dram : EspBus {
    std::vector<uint16_t> ram = std::vector<uint16_t>(0x10000, 0);
    uint16_t read(bool io, uint32_t a) override { return io ? 0 : ram[a & 0xFFFF]; }
    void write(bool io, uint32_t a, uint16_t v) override {
        if (!io) ram[a & 0xFFFF] = v;
    }
};

// ---- reverb tank -----------------------------------------------------------------------
// Topology (ESP2 Part III §7, Figure R, reduced): the four input diffusers in series (lattice
// allpasses, lengths 142/107/379/277, coefficients 0.75/0.75/0.625/0.625 as DIFFUS1/DIFFUS2),
// then one recirculating delay of 4453 samples (the tank's longest line) with feedback gain
// 0.7: t = u + 0.7 * t[n - 4453]. SER3L = t (wet), SER3R = u (diffused, no feedback).
//
// Each allpass is the ESP2 two-line lattice  w = x - g*d (written to the line), y = d + g*w,
// coded with the MAC preload ("MOV d > MACP" in ESP2) as three ES5510 words:
//   a  : MOV x > MACH                 MUL 0 x 0 (clears the accumulator)
//   a+1: MOV DIL(d) > MACH            MUL DIL(d) x -g +MAC > W, DOL   RAM write line[0]
//   a+2:                              MUL W(w) x g +MAC > W           RAM read next line[D]
// The read of d is issued two words before a+1 (by the previous block's a+2 or by word 0).
struct Ap {
    uint32_t base, len;  // line[0] (write) and line[len] (read) offsets, in samples
    double coef;
    uint8_t w, g, ng;  // output register, +coef, -coef
};
constexpr Ap kAp[4] = {{0, 142, 0.75, 0x80, 0x88, 0x89},
                       {143, 107, 0.75, 0x81, 0x8A, 0x8B},
                       {251, 379, 0.625, 0x82, 0x8C, 0x8D},
                       {631, 277, 0.625, 0x83, 0x8E, 0x8F}};
constexpr uint32_t kTankBase = 909, kTankLen = 4453;
constexpr uint8_t kTank = 0x84, kFeedback = 0x90;
constexpr int kReverbWords = 18;

uint32_t q23(double v) {  // coefficient in Q23, 24-bit two's complement
    return static_cast<uint32_t>(static_cast<int32_t>(std::lround(v * 8388608.0))) & 0xFFFFFF;
}

struct Program {
    std::vector<uint64_t> words;
    std::vector<std::pair<int, uint32_t>> gprs;  // (address, value) host writes, in order
};

uint64_t word(uint8_t d, uint8_t c, uint8_t b, uint8_t a, Op op, uint8_t opsel, bool mac,
              RamOp ram) {
    return Instr{d, c, b, a, op, opsel, false, mac, ram}.encode();
}

Program reverbProgram() {
    Program p;
    p.words.assign(kReverbWords, 0);
    // gpr[pc] is the RAM offset of word pc: samples in bits 23..8 (MEMSIZ 0xFF).
    auto off = [&p](int pc, uint32_t samples) {
        p.gprs.push_back({pc, (samples << 8) & 0xFFFFFF});
    };
    // w0: NOP; read line 0 at its tap.
    p.words[0] = word(kZero, kZero, kZero, kZero, Op::MOV, 0, true, RamOp::ReadDelay);
    off(0, kAp[0].base + kAp[0].len);
    for (int k = 0; k < 4; ++k) {
        const Ap& ap = kAp[k];
        const int a = 3 * k + 1;
        const uint8_t x = (k == 0) ? kSer0L : kAp[k - 1].w;
        p.words[a] = word(kZero, kZero, x, kMach, Op::MOV, 0, false, RamOp::ReadDelay);
        // opsel 4: MUL X input = DIL, result to C and to the DOL; ALU result to A.
        p.words[a + 1] = word(ap.ng, ap.w, kDil, kMach, Op::MOV, 4, true, RamOp::WriteDelay);
        off(a + 1, ap.base);
        p.words[a + 2] = word(ap.g, ap.w, kZero, kZero, Op::MOV, 0, true, RamOp::ReadDelay);
        off(a + 2, k < 3 ? kAp[k + 1].base + kAp[k + 1].len : kTankBase + kTankLen);
    }
    // Tank: t = u + 0.7 * tank[4453]; write t at tank[0].
    p.words[13] = word(kZero, kZero, kAp[3].w, kMach, Op::MOV, 0, false, RamOp::ReadDelay);
    p.words[14] = word(kFeedback, kTank, kZero, kZero, Op::MOV, 4, true, RamOp::WriteDelay);
    off(14, kTankBase);
    p.words[15] = word(kZero, kZero, kTank, kSer3L, Op::MOV, 0, true, RamOp::ReadDelay);
    p.words[16] = word(kZero, kZero, kAp[3].w, kSer3R, Op::MOV, 0, true, RamOp::ReadDelay);
    p.words[17] = word(kZero, kZero, kZero, kZero, Op::END, 0, true, RamOp::ReadDelay);

    for (const Ap& ap : kAp) {
        p.gprs.push_back({ap.g, q23(ap.coef)});
        p.gprs.push_back({ap.ng, q23(-ap.coef)});
    }
    p.gprs.push_back({kFeedback, q23(0.7)});
    p.gprs.push_back({kSigreg, kShiftOne});
    p.gprs.push_back({kMemsiz, 0x0000FF});
    p.gprs.push_back({kDlength, 0xFFFF00});  // the whole 64K-word DRAM is the delay ring
    p.gprs.push_back({kDbase, 0x000000});
    return p;
}

void load(Esp& e, const Program& p) {
    for (const auto& g : p.gprs) e.setGpr(g.first, g.second);
    for (size_t i = 0; i < p.words.size(); ++i) e.setInstr(static_cast<int>(i), p.words[i]);
}

// −60 dBFS decay time of a mono 16-bit signal: the end of the last 0.1 s window (windows of
// round(0.1 * rate) samples from the start) whose RMS is at or above −60 dBFS (32768 * 1e-3).
// Returns tenths of a second; 0 means the first window is already below.
int decayTenths(const std::vector<int16_t>& s, double rate) {
    const size_t win = static_cast<size_t>(std::lround(0.1 * rate));
    const double thr = 32768.0 * 1e-3;
    int last = -1;
    for (size_t i = 0; (i + 1) * win <= s.size(); ++i) {
        double e = 0;
        for (size_t n = i * win; n < (i + 1) * win; ++n) e += double(s[n]) * double(s[n]);
        if (std::sqrt(e / double(win)) >= thr) last = static_cast<int>(i);
    }
    return last + 1;
}

}  // namespace

TEST(esp_allpass_reverb_decays) {
    Dram bus;
    Esp e(bus);
    const Program p = reverbProgram();
    load(e, p);
    const int frames = static_cast<int>(6.0 * kFrameRate);
    std::vector<int16_t> wet(frames), dry(frames);
    int32_t peak = 0;
    for (int n = 0; n < frames; ++n) {
        e.setSerialIn(0, n == 0 ? 0x7FFF : 0, 0);  // one full-scale sample on SER0L, then 0
        CHECK_EQ(e.runSample(), kReverbWords);
        e.serialOut(3, wet[n], dry[n]);
        const int32_t a = wet[n] < 0 ? -wet[n] : wet[n];
        if (a > peak) peak = a;
    }
    // The tank produces output (the first diffuser tap reaches SER3L through the chain).
    CHECK(peak > 1000);
    // The diffusers alone (no feedback) die out quickly; the tank lengthens the tail.
    const int dryT = decayTenths(dry, kFrameRate);
    const int wetT = decayTenths(wet, kFrameRate);
    // Guard against MAME bug #1 (tails that ring for tens of seconds): well under 3 s.
    CHECK(wetT < 30);
    // Pinned measurements (0.1 s resolution) of this core on this program.
    CHECK_EQ(wetT, 13);
    CHECK_EQ(dryT, 2);
    if (wetT != 13 || dryT != 2) {
        std::printf("  -60 dBFS: wet %d.%d s, diffusers only %d.%d s, wet peak %d\n", wetT / 10,
                    wetT % 10, dryT / 10, dryT % 10, static_cast<int>(peak));
    }
}

// Linear interpolation between delay taps m and m+1 (ESP2 Part III §1.2):
//   out = frac_u * D[m] + frac * D[m+1],  frac = fraction bits of the tap offset, frac_u = 1-frac.
// On the ES5510 the tap offset register holds m in bits 23..8 (MEMSIZ 0xFF) and the fraction in
// bits 7..0, which the AGEN ignores; LS15 moves those 8 bits to a positive Q23 coefficient
// (E14) and DIFF forms 0x7FFFFF - frac (E2).
TEST(esp_chorus_interp_lsb) {
    Dram bus;
    Esp e(bus);
    constexpr uint8_t kOff = 0x80, kStep = 0x81, kFrac = 0x82, kFracU = 0x83, kOut = 0x84;
    e.setGpr(kSigreg, kShiftOne);
    e.setGpr(kMemsiz, 0x0000FF);
    e.setGpr(kDlength, 0xFFFF00);
    e.setGpr(kStep, 0x000100);  // one sample
    e.setInstr(0, word(kZero, kZero, kOff, 4, Op::MOV, 0, true, RamOp::ReadDelay));   // g4 = m.f
    e.setInstr(1, word(kZero, kZero, kOff, 5, Op::MOV, 0, true, RamOp::ReadDelay));   // g5 = m.f
    e.setInstr(2, word(kZero, kZero, kStep, 5, Op::ADD, 0, true, RamOp::ReadDelay));  // g5 += 1
    e.setInstr(3, word(kZero, kZero, kOff, kFrac, Op::LS15, 0, true, RamOp::ReadDelay));
    e.setInstr(4, word(kZero, kZero, kFrac, kFracU, Op::DIFF, 0, true, RamOp::ReadDelay));  // D[m]
    e.setInstr(5, word(kZero, kZero, kZero, kZero, Op::MOV, 0, true, RamOp::ReadDelay));  // D[m+1]
    // opsel 3: MUL X input = DIL, result to C.
    e.setInstr(6, word(kFracU, kOut, kZero, kZero, Op::MOV, 3, false, RamOp::ReadDelay));
    e.setInstr(7, word(kFrac, kOut, kZero, kZero, Op::MOV, 3, true, RamOp::ReadDelay));
    e.setInstr(8, word(kZero, kZero, kOut, kSer3L, Op::MOV, 0, true, RamOp::ReadDelay));
    e.setInstr(9, word(kZero, kZero, kZero, kZero, Op::END, 0, true, RamOp::ReadDelay));

    constexpr uint32_t kM = 100;
    constexpr int16_t kA = -0x4000, kB = 0x4000;  // taps m and m+1 (m+2 = B as well)
    std::vector<int32_t> out;
    for (uint32_t f = 0; f <= 256; ++f) {
        const uint32_t mf = (kM << 8) + f;  // f = 256 is the next whole tap (m+1, frac 0)
        e.setGpr(kOff, mf);
        // Place D[m], D[m+1], D[m+2] where this sample's delay addressing looks for them.
        const ens::esp::Agen& g = e.agen();
        bus.ram[g.physical(RamOp::ReadDelay, kM << 8) & 0xFFFF] = static_cast<uint16_t>(kA);
        bus.ram[g.physical(RamOp::ReadDelay, (kM + 1) << 8) & 0xFFFF] = static_cast<uint16_t>(kB);
        bus.ram[g.physical(RamOp::ReadDelay, (kM + 2) << 8) & 0xFFFF] = static_cast<uint16_t>(kB);
        CHECK_EQ(e.runSample(), 10);
        CHECK_EQ(e.gpr(kFrac), (f & 0xFF) << 15);
        const uint32_t v = e.gpr(kOut);
        out.push_back(static_cast<int32_t>(v << 8) >> 8);
    }
    const int32_t a24 = kA * 256, b24 = kB * 256;
    // Endpoints: frac 0 gives D[m] (frac_u = 1 - 2^-23 costs at most one 24-bit LSB).
    CHECK(out[0] <= a24 && out[0] >= a24 - 1);
    CHECK(out[256] <= b24 && out[256] >= b24 - 1);
    // Strictly increasing, in equal steps of (B - A) / 256 within one LSB of rounding.
    const int32_t step = (b24 - a24) / 256;
    bool monotonic = true, even = true;
    for (size_t i = 1; i < out.size(); ++i) {
        monotonic = monotonic && out[i] > out[i - 1];
        const int32_t d = out[i] - out[i - 1];
        even = even && d >= step - 1 && d <= step + 1;
    }
    CHECK(monotonic);
    CHECK(even);
    int16_t l = 0, r = 0;
    e.serialOut(3, l, r);
    CHECK_EQ(l, static_cast<int16_t>(out[256] >> 8));
}

// tests/data/reverb_tank.trace loads exactly the program above through host writes (so the
// MAME comparison in docs/verification-plan1.md runs the program this file measures).
TEST(reverb_tank_trace_matches_program) {
    const std::string dir = ENS_TEST_DATA_DIR;
    std::ifstream f(dir + "/reverb_tank.trace", std::ios::binary);
    CHECK(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    std::vector<ens::TraceCmd> cmds;
    ens::TraceError err;
    CHECK(ens::parseTrace(ss.str(), dir, cmds, err));

    // Replay the host latches: $00-$02 GPR data, $03-$08 INSTR data, $A0/$C0/$E0 selects.
    uint32_t gl = 0;
    uint64_t il = 0;
    std::vector<std::pair<int, uint32_t>> gprs;
    std::vector<uint64_t> words(Esp::kProgramSize, 0);
    int maxWord = -1;
    for (const auto& c : cmds) {
        if (c.op != ens::TraceOp::EspWrite) continue;
        const uint32_t a = c.a, v = c.b & 0xFF;
        if (a <= 2) gl = (gl & ~(0xFFu << (8 * (2 - a)))) | (v << (8 * (2 - a)));
        if (a >= 3 && a <= 8) {
            const int sh = 8 * (8 - static_cast<int>(a));
            il = (il & ~(uint64_t(0xFF) << sh)) | (uint64_t(v) << sh);
        }
        if (a == 0xA0 || a == 0xE0) gprs.push_back({static_cast<int>(v), gl});
        if ((a == 0xC0 || a == 0xE0) && v < Esp::kProgramSize) {
            words[v] = il;
            if (static_cast<int>(v) > maxWord) maxWord = v;
        }
    }
    const Program p = reverbProgram();
    CHECK_EQ(maxWord + 1, kReverbWords);
    for (int i = 0; i < kReverbWords; ++i) CHECK_EQ(words[i], p.words[i]);
    CHECK_EQ(gprs.size(), p.gprs.size());
    for (size_t i = 0; i < gprs.size() && i < p.gprs.size(); ++i) {
        CHECK_EQ(gprs[i].first, p.gprs[i].first);
        CHECK_EQ(gprs[i].second, p.gprs[i].second);
    }
}
