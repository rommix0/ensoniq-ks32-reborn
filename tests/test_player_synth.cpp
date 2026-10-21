#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "ks32/wave_roms.h"
#include "player/effects.h"
#include "player/sample_map.h"
#include "player/synth.h"

using namespace ens::player;

namespace {

constexpr int kRate = 29762;  // frames per second (rounded chip rate)

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool readBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool hasLine(const std::vector<std::string>& log, const char* needle) {
    for (const std::string& s : log)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

// A looped one-zone wave: root key 60 plays one sample per frame, 16-word loop at word 0.
Wave loopedWave(int set, int number) {
    Wave w;
    w.set = set;
    w.number = number;
    Zone z;
    z.topKey = 127;
    z.pitchWord = -15360;
    z.bank = 0;
    z.start = 0;
    z.loopStart = 0;
    z.end = 16;
    z.crLow = 0x0B;
    w.zones.push_back(z);
    return w;
}

SampleMap syntheticMap() {
    SampleMap m;
    m.waves.push_back(loopedWave(0, 0));
    m.waves.push_back(loopedWave(0, 1));
    return m;
}

std::vector<int16_t> renderFrames(Synth& s, int frames) {
    std::vector<int16_t> buf(static_cast<size_t>(frames) * 2);
    s.render(buf.data(), frames);
    return buf;
}

}  // namespace

TEST(synth_velocity0_is_note_off) {
    // No effect library: the load is logged and skipped; the voice manager is what is under test.
    Synth s(std::vector<uint16_t>(256, 0x4000), {}, syntheticMap(), EffectLibrary(), makeFcTable(), SynthConfig());
    CHECK(hasLine(s.takeLog(), "effect 1"));
    s.midi(0x90, 60, 100);
    CHECK_EQ(s.activeVoices(), 1);
    renderFrames(s, kRate / 100);
    CHECK_EQ(s.activeVoices(), 1);
    s.midi(0x90, 60, 0);
    renderFrames(s, kRate * 4 / 10);
    CHECK_EQ(s.activeVoices(), 0);
}

TEST(synth_program_change_missing_wave_ignored) {
    Synth s(std::vector<uint16_t>(256, 0x4000), {}, syntheticMap(), EffectLibrary(), makeFcTable(), SynthConfig());
    s.takeLog();
    s.midi(0xC0, 126, 0);
    CHECK(hasLine(s.takeLog(), "no zones"));
    s.midi(0x90, 60, 100);
    CHECK_EQ(s.activeVoices(), 1);  // the previous wave is still selected

    // Bank select applies at the next program change; a present wave is logged by its set:number.
    s.midi(0xB0, 0, 1);
    s.midi(0xC0, 0, 0);
    CHECK(hasLine(s.takeLog(), "wave 1:0 has no zones"));
    s.midi(0xB0, 0, 0);
    s.midi(0xC0, 1, 0);
    CHECK(hasLine(s.takeLog(), "wave 0:1"));
}

TEST(synth_controllers_and_messages) {
    Synth s(std::vector<uint16_t>(256, 0x4000), {}, syntheticMap(), EffectLibrary(), makeFcTable(), SynthConfig());
    s.takeLog();
    s.midi(0xB0, 102, 11);  // out of range: ignored and logged
    CHECK(hasLine(s.takeLog(), "ignored"));
    s.midi(0xB0, 103, 3);   // bus out of range: ignored
    s.midi(0xB0, 103, 2);
    s.midi(0xB0, 64, 127);  // pedal down
    s.midi(0x90, 60, 100);
    s.midi(0x80, 60, 0);
    renderFrames(s, kRate / 2);
    CHECK_EQ(s.activeVoices(), 1);  // held by the pedal
    s.midi(0xB0, 64, 0);
    renderFrames(s, kRate / 2);
    CHECK_EQ(s.activeVoices(), 0);
    s.midi(0x91, 62, 100);
    s.midi(0xE1, 0x00, 0x40);  // pitch bend, centre
    s.midi(0xB1, 7, 100);
    s.midi(0xD1, 10, 0);       // channel pressure: ignored
    s.midi(0xF8, 0, 0);        // system message: ignored
    CHECK_EQ(s.activeVoices(), 1);
    s.midi(0xB1, 123, 0);      // all notes off
    renderFrames(s, kRate / 2);
    CHECK_EQ(s.activeVoices(), 0);
    s.midi(0x90, 60, 100);
    s.midi(0x92, 64, 100);
    s.midi(0xB2, 120, 0);      // all sound off
    renderFrames(s, kRate / 100);
    CHECK_EQ(s.activeVoices(), 0);
}

TEST(synth_render_deterministic) {
    std::string err;
    EffectLibrary fx;
    CHECK(parseEffects(readFile(ENS_DATA_DIR "/esp/esp_programs.json"), readFile(ENS_DATA_DIR "/esp/effect_defaults.json"),
                       fx, err));
    std::vector<uint16_t> bank(4096);
    uint32_t seed = 12345;
    for (uint16_t& w : bank) {
        seed = seed * 1664525u + 1013904223u;
        w = static_cast<uint16_t>(seed >> 16);
    }
    SampleMap map;
    Wave w = loopedWave(0, 0);
    w.zones[0].end = 1000;
    map.waves.push_back(w);

    auto run = [&]() {
        Synth s(bank, {}, map, fx, makeFcTable(), SynthConfig());
        std::vector<int16_t> all;
        auto chunk = [&](int frames) {
            const std::vector<int16_t> b = renderFrames(s, frames);
            all.insert(all.end(), b.begin(), b.end());
        };
        s.midi(0x90, 60, 100);
        chunk(40000);
        s.midi(0xE0, 0x00, 0x60);
        s.midi(0x90, 67, 90);
        s.midi(0xB0, 102, 3);
        chunk(30000);
        s.midi(0x80, 60, 0);
        s.midi(0xB0, 123, 0);
        chunk(20000);
        return all;
    };
    const std::vector<int16_t> a = run(), b = run();
    CHECK(a == b);
    int peak = 0;
    for (int16_t v : a) peak = std::max(peak, std::abs(static_cast<int>(v)));
    CHECK(peak > 0);  // not vacuously equal
}

TEST(synth_rom_note_sounds_and_decays) {
    const std::string dir = ENS_ROM_DIR;
    std::vector<uint8_t> u25, u26, rom2, rom3;
    if (!readBytes(dir + "/sq1-u25.bin", u25) || !readBytes(dir + "/sq1-u26.bin", u26) ||
        !readBytes(dir + "/rom2.u39", rom2) || !readBytes(dir + "/rom3.u38", rom3)) {
        std::printf("  skipped: ROMs not found in %s\n", dir.c_str());
        return;
    }
    std::vector<uint16_t> b0, b1;
    std::string err;
    CHECK(ens::ks32::buildWaveBanks(u25, u26, rom2, rom3, b0, b1, err));
    SampleMap map;
    EffectLibrary fx;
    CHECK(parseSampleMap(readFile(ENS_DATA_DIR "/sample_map.json"), map, err));
    CHECK(parseEffects(readFile(ENS_DATA_DIR "/esp/esp_programs.json"), readFile(ENS_DATA_DIR "/esp/effect_defaults.json"),
                       fx, err));
    Synth s(std::move(b0), std::move(b1), std::move(map), std::move(fx), makeFcTable(), SynthConfig());

    // The ESP boot takes about 718 frames and the effect load about 1405 more (output fades in
    // during the load), so 1.5 s (44643 frames) is rendered before the note: the first 0.1 s alone
    // would be boot only.
    renderFrames(s, kRate * 3 / 2);
    s.midi(0x90, 60, 100);
    const std::vector<int16_t> on = renderFrames(s, kRate / 2);
    int peak = 0;
    for (int16_t v : on) peak = std::max(peak, std::abs(static_cast<int>(v)));
    CHECK(peak > 1000);

    s.midi(0x90, 60, 0);
    const std::vector<int16_t> tail = renderFrames(s, kRate * 4);
    CHECK_EQ(s.activeVoices(), 0);
    // The effect tails settle on a small DC (floor truncation in the ESP filters), so the last
    // 0.1 s is judged by its AC RMS (about the window mean) and the mean separately.
    const size_t n = static_cast<size_t>(kRate / 10);
    double worstRms = 0, worstDc = 0;
    for (size_t c = 0; c < 2; ++c) {
        double mean = 0, sq = 0;
        for (size_t i = 0; i < n; ++i) mean += tail[(tail.size() / 2 - n + i) * 2 + c];
        mean /= static_cast<double>(n);
        for (size_t i = 0; i < n; ++i) {
            const double d = tail[(tail.size() / 2 - n + i) * 2 + c] - mean;
            sq += d * d;
        }
        worstRms = std::max(worstRms, std::sqrt(sq / static_cast<double>(n)));
        worstDc = std::max(worstDc, std::fabs(mean));
    }
    std::printf("  ROM note: peak %d, final AC RMS %.3f, DC %.3f\n", peak, worstRms, worstDc);
    CHECK(worstRms <= 4.0);
    CHECK(worstDc <= 32.0);
}
