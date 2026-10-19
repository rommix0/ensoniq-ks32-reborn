#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.h"
#include "esp/esp_isa.h"
#include "trace/trace.h"

using ens::TraceError;
using ens::TraceRunner;
using ens::esp::Instr;
using ens::esp::Op;
using ens::esp::RamOp;

namespace {

// ---- helpers -----------------------------------------------------------------------------

std::string tmpPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / ("ens_trace_test_" + name)).string();
}

void writeWave(const std::string& path, const std::vector<uint16_t>& words) {
    std::ofstream f(path, std::ios::binary);
    for (uint16_t w : words) {
        f.put(static_cast<char>(w & 0xFF));
        f.put(static_cast<char>(w >> 8));
    }
}

std::string fmt(const char* f, unsigned a = 0, unsigned b = 0) {
    char buf[96];
    std::snprintf(buf, sizeof buf, f, a, b);
    return buf;
}

// Trace lines that set up voice `v`: looped, all four poles low-pass with the cutoffs wide open,
// loop start at word 0, loop end END29 = endL (units of 1/512 sample, so 0x2000 = 16 samples),
// CA = channel 0..3, FC 0x400 = one wave sample per frame. lvol/rvol are the 8-bit volumes.
std::string voice(unsigned v, unsigned ca, unsigned endL, unsigned lvol, unsigned rvol) {
    const unsigned cr = 0x0C00 | 0x0008 | (ca << 8);  // LP3 | LP4 | LPE | CA
    std::string s = fmt("otis 15 %04X\n", v);
    s += fmt("otis 0 %04X\n", cr);
    s += "otis 1 0400\notis 2 0000\notis 3 0000\notis 4 0000\n";
    s += fmt("otis 5 %04X\n", endL);
    s += "otis 6 FFF0\notis 7 FFF0\n";
    s += fmt("otis 8 %04X\n", lvol << 8);
    s += fmt("otis 9 %04X\n", rvol << 8);
    s += "otis 10 0000\notis 11 0000\n";
    return s;
}

// OTIS global setup: `act` = last active voice; SERMODE = 0 (A/D off, as the KS-32 firmware
// does) so port 3 is an output and CA 3 carries voices.
std::string otisSetup(unsigned act) {
    return fmt("otis 13 %04X\n", act) + "otis 15 0040\notis 8 0000\n";
}

// Host writes that load ESP program word `idx` while halted (INSTR only, select $C0).
std::string espWord(unsigned idx, const Instr& in) {
    const uint64_t w = in.encode();
    std::string s;
    for (int i = 0; i < 6; ++i)
        s += fmt("esp %02X ", 3 + i) + fmt("%02X\n", static_cast<unsigned>((w >> (40 - 8 * i)) & 0xFF));
    return s + fmt("esp C0 %02X\n", idx);
}

constexpr uint8_t kZero = 255;
Instr mov(uint8_t src, uint8_t dst) {
    return Instr{kZero, kZero, src, dst, Op::MOV, 0, false, true, RamOp::ReadDelay};
}
Instr endWord() { return Instr{kZero, kZero, kZero, kZero, Op::END, 0, false, true, RamOp::ReadDelay}; }
// SER registers: address = 234 + 2*port (R), +1 (L).
constexpr uint8_t kSer0R = 234, kSer0L = 235, kSer1L = 237, kSer2R = 238, kSer3R = 240, kSer3L = 241,
                  kDil = 244;

// Loads `text` (wave images are named by absolute temp paths) and runs it.
bool runText(const std::string& text, std::vector<int16_t>& out, TraceError* errOut = nullptr) {
    TraceRunner r;
    TraceError err;
    const bool ok = r.load(text, "", err);
    if (errOut) *errOut = err;
    if (ok) r.run(out);
    return ok;
}

int16_t at(const std::vector<int16_t>& pcm, size_t frame, int ch) { return pcm[frame * 4 + ch]; }

std::string rampWave(const char* name, int words, int slope) {
    std::vector<uint16_t> w;
    for (int i = 0; i < words; ++i) w.push_back(static_cast<uint16_t>(i * slope));
    const std::string p = tmpPath(name);
    writeWave(p, w);
    return p;
}

}  // namespace

