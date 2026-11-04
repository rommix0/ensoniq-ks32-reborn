// The JUCE-free engine (Plan 7, Task 2): the machine through the resampler at the host rate, MIDI
// with block offsets (A3), GUI events (A4), the panel snapshot (A5, A6) and the RAM hand-off.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/spsc_ring.h"
#include "check.h"
#include "ks32_firmware.h"
#include "player/resampler.h"

using namespace ens;
using ens::app::Engine;
using ens::app::Event;

namespace {

constexpr int kBlock = 480;  // 10 ms at 48 000 Hz

std::unique_ptr<Engine> makeEngine() {
    ks32::RomSet roms;
    if (!readKs32Roms(roms)) {
        std::printf("  skip: ROMs not found\n");
        return nullptr;
    }
    std::string err;
    auto e = Engine::create(roms, err);
    if (!e) {
        std::printf("  FAIL: ROMs present but rejected: %s\n", err.c_str());
        ++ens_test::failures();
        return nullptr;
    }
    e->prepare(48000, kBlock);
    return e;
}

// Renders ms of 10 ms blocks; the output is appended to out (left channel) when given.
void renderMs(Engine& e, int ms, std::vector<float>* out = nullptr) {
    float l[kBlock], r[kBlock];
    for (int b = 0; b < ms / 10; ++b) {
        e.render(l, r, kBlock);
        if (out) out->insert(out->end(), l, l + kBlock);
    }
}

std::unique_ptr<Engine> bootedEngine() {
    auto e = makeEngine();
    if (e) renderMs(*e, 3300);
    return e;
}

std::string shown(const ks32::PanelState& p) { return rowText(p, 0) + rowText(p, 1); }

int firstNonZero(const std::vector<float>& v) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] != 0.0f) return static_cast<int>(i);
    return -1;
}

Event button(ks32::Button b, bool down) {
    return {Event::Kind::Button, static_cast<uint8_t>(b), down, 0};
}

}  // namespace

TEST(spsc_ring_fifo_and_full) {
    app::SpscRing<int, 4> q;
    for (int i = 0; i < 4; ++i) CHECK(q.push(i));
    CHECK(!q.push(4));
    for (int i = 0; i < 4; ++i) {
        int v = -1;
        CHECK(q.pop(v));
        CHECK_EQ(v, i);
    }
    int v;
    CHECK(!q.pop(v));
}

TEST(engine_create_rejects_wrong_rom_size) {
    ks32::RomSet roms;
    if (!readKs32Roms(roms)) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    roms.osUpper.pop_back();
    std::string err;
    CHECK(Engine::create(roms, err) == nullptr);
    CHECK(err.find("OS upper") != std::string::npos);
}

TEST(engine_matches_machine_and_resampler) {
    auto e = makeEngine();
    if (!e) return;
    auto m = loadKs32();
    if (!m) return;
    player::Resampler rs(ks32::kWavRate, 48000, 2);
    std::vector<float> ref;
    ref.reserve(2 * 48100);
    while (ref.size() < 2 * 48000) {
        int16_t s[2];
        m->runFrame(s[0], s[1]);
        rs.process(s, 1, ref);
    }
    float l[kBlock], r[kBlock];
    int bad = 0;
    for (int f = 0; f < 48000; f += kBlock) {
        e->render(l, r, kBlock);
        for (int i = 0; i < kBlock; ++i)
            if (l[i] != ref[2 * (f + i)] || r[i] != ref[2 * (f + i) + 1]) ++bad;
    }
    CHECK_EQ(bad, 0);
}

TEST(engine_render_larger_than_prepared_block) {
    auto a = makeEngine();
    if (!a) return;
    auto b = makeEngine();
    std::vector<float> al(2000), ar(2000), bl(2000), br(2000);
    int bad = 0;
    for (int n = 0; n < 50; ++n) {  // 2 s, past the start of the boot's sound
        a->render(al.data(), ar.data(), 2000);
        for (int k = 0; k < 4; ++k) b->render(bl.data() + 500 * k, br.data() + 500 * k, 500);
        if (al != bl || ar != br) ++bad;
    }
    CHECK_EQ(bad, 0);
}

TEST(engine_boot_snapshot) {
    auto e = makeEngine();
    if (!e) return;
    renderMs(*e, 3000);
    CHECK(!e->snapshot().booted);
    renderMs(*e, 300);
    const app::Snapshot s = e->snapshot();
    CHECK(s.booted);
    std::printf("  display: [%s]\n", shown(s.panel).c_str());
    CHECK(rowText(s.panel, 0) == "ROM   00        ");
    CHECK(rowText(s.panel, 1) == " KS Grand Piano ");
    CHECK(s.panel.leds[8] == ks32::Led::On);  // Select Sound
}