// ---- runner ------------------------------------------------------------------------------

TEST(trace_runs_dc_voice) {
    const std::string p = tmpPath("dc.bin");
    writeWave(p, std::vector<uint16_t>(64, 0x4000));
    const std::string t =
        "wave 0 " + p + "\n" + otisSetup(0) + voice(0, 0, 0x2000, 0xF0, 0xF0) + "frames 8\n";
    std::vector<int16_t> pcm;
    CHECK(runText(t, pcm));
    CHECK_EQ(pcm.size(), 8u * 4);
    CHECK(at(pcm, 0, 0) != 0);  // the filters are still settling in frame 0 (8184)
    for (size_t f = 2; f < 8; ++f) {
        CHECK_EQ(at(pcm, f, 0), 0x1FFE);  // settled: 0x4000 * 31/32 / 2 (volume 0xF0), floor
        CHECK_EQ(at(pcm, f, 1), 0x1FFE);
        CHECK_EQ(at(pcm, f, 2), 0);  // nothing routed to the main out
        CHECK_EQ(at(pcm, f, 3), 0);
    }
}

TEST(trace_esp_passthrough) {
    // The program copies SER0 (OTIS ch1) to SER3 (main out); a CA=1 voice feeds it. Reference
    // run: the same voice on CA=0, whose aux output is that voice's signal.
    // Pinned lag: 0 frames. Within one frame the runner does runFrame -> setSerialIn ->
    // runSample -> serialOut, and the two MOVs (words 0 and 1) are committed by word 2 (END) in
    // that same runSample (E17: ALU(k-1) commits at step k), so the SER3 latch already holds
    // this frame's SER0 value when it is read out. The runner itself adds no delay (unlike the
    // delay-line lag of 4 pinned in esp_delay_line_roundtrip, which is the program's own delay).
    const std::string p = rampWave("ramp.bin", 256, 100);
    const std::string prog = "esphalt 1\n" + espWord(0, mov(kSer0L, kSer3L)) +
                             espWord(1, mov(kSer0R, kSer3R)) + espWord(2, endWord()) + "esphalt 0\n";
    const std::string common = "wave 0 " + p + "\n" + otisSetup(0);
    std::vector<int16_t> viaEsp, ref;
    CHECK(runText(common + voice(0, 1, 0xFFE0, 0xF0, 0xB8) + prog + "frames 40\n", viaEsp));
    CHECK(runText(common + voice(0, 0, 0xFFE0, 0xF0, 0xB8) + "frames 40\n", ref));
    CHECK_EQ(viaEsp.size(), 160u);
    int lag = -1;
    for (int L = 0; L <= 8 && lag < 0; ++L) {
        bool ok = true;
        for (size_t n = 0; n < 40; ++n)
            for (int c = 0; c < 2; ++c)
                ok = ok && at(viaEsp, n, 2 + c) ==
                               (n >= static_cast<size_t>(L) ? at(ref, n - static_cast<size_t>(L), c) : 0);
        if (ok) lag = L;
    }
    CHECK_EQ(lag, 0);
    CHECK(at(viaEsp, 39, 2) > 1000);                // a real, non-trivial signal
    CHECK(at(viaEsp, 39, 2) != at(viaEsp, 38, 2));  // that moves from frame to frame
    CHECK_EQ(at(viaEsp, 39, 0), 0);                 // CA=1 does not reach the aux out
    CHECK(at(ref, 39, 0) != at(ref, 39, 1));        // L and R differ, so a swap would show
}

TEST(trace_routing_ch2_ch3_to_ser1_ser2) {
    // TR1: ch2 -> SER1, ch3 -> SER2. SER1L -> main L, SER2R -> main R. Voice 0 (CA 2) is left
    // only, voice 1 (CA 3) is right only, so a wrong permutation shows up as a missing side.
    const std::string p = rampWave("ramp2.bin", 256, 100);
    const std::string prog = "esphalt 1\n" + espWord(0, mov(kSer1L, kSer3L)) +
                             espWord(1, mov(kSer2R, kSer3R)) + espWord(2, endWord()) + "esphalt 0\n";
    std::vector<int16_t> pcm;
    CHECK(runText("wave 0 " + p + "\n" + otisSetup(1) + voice(0, 2, 0xFFE0, 0xF0, 0) +
                      voice(1, 3, 0xFFE0, 0, 0xF0) + prog + "frames 40\n",
                  pcm));
    CHECK(at(pcm, 39, 2) > 1000);
    CHECK(at(pcm, 39, 3) > 1000);
    CHECK_EQ(at(pcm, 39, 0), 0);
    CHECK_EQ(at(pcm, 39, 1), 0);
}

TEST(trace_halted_esp_outputs_nothing_and_resumes) {
    const std::string p = rampWave("ramp3.bin", 256, 100);
    const std::string prog = "esphalt 1\n" + espWord(0, mov(kSer0L, kSer3L)) + espWord(1, endWord());
    std::vector<int16_t> pcm;
    CHECK(runText("wave 0 " + p + "\n" + otisSetup(0) + voice(0, 1, 0xFFE0, 0xF0, 0xF0) + prog +
                      "frames 6\nesphalt 0\nframes 6\n",
                  pcm));
    for (size_t f = 0; f < 6; ++f) CHECK_EQ(at(pcm, f, 2), 0);  // halted: runSample does nothing
    CHECK(at(pcm, 11, 2) != 0);                                  // resumed at pc 0
}

TEST(trace_dram_masks_addresses_and_io_reads_zero) {
    // Host writes 0x1111 at DADR 0x000050 and 0x2222 at DADR 0x010050, then reads 0x000050 back
    // into DIL; word 0 of the program copies DIL to SER3L. The two addresses differ in bit 16, so
    // they alias (the second write wins) for dram 16 and for the default 64K words, and stay
    // distinct for 128K words. I/O space (ram control bit 6 = 0) reads 0.
    auto host = [](const char* ramCtlRead, const char* dramSize) {
        std::string s = std::string(dramSize) + "esphalt 1\nesp 14 40\n";
        s += "esp 0C 11\nesp 0D 11\nesp 10 00\nesp 11 50\nesp 0F 00\n";
        s += "esp 0C 22\nesp 0D 22\nesp 10 00\nesp 11 50\nesp 0F 01\n";
        s += std::string("esp 14 ") + ramCtlRead + "\nesp 10 00\nesp 11 50\nesp 0F 00\n";
        s += espWord(0, mov(kDil, kSer3L)) + espWord(1, endWord()) + "esphalt 0\nframes 1\n";
        return s;
    };
    std::vector<int16_t> pcm;
    CHECK(runText(host("C0", "dram 16\n"), pcm));
    CHECK_EQ(at(pcm, 0, 2), 0x2222);  // aliased: the second write overwrote the first
    CHECK(runText(host("C0", ""), pcm));
    CHECK_EQ(at(pcm, 0, 2), 0x2222);  // default 64K words (MEMSIZ 0xFF): bit 16 is masked off
    CHECK(runText(host("C0", "dram 131072\n"), pcm));
    CHECK_EQ(at(pcm, 0, 2), 0x1111);  // 128K words: distinct
    CHECK(runText(host("80", ""), pcm));
    CHECK_EQ(at(pcm, 0, 2), 0);  // I/O space reads 0
    // A 24-bit word address into the smallest DRAM must not read or write out of bounds.
    CHECK(runText("dram 1\nesphalt 1\nesp 14 40\nesp 0C 12\nesp 0D 34\nesp 0F FF\nesp 10 FF\nesp 11 F0\n"
                  "esp 14 C0\nesp 0F FF\nesphalt 0\nframes 2\n",
                  pcm));
    CHECK_EQ(pcm.size(), 8u);
}