// DECISION A3: a byte at offset 479 enters about 479 output frames later than one at offset 0.
TEST(engine_midi_offset_places_note) {
    auto a = bootedEngine();
    if (!a) return;
    auto b = bootedEngine();
    const uint8_t on[] = {0x90, 0x3C, 0x64};
    a->midi(0, on, 3);
    b->midi(479, on, 3);
    std::vector<float> oa, ob;
    renderMs(*a, 500, &oa);
    renderMs(*b, 500, &ob);
    const int fa = firstNonZero(oa), fb = firstNonZero(ob);
    std::printf("  first sound: %d and %d\n", fa, fb);
    CHECK(fa >= 0 && fb >= 0);
    CHECK(fb - fa >= 400 && fb - fa <= 560);
}

TEST(engine_chord_reaches_version_screen) {
    auto e = bootedEngine();
    if (!e) return;
    const Event steps[] = {
        button(ks32::Button::EditSeqPreset, true), button(ks32::Button::EditSeqPreset, false),
        button(ks32::Button::Bank9, true),         button(ks32::Button::Screen1, true),
        button(ks32::Button::Screen1, false),      button(ks32::Button::Bank9, false),
    };
    for (const Event& ev : steps) {
        CHECK(e->post(ev));
        renderMs(*e, 100);
    }
    const std::string s = shown(e->snapshot().panel);
    std::printf("  display: [%s]\n", s.c_str());
    CHECK(s.find("3.01") != std::string::npos);
}

// DECISION A3: the block's MIDI store keeps 1024 bytes; ruling R4: a sysex the note-on ends.
TEST(engine_midi_overflow_is_counted) {
    auto e = bootedEngine();
    if (!e) return;
    std::vector<uint8_t> flood(3000, 0x00);
    flood[0] = 0xF0;
    flood[1] = 0x7D;
    e->midi(0, flood.data(), static_cast<int>(flood.size()));
    renderMs(*e, 10);
    const uint32_t dropped = e->snapshot().midiDropped;
    std::printf("  dropped: %u\n", static_cast<unsigned>(dropped));
    CHECK(dropped >= 1976u);
    std::vector<float> quiet;
    renderMs(*e, 200, &quiet);
    CHECK(firstNonZero(quiet) < 0);
    const uint8_t on[] = {0x90, 0x3C, 0x64};
    e->midi(0, on, 3);
    std::vector<float> out;
    renderMs(*e, 1000, &out);
    CHECK(firstNonZero(out) >= 0);
}

// A full event queue at once: the panel link takes only whole events, and the rest wait.
TEST(engine_posts_never_split_a_panel_event) {
    auto e = bootedEngine();
    if (!e) return;
    for (int i = 0; i < 256; ++i) CHECK(e->post({Event::Kind::Key, 60, i % 2 == 0, 100}));
    CHECK(!e->post({Event::Kind::Key, 60, false, 100}));
    renderMs(*e, 1000);
    CHECK_EQ(e->machine().panelDropped(), 0u);
    CHECK_EQ(e->machine().duart().rxDropped(ks32::kPanelChannel), 0u);
    CHECK(e->snapshot().midiOutBytes > 0);  // the keys played: the firmware sends them on MIDI out
}

TEST(engine_ram_handoff) {
    auto e = makeEngine();
    if (!e) return;
    renderMs(*e, 100);
    std::vector<uint8_t> v;
    CHECK(!e->takeRam(v));
    e->requestRam();
    renderMs(*e, 10);
    CHECK(e->takeRam(v));
    CHECK(v == e->machine().ramImage());
    CHECK(!e->takeRam(v));
}

// ---- Plan 8, Task 2: the current sound, the requested sound, analogs, the note mask (A15, A16, A19).

namespace {
void press(Engine& e, ks32::Button b) {  // 100 ms held, 300 ms apart (as Task 1's runs)
    e.post(button(b, true));
    renderMs(e, 100);
    e.post(button(b, false));
    renderMs(e, 200);
}
void midiBytes(Engine& e, std::initializer_list<uint8_t> v) {
    const std::vector<uint8_t> b(v);
    e.midi(0, b.data(), static_cast<int>(b.size()));
}
// setProgram before every block, for ms.
void renderWanting(Engine& e, int n, int ms) {
    for (int i = 0; i < ms / 10; ++i) {
        e.setProgram(n);
        renderMs(e, 10);
    }
}
std::vector<uint8_t> selectOf(const app::Snapshot& s) { return {s.select.begin(), s.select.begin() + s.selectLen}; }
}  // namespace