TEST(trace_espidle_finishes_ram_clear) {
    // R15. dram 256; word 0x50 holds 0x1111. MEMSIZ = 0xFF, DLENGTH = 0x00FF00, then host
    // control 03 / 02 starts a RAM clear that zeroes addresses 0xFF..0. Each `esp` write while
    // halted adds one idle step; the clear reaches address 0x50 after 0xFF-0x50+1 = 176 steps.
    auto gprWrite = [](unsigned addr, const char* b0, const char* b1, const char* b2) {
        return std::string("esp 00 ") + b0 + "\nesp 01 " + b1 + "\nesp 02 " + b2 + "\n" +
               fmt("esp A0 %02X\n", addr);
    };
    auto trace = [&](const char* idle) {
        std::string s =
            "dram 256\nesphalt 1\nesp 14 40\nesp 0C 11\nesp 0D 11\nesp 10 00\nesp 11 50\nesp 0F 00\n";
        s += gprWrite(kDil, "00", "00", "FF");  // address 244 on writes is MEMSIZ
        s += gprWrite(245, "00", "FF", "00");   // DLENGTH
        s += "esp 12 03\nesp 12 02\n";
        s += idle;
        s += "esp 14 C0\nesp 10 50\nesp 11 00\nesp 0F 00\n";  // read word 0x50 (MEMSIZ=0xFF: DADR>>8)
        s += espWord(0, mov(kDil, kSer3L)) + espWord(1, endWord()) + "esphalt 0\nframes 1\n";
        return s;
    };
    std::vector<int16_t> pcm;
    CHECK(runText(trace("espidle 400\n"), pcm));
    CHECK_EQ(at(pcm, 0, 2), 0);  // cleared
    CHECK(runText(trace("espidle 10\n"), pcm));
    CHECK_EQ(at(pcm, 0, 2), 0x1111);  // clear not finished yet: the word survived
}

TEST(trace_wave_reads_past_the_image_are_zero) {
    // A 4-word image; the voice runs far past it. Beyond the image the wave is 0, so the output
    // falls back to 0 once the filters have drained.
    const std::string p = tmpPath("short.bin");
    writeWave(p, std::vector<uint16_t>(4, 0x4000));
    std::vector<int16_t> pcm;
    CHECK(runText("wave 0 " + p + "\n" + otisSetup(0) + voice(0, 0, 0xFFE0, 0xF0, 0xF0) + "frames 200\n",
                  pcm));
    CHECK(at(pcm, 2, 0) > 1000);
    CHECK_EQ(at(pcm, 199, 0), 0);
}

TEST(trace_run_is_repeatable) {
    const std::string p = rampWave("ramp4.bin", 256, 100);
    TraceRunner r;
    TraceError err;
    CHECK(r.load("wave 0 " + p + "\n" + otisSetup(0) + voice(0, 0, 0xFFE0, 0xF0, 0xF0) + "frames 30\n", "",
                 err));
    std::vector<int16_t> a, b;
    r.run(a);
    r.run(b);
    CHECK_EQ(a.size(), 120u);
    CHECK(a == b);
}

TEST(trace_smoke_file) {
    std::ifstream f(std::string(ENS_TEST_DATA_DIR) + "/smoke.trace", std::ios::binary);
    CHECK(f.good());
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    TraceRunner r;
    TraceError err;
    CHECK(r.load(text, ENS_TEST_DATA_DIR, err));
    std::vector<int16_t> pcm;
    r.run(pcm);
    CHECK_EQ(pcm.size(), 64u * 4);
    bool aux = false, mainOut = false;
    for (size_t i = 0; i < 64; ++i) {
        aux = aux || at(pcm, i, 0) != 0;
        mainOut = mainOut || at(pcm, i, 2) != 0;
    }
    CHECK(aux);
    CHECK(mainOut);
}

// ---- parser and loader errors ------------------------------------------------------------

TEST(trace_parse_error_reports_line) {
    TraceError err;
    std::vector<int16_t> out;
    CHECK(!runText("otis 3 zz\n", out, &err));
    CHECK_EQ(err.line, 1);
    CHECK(!err.msg.empty());

    CHECK(!runText("# comment\n\n   \nframes 1\nbogus 1 2\n", out, &err));
    CHECK_EQ(err.line, 5);

    // Missing wave file: reported with the line of its `wave` command.
    CHECK(!runText("# header\nwave 1 /nonexistent_dir_ens/nope.bin\nframes 1\n", out, &err));
    CHECK_EQ(err.line, 2);
    CHECK(err.msg.find("nope.bin") != std::string::npos);
}

TEST(trace_parse_rejects_bad_commands) {
    struct Case {
        const char* text;
        int line;
    };
    const Case cases[] = {
        {"otis 16 0000\n", 1},       // register out of range
        {"otis 1 10000\n", 1},       // value does not fit 16 bits
        {"otis 1\n", 1},             // missing value
        {"otis 1 0000 extra\n", 1},  // extra token
        {"otis -1 0000\n", 1},       // sign
        {"esp 100 00\n", 1},         // address does not fit 8 bits
        {"esp 10 1FF\n", 1},         // value does not fit 8 bits
        {"esphalt 2\n", 1},
        {"espidle abc\n", 1},
        {"espidle 16777217\n", 1},  // over the per-command limit
        {"wave 2 x.bin\n", 1},      // bank
        {"wave 0\n", 1},            // no path
        {"dram 1000\n", 1},         // not a power of two
        {"dram 0\n", 1},
        {"dram 33554432\n", 1},  // above 24-bit addressing
        {"frames\n", 1},
        {"frames -3\n", 1},
        {"frames 0x10\n", 1},      // frames is decimal
        {"frames 33554433\n", 1},  // total frame limit
        {"frames 30000000\nframes 30000000\n", 2},
        {"frames 1\ndram 256\n", 2},  // dram after frames
        {"frames 1\nwave 0 x.bin\n", 2},
        {"FRAMES 1\n", 1},  // commands are lower case
        {"otis 1 0000 # no inline comments\n", 1},
    };
    for (const Case& c : cases) {
        std::vector<ens::TraceCmd> cmds;
        TraceError err;
        const bool ok = ens::parseTrace(c.text, "", cmds, err);
        if (ok || err.line != c.line) std::printf("  case failed: %s", c.text);
        CHECK(!ok);
        CHECK_EQ(err.line, c.line);
    }
}

TEST(trace_parse_accepts_format_variants) {
    // CRLF line ends, tabs, comments, blank lines, 0x prefixes, lower/upper hex, no final newline,
    // a wave path with spaces, relative paths joined with the base directory.
    const std::string t =
        "# a comment\r\n\r\n\twave\t1   my dir/w 1.bin  \r\notis 15 0x00AB\r\nesp a0 Ff\r\n"
        "esphalt 1\r\nespidle 3\r\ndram 4096\r\nframes 2\r\nframes 3";
    std::vector<ens::TraceCmd> c;
    TraceError err;
    CHECK(ens::parseTrace(t, "base", c, err));
    CHECK_EQ(c.size(), 8u);
    CHECK(c[0].op == ens::TraceOp::Wave);
    CHECK_EQ(c[0].a, 1u);
    CHECK(c[0].path == "base/my dir/w 1.bin");
    CHECK_EQ(c[0].line, 3);
    CHECK(c[1].op == ens::TraceOp::OtisWrite);
    CHECK_EQ(c[1].a, 15u);
    CHECK_EQ(c[1].b, 0xABu);
    CHECK(c[2].op == ens::TraceOp::EspWrite);
    CHECK_EQ(c[2].a, 0xA0u);
    CHECK_EQ(c[2].b, 0xFFu);
    CHECK(c[3].op == ens::TraceOp::EspHalt);
    CHECK(c[4].op == ens::TraceOp::EspIdle);
    CHECK_EQ(c[4].a, 3u);
    CHECK_EQ(c[5].a, 4096u);
    CHECK_EQ(c[7].a, 3u);
    CHECK_EQ(c[7].line, 10);

    // Absolute paths are kept as they are.
    c.clear();
    CHECK(ens::parseTrace("wave 0 /abs/w.bin\nwave 0 C:/abs/w.bin\n", "base", c, err));
    CHECK(c[0].path == "/abs/w.bin");
    CHECK(c[1].path == "C:/abs/w.bin");
}

TEST(trace_wave_file_errors_report_their_line) {
    std::vector<int16_t> out;
    TraceError err;
    const std::string odd = tmpPath("odd.bin");
    {
        std::ofstream f(odd, std::ios::binary);
        f << "abc";
    }
    CHECK(!runText("\n\nwave 0 " + odd + "\n", out, &err));
    CHECK_EQ(err.line, 3);
    CHECK(err.msg.find("odd") != std::string::npos);

    const std::string big = tmpPath("big.bin");
    writeWave(big, std::vector<uint16_t>((1u << 20) + 1, 1));
    CHECK(!runText("wave 1 " + big + "\n", out, &err));
    CHECK_EQ(err.line, 1);
    writeWave(big, std::vector<uint16_t>(1u << 20, 1));  // exactly 1M words is fine
    CHECK(runText("wave 1 " + big + "\n", out));
    CHECK_EQ(out.size(), 0u);
}