TEST(engine_tracks_boot_as_program_zero) {
    auto e = makeEngine();
    if (!e) return;
    renderMs(*e, 3000);
    CHECK_EQ(e->snapshot().program, -1);
    renderMs(*e, 300);
    const app::Snapshot s = e->snapshot();
    CHECK_EQ(s.program, 0);
    CHECK_EQ(s.programsSent, 0u);
    CHECK_EQ(s.selectLen, 0);
}

TEST(engine_tracks_panel_pick) {
    auto e = bootedEngine();
    if (!e) return;
    press(*e, ks32::Button::UpYes);
    press(*e, ks32::Button::UpYes);
    renderMs(*e, 200);
    const app::Snapshot s = e->snapshot();
    CHECK_EQ(s.program, 2);
    CHECK(selectOf(s) == (std::vector<uint8_t>{0xC0, 0x02}));
}

TEST(engine_tracks_midi_in_program) {
    auto e = bootedEngine();
    if (!e) return;
    midiBytes(*e, {0xC0, 0x05});
    renderMs(*e, 500);
    const app::Snapshot s = e->snapshot();
    CHECK_EQ(s.program, 5);
    CHECK(shown(s.panel).find("ROM   05") != std::string::npos);
    CHECK_EQ(s.programsSent, 0u);
}

TEST(engine_restores_program_after_warm_boot) {
    auto a = bootedEngine();
    if (!a) return;
    midiBytes(*a, {0xC0, 0x37});
    renderMs(*a, 500);
    const std::vector<uint8_t> ram = a->machine().ramImage();
    auto b = makeEngine();
    std::string err;
    CHECK(b->loadRam(ram, err));
    renderWanting(*b, 55, 3300 + 500);
    const app::Snapshot s = b->snapshot();
    CHECK(rowText(s.panel, 0).find("ROM   55") != std::string::npos);
    CHECK_EQ(s.program, 55);
    CHECK_EQ(s.programsSent, 1u);
}

TEST(engine_panel_pick_is_not_undone) {
    auto e = bootedEngine();
    if (!e) return;
    renderWanting(*e, 0, 100);
    press(*e, ks32::Button::UpYes);
    CHECK_EQ(e->snapshot().program, 1);
    renderWanting(*e, 0, 500);
    CHECK_EQ(e->snapshot().program, 1);
    CHECK_EQ(e->snapshot().programsSent, 0u);
    renderWanting(*e, 1, 100);
    CHECK_EQ(e->snapshot().programsSent, 0u);
    renderWanting(*e, 7, 500);
    const app::Snapshot s = e->snapshot();
    CHECK_EQ(s.programsSent, 1u);
    CHECK(rowText(s.panel, 0).find("ROM   07") != std::string::npos);
    CHECK_EQ(s.program, 7);
}

// Task 1's kinds that come back: the selection's tracked bytes, replayed with setSelect after a warm boot.
// Plays the panel steps on a fresh engine, takes the tracked select and the RAM, and replays on another.
static void roundTrip(const std::vector<ks32::Button>& steps, const std::vector<uint8_t>& expectSelect,
                      const char* shownRow0, int program) {
    auto a = bootedEngine();
    if (!a) return;
    for (const auto b : steps) press(*a, b);
    renderMs(*a, 300);
    const app::Snapshot sa = a->snapshot();
    std::printf("  tracked:");
    for (const uint8_t b : selectOf(sa)) std::printf(" %02X", b);
    std::printf("  display: [%s]\n", shown(sa.panel).c_str());
    CHECK(selectOf(sa) == expectSelect);
    CHECK(rowText(sa.panel, 0).find(shownRow0) != std::string::npos);
    CHECK_EQ(sa.program, program);
    const std::vector<uint8_t> ram = a->machine().ramImage();
    auto b = makeEngine();
    std::string err;
    CHECK(b->loadRam(ram, err));
    b->setSelect(sa.select.data(), sa.selectLen);
    renderWanting(*b, program, 3300 + 500);
    const app::Snapshot sb = b->snapshot();
    std::printf("  restored: [%s]\n", shown(sb.panel).c_str());
    CHECK(rowText(sb.panel, 0).find(shownRow0) != std::string::npos);
    CHECK_EQ(sb.program, program);
    CHECK_EQ(sb.programsSent, 1u);
    CHECK(selectOf(sb) == expectSelect);
}