TEST(trace_garbage_never_crashes) {
    // Deterministic pseudo-random text built from format tokens and raw bytes: parse and load
    // must return (true or false) without crashing.
    uint32_t x = 12345;
    auto rnd = [&] {
        x = x * 1664525u + 1013904223u;
        return x >> 8;
    };
    const char* words[] = {"otis", "esp", "wave", "dram", "frames", "esphalt", "espidle", "#",
                           "0x",   "zz",  "-1",   "99999999999999999999", "\n", "\r\n", "\t", " "};
    for (int iter = 0; iter < 300; ++iter) {
        std::string s;
        for (int k = 0; k < 40; ++k) {
            if (rnd() % 4 == 0) s += static_cast<char>(rnd() & 0xFF);
            else s += words[rnd() % (sizeof words / sizeof words[0])];
            if (rnd() % 2) s += ' ';
        }
        std::vector<ens::TraceCmd> cmds;
        TraceError err;
        const bool ok = ens::parseTrace(s, "", cmds, err);
        CHECK(ok || err.line >= 1);
    }
    // Truncated mid-command and empty inputs.
    std::vector<int16_t> out;
    TraceError err;
    CHECK(runText("", out, &err));
    CHECK_EQ(out.size(), 0u);
    CHECK(!runText("otis 15", out, &err));
    CHECK_EQ(err.line, 1);
    CHECK(runText("frames 0\n", out));
}

// ---- pcm compare -------------------------------------------------------------------------

TEST(pcm_diff_identical) {
    std::vector<int16_t> a;
    for (int i = 0; i < 40; ++i) a.push_back(static_cast<int16_t>(i * 1000 - 20000));
    const ens::PcmCompare r = ens::pcmCompare(a, a, 4);
    CHECK(r.identical);
    CHECK(!r.lengthDiffers);
    CHECK_EQ(r.framesA, 10u);
    CHECK_EQ(r.framesB, 10u);
    CHECK_EQ(r.firstDiffFrame, -1);
    CHECK_EQ(r.maxAbs.size(), 4u);
    for (size_t c = 0; c < 4; ++c) {
        CHECK_EQ(r.maxAbs[c], 0);
        CHECK(r.rms[c] == 0.0);
    }
}

TEST(pcm_diff_reports_differences) {
    std::vector<int16_t> a(40, 100), b(40, 100);
    b[4 * 3 + 2] = 90;      // frame 3, channel 2: diff 10
    b[4 * 7 + 2] = 130;     // frame 7, channel 2: diff 30
    b[4 * 9 + 0] = -32768;  // frame 9, channel 0: diff 32868 (does not fit int16)
    const ens::PcmCompare r = ens::pcmCompare(a, b, 4);
    CHECK(!r.identical);
    CHECK_EQ(r.firstDiffFrame, 3);
    CHECK_EQ(r.maxAbs[0], 32868);
    CHECK_EQ(r.maxAbs[1], 0);
    CHECK_EQ(r.maxAbs[2], 30);
    CHECK_EQ(r.maxAbs[3], 0);
    CHECK(std::fabs(r.rms[2] - 10.0) < 1e-9);  // sqrt((10^2 + 30^2) / 10 frames)
    // Different lengths: compared over the common part, flagged as different.
    std::vector<int16_t> c(a.begin(), a.begin() + 36);
    const ens::PcmCompare r2 = ens::pcmCompare(a, c, 4);
    CHECK(!r2.identical);
    CHECK(r2.lengthDiffers);
    CHECK_EQ(r2.framesA, 10u);
    CHECK_EQ(r2.framesB, 9u);
    CHECK_EQ(r2.firstDiffFrame, -1);
    // Another channel count: sample 14 is the first difference, frame 7 with 2 channels.
    const ens::PcmCompare r3 = ens::pcmCompare(a, b, 2);
    CHECK_EQ(r3.maxAbs.size(), 2u);
    CHECK_EQ(r3.framesA, 20u);
    CHECK_EQ(r3.firstDiffFrame, 7);
}