TEST(engine_restores_task1_kinds) {
    // INT 34: BankSet, Bank 3, Screen 4.
    roundTrip({ks32::Button::BankSet, ks32::Button::Bank3, ks32::Button::Screen4},
              {0xB0, 0x00, 0x00, 0x20, 0x00, 0xC0, 0x22}, "INT   34", 34);
    // A ROM sound picked after BankSet and back to ROM: the bank LSB 1 comes before the C0.
    roundTrip({ks32::Button::BankSet, ks32::Button::BankSet, ks32::Button::Bank5, ks32::Button::Screen5},
              {0xB0, 0x00, 0x00, 0x20, 0x01, 0xC0, 0x37}, "ROM   55", 55);
}

TEST(engine_analog_waits_for_boot) {
    auto e = makeEngine();
    if (!e) return;
    for (int i = 0; i < 100; ++i) {
        e->setAnalog(ks32::Analog::PitchWheel, 0x100);
        renderMs(*e, 10);
    }
    CHECK_EQ(e->machine().analog(ks32::Analog::PitchWheel), 0x200);
    for (int i = 0; i < 230; ++i) {
        e->setAnalog(ks32::Analog::PitchWheel, 0x100);
        renderMs(*e, 10);
    }
    CHECK_EQ(e->machine().analog(ks32::Analog::PitchWheel), 0x100);
}

TEST(engine_note_mask) {
    auto e = bootedEngine();
    if (!e) return;
    auto notes = [&] { renderMs(*e, 10); return e->snapshot().notes; };
    midiBytes(*e, {0x90, 0x3C, 0x64});
    CHECK_EQ(notes()[0], uint64_t{1} << 60);
    midiBytes(*e, {0x94, 0x40, 0x64});
    CHECK_EQ(notes()[1], uint64_t{1});
    midiBytes(*e, {0x80, 0x3C, 0x00});
    CHECK_EQ(notes()[0], uint64_t{0});
    midiBytes(*e, {0x94, 0x40, 0x00});
    CHECK_EQ(notes()[1], uint64_t{0});
    midiBytes(*e, {0x90, 0x3C, 0x64});
    renderMs(*e, 10);
    midiBytes(*e, {0xB0, 0x7B, 0x00});
    const auto n = notes();
    CHECK(n[0] == 0 && n[1] == 0);
}

TEST(engine_load_ram_resets_analogs) {
    auto e = bootedEngine();
    if (!e) return;
    for (int i = 0; i < 10; ++i) {
        e->setAnalog(ks32::Analog::PitchWheel, 0x100);
        renderMs(*e, 10);
    }
    CHECK_EQ(e->machine().analog(ks32::Analog::PitchWheel), 0x100);
    const std::vector<uint8_t> ram = e->machine().ramImage();
    std::string err;
    CHECK(e->loadRam(ram, err));
    for (int i = 0; i < 100; ++i) {
        e->setAnalog(ks32::Analog::PitchWheel, 0x100);
        renderMs(*e, 10);
    }
    CHECK_EQ(e->machine().analog(ks32::Analog::PitchWheel), 0x200);
    for (int i = 0; i < 230; ++i) {
        e->setAnalog(ks32::Analog::PitchWheel, 0x100);
        renderMs(*e, 10);
    }
    CHECK_EQ(e->machine().analog(ks32::Analog::PitchWheel), 0x100);
}

TEST(engine_set_select_ignores_bad_bytes) {
    auto a = bootedEngine();
    if (!a) return;
    const std::vector<uint8_t> ram = a->machine().ramImage();
    auto b = makeEngine();
    std::string err;
    CHECK(b->loadRam(ram, err));
    const uint8_t bad[] = {0x90, 0x3C, 0x64, 0xC0, 0x05};
    b->setSelect(bad, 5);
    renderWanting(*b, 5, 3300 + 500);
    const app::Snapshot s = b->snapshot();
    CHECK_EQ(s.programsSent, 1u);
    CHECK(rowText(s.panel, 0).find("ROM   05") != std::string::npos);
    CHECK(s.notes[0] == 0 && s.notes[1] == 0);
    CHECK(selectOf(s) == (std::vector<uint8_t>{0xC0, 0x05}));
}
