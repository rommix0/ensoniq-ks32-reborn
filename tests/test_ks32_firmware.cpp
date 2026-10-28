// The KS-32 OS ROM (v3.01) booting on the machine. The firmware is run, never studied: these
// tests look only at device accesses, exception counts and device state after kBootMs, and at
// the panel after button presses.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "ks32_firmware.h"

using namespace ens::ks32;

namespace {

// The display as one line: rows joined, upper case, runs of spaces collapsed.
std::string shown(const PanelState& p) {
    std::string s;
    for (const std::string& row : p.rows)
        for (const char c : row + " ") {
            const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (u != ' ' || (!s.empty() && s.back() != ' ')) s += u;
        }
    return s;
}

// What the display showed at any frame of the shared boot.
struct BootSeen {
    bool lowBattery = false;       // "LOW" together with "BATTERY"
    bool unexpectedEvent = false;  // "Unexpected Event ID = n" (service manual PDF p11)
    bool card = false;             // "CARD" anywhere ("SORRY! Install a RAM card first.", K10)
};
BootSeen bootSeen;

// One machine booted from blank RAM for kBootMs with the K13 resting analog values, shared by
// the tests that only read its state (suite runtime); nullptr without the ROMs. The display is
// polled after every frame (bootSeen).
Machine* bootedKs32() {
    static const std::unique_ptr<Machine> m = [] {
        auto b = loadKs32();
        if (!b) return b;
        const uint64_t frames = static_cast<uint64_t>(kBootMs) * kWavRate / 1000;
        int16_t l, r;
        for (uint64_t f = 0; f < frames; ++f) {
            b->runFrame(l, r);
            const std::string s = shown(b->panel());
            if (s.find("LOW") != std::string::npos && s.find("BATTERY") != std::string::npos)
                bootSeen.lowBattery = true;
            if (s.find("EVENT") != std::string::npos) bootSeen.unexpectedEvent = true;
            if (s.find("CARD") != std::string::npos) bootSeen.card = true;
        }
        return b;
    }();
    if (!m) std::printf("  skip: ROMs not found\n");
    return m.get();
}

}  // namespace

TEST(ks32_firmware_cold_boot_is_clean) {
    Machine* m = bootedKs32();
    if (!m) return;
    CHECK(!bootSeen.lowBattery);
    CHECK(!bootSeen.unexpectedEvent);
}

// The boot clock test leaves OTIS at 32 voices with the K3 clocks (design 10 A).
TEST(ks32_firmware_boot_keeps_32_voices) {
    Machine* m = bootedKs32();
    if (!m) return;
    CHECK_EQ(m->board().otis().activeVoices(), 32);
}

TEST(ks32_firmware_boot_takes_no_fault_vectors) {
    Machine* m = bootedKs32();
    if (!m) return;
    for (int v : {2, 3, 4, 5, 6, 7, 8, 11, 14, 15, 24}) {
        if (m->cpu().exceptionCount(v) != 0) std::printf("  vector %d taken %u times\n", v, m->cpu().exceptionCount(v));
        CHECK_EQ(m->cpu().exceptionCount(v), 0u);
    }
    CHECK(!m->cpu().halted());
    std::printf("  line A (vector 10) taken %u times\n", m->cpu().exceptionCount(10));  // R15: printed only
}

// Every access is in the K1 map, with the device's own width.
TEST(ks32_firmware_boot_is_fully_mapped) {
    auto m = loadKs32();
    if (!m) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    uint32_t unmapped = 0, otisBytes = 0, evenBytes = 0;
    m->onBusEvent = [&](const BusEvent& e) {
        if (e.dev == Device::Unmapped) ++unmapped;
        if (e.dev == Device::Otis && e.size == 1) ++otisBytes;
        if ((e.dev == Device::Esp || e.dev == Device::Duart) && e.size == 1 && !(e.addr & 1)) ++evenBytes;
    };
    runMs(*m, kBootMs);
    CHECK_EQ(unmapped, 0u);
    CHECK_EQ(m->duart().unsupportedWrites(), 0u);
    CHECK_EQ(otisBytes, 0u);
    CHECK_EQ(evenBytes, 0u);
}

// The firmware releases ESPHALT (K15) and sets the serial control register (findings C).
TEST(ks32_firmware_boot_runs_the_esp) {
    Machine* m = bootedKs32();
    if (!m) return;
    CHECK(!m->board().esp().halted());
    CHECK_EQ(m->board().esp().serialControl(), 0x63);
}

TEST(ks32_firmware_boot_is_deterministic) {
    auto a = loadKs32();
    if (!a) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    auto b = loadKs32();
    CHECK_EQ(traceHash(*a, kBootMs), traceHash(*b, kBootMs));
}

// The steady display the boot reaches without a button press. With the K13 resting values (battery
// nominal) it is the play screen: Select Sound mode on ROM sound 00, with only the Select Sound LED
// (8) lit. Below a battery reading of about 125 the boot ends on the low-battery warning instead.
TEST(ks32_firmware_boot_shows_play_screen) {
    Machine* m = bootedKs32();
    if (!m) return;
    const PanelState& p = m->panel();
    CHECK_EQ(p.rows.size(), 2u);
    if (p.rows.size() == 2) {
        CHECK(p.rows[0] == "ROM   00        ");
        CHECK(p.rows[1] == " KS Grand Piano ");
    }
    for (int i = 0; i < kPanelLeds; ++i) CHECK(p.leds[i] == (i == 8 ? Led::On : Led::Off));
    CHECK_EQ(p.blink, 0u);
    CHECK_EQ(p.unknown, 0u);  // every byte the boot sends is in the decoded table (K7)
}

namespace {

// Press b, hold it holdMs, release it, then give the firmware 300 ms.
void press(Machine& m, Button b, uint32_t holdMs = 100) {
    m.button(b, true);
    runMs(m, holdMs);
    m.button(b, false);
    runMs(m, 300);
}

// One booted machine for the button tests, which mutate it (one boot for all of them, suite
// runtime). Two quick taps, each a press and release queued in the same slot, 600 ms apart:
// Up/Yes, which acts on the press and steps ROM 00 to ROM 01 (afterTap), then Replace Track Sound,
// which acts on its release (afterReplace). Select Sound then leaves that mode. Each test starts by
// pressing Select Sound and leaves no prompt open, so the tests do not depend on their order.
struct PanelRig {
    std::unique_ptr<Machine> m;
    PanelState afterTap, afterReplace;
    uint32_t overruns = 0;  // channel B receive overruns over the two taps
};

PanelRig& panelKs32() {
    static PanelRig r = [] {
        PanelRig x;
        x.m = loadKs32();
        if (!x.m) return x;
        runMs(*x.m, kBootMs);
        x.m->button(Button::UpYes, true);
        x.m->button(Button::UpYes, false);
        runMs(*x.m, 600);
        x.afterTap = x.m->panel();
        x.m->button(Button::ReplaceTrackSound, true);
        x.m->button(Button::ReplaceTrackSound, false);
        runMs(*x.m, 600);
        x.afterReplace = x.m->panel();
        x.overruns = x.m->duart().overruns(kPanelChannel);
        press(*x.m, Button::SelectSound);
        return x;
    }();
    if (!r.m) std::printf("  skip: ROMs not found\n");
    return r;
}

// Service manual PDF p5: "Press Edit Seq/Preset. While holding down Bank 9, press Screen 1"
// (Screen 9: reinitialize, PDF p4; Screen 0: analog tests, PDF p13).
std::string serviceScreen(Machine& m, Button screen) {
    press(m, Button::SelectSound);
    press(m, Button::EditSeqPreset);
    m.button(Button::Bank9, true);
    runMs(m, 100);
    press(m, screen);
    m.button(Button::Bank9, false);
    runMs(m, 800);
    return shown(m.panel());
}

}  // namespace

TEST(ks32_firmware_version_screen) {
    PanelRig& r = panelKs32();
    if (!r.m) return;
    const std::string s = serviceScreen(*r.m, Button::Screen1);
    std::printf("  display: %s\n", s.c_str());
    CHECK(s.find("ENSONIQ KS-32") != std::string::npos);
    CHECK(s.find("3.01") != std::string::npos);
}

// Bank 9 released before Screen 1: no combination, so the firmware sees the release.
TEST(ks32_firmware_release_ends_the_hold) {
    PanelRig& r = panelKs32();
    if (!r.m) return;
    press(*r.m, Button::SelectSound);
    press(*r.m, Button::EditSeqPreset);
    press(*r.m, Button::Bank9);
    press(*r.m, Button::Screen1);
    const std::string s = shown(r.m->panel());
    std::printf("  display: %s\n", s.c_str());
    CHECK(s.find("ENSONIQ") == std::string::npos);
}

// The prompt is left with Down/No; Up/Yes would erase the RAM.
TEST(ks32_firmware_reinit_prompt) {
    PanelRig& r = panelKs32();
    if (!r.m) return;
    const std::string s = serviceScreen(*r.m, Button::Screen9);
    std::printf("  display: %s\n", s.c_str());
    CHECK(s.find("REINITIALIZE") != std::string::npos);
    press(*r.m, Button::DownNo);
    CHECK(shown(r.m->panel()).find("REINITIALIZE") == std::string::npos);
}

// The page is left with Screen 5 (service manual PDF p21).
TEST(ks32_firmware_analog_test_page) {
    PanelRig& r = panelKs32();
    if (!r.m) return;
    const std::string s = serviceScreen(*r.m, Button::Screen0);
    std::printf("  display: %s\n", s.c_str());
    CHECK(s.find("ANALOG") != std::string::npos);
    press(*r.m, Button::Screen5);
    CHECK(shown(r.m->panel()).find("ANALOG") == std::string::npos);
}

// A press and release queued in one slot does what a 100 ms press does, with nothing lost to an
// overrun: Up/Yes (acts on the press) steps ROM 00 to ROM 01, and Replace Track Sound (acts on
// the release) opens its page, to the same display and LEDs. A lost release would leave the sound
// screen after Replace Track Sound.
TEST(ks32_firmware_quick_tap_is_seen) {
    PanelRig& r = panelKs32();
    if (!r.m) return;
    CHECK_EQ(r.overruns, 0u);
    auto m = loadKs32();
    runMs(*m, kBootMs);
    m->button(Button::UpYes, true);
    runMs(*m, 100);
    m->button(Button::UpYes, false);
    runMs(*m, 500);
    std::printf("  display: %s\n", shown(r.afterTap).c_str());
    CHECK(shown(r.afterTap).find("ROM 01") != std::string::npos);
    CHECK(m->panel().rows == r.afterTap.rows);
    CHECK(m->panel().leds == r.afterTap.leds);
    m->button(Button::ReplaceTrackSound, true);
    runMs(*m, 100);
    m->button(Button::ReplaceTrackSound, false);
    runMs(*m, 500);
    std::printf("  display: %s\n", shown(r.afterReplace).c_str());
    CHECK(r.afterReplace.leds[14] == Led::Blink);
    CHECK(m->panel().rows == r.afterReplace.rows);
    CHECK(m->panel().leds == r.afterReplace.leds);
}

// K11 (ruling T8-1): the battery RAM keeps the settings. Master Tune is set to +02 on the shared
// panel machine (System/MIDI, Up/Yes twice), the RAM is saved and the setting put back. A fresh
// machine booted from the image ends on the play screen with no warning or unexpected event and
// shows Master Tune= +02. OS v3.01 powers on to ROM 00 whatever was selected (warm or cold), so
// the play screen is the cold boot's.
TEST(ks32_firmware_ram_image_keeps_settings) {
    PanelRig& r = panelKs32();
    if (!r.m) return;
    press(*r.m, Button::SelectSound);
    press(*r.m, Button::SystemMidi);
    press(*r.m, Button::UpYes);
    press(*r.m, Button::UpYes);
    const std::string set = shown(r.m->panel());
    const std::vector<uint8_t> img = r.m->ramImage();
    press(*r.m, Button::DownNo);
    press(*r.m, Button::DownNo);
    press(*r.m, Button::SelectSound);

    auto m = loadKs32();
    std::string err;
    CHECK(m->loadRamImage(img, err));
    bool warned = false;
    for (uint32_t ms = 0; ms < kBootMs; ms += 100) {
        runMs(*m, 100);
        const std::string s = shown(m->panel());
        warned |= s.find("EVENT") != std::string::npos || s.find("LOW") != std::string::npos;
    }
    const std::string booted = shown(m->panel());
    press(*m, Button::SystemMidi);
    const std::string tune = shown(m->panel());
    std::printf("  saved: %s; booted: %s; %s\n", set.c_str(), booted.c_str(), tune.c_str());
    CHECK(set.find("MASTER TUNE= +02") != std::string::npos);
    CHECK(!warned);
    CHECK(booted.find("ROM 00 KS GRAND PIANO") != std::string::npos);
    CHECK(tune.find("MASTER TUNE= +02") != std::string::npos);
}

// K10: the slot is empty. The boot to the play screen shows nothing about a card, and a copy to the
// card is refused with "SORRY! Install a RAM card first." without the firmware touching the card
// window (0x2E0000, probed only while DUART IP1, the card detect, is low).
TEST(ks32_firmware_card_slot_empty) {
    Machine* b = bootedKs32();
    if (!b) return;
    CHECK(!bootSeen.card);
    CHECK(!bootSeen.unexpectedEvent);
    CHECK_EQ(b->duart().read(13) & 0x02, 0x02);  // IP1 high: no card (reading IP has no side effect)
    PanelRig& r = panelKs32();
    if (!r.m) return;
    Machine& m = *r.m;
    uint32_t unmapped = 0;
    m.onBusEvent = [&unmapped](const BusEvent& e) { unmapped += e.dev == Device::Unmapped; };
    press(m, Button::SelectSound);
    press(m, Button::EditSound);
    press(m, Button::Bank9);      // "Press ENTER to Store Sounds"
    press(m, Button::EnterSave);  // "Storage Type = MEMORY CARD"
    press(m, Button::DownNo);     // stays on MEMORY CARD, the first type
    press(m, Button::EnterSave);  // "Operation = COPY INT TO CARD A"
    press(m, Button::EnterSave);
    const std::string s = shown(m.panel());
    m.onBusEvent = nullptr;
    std::printf("  display: %s\n", s.c_str());
    CHECK(s.find("INSTALL A RAM CARD") != std::string::npos);
    CHECK_EQ(unmapped, 0u);
    runMs(m, 1600);  // the message goes back to "Press ENTER to Store Sounds"
    press(m, Button::SelectSound);
}

namespace {

// The number after "<name>=" on the display, -1 when absent.
int reading(const PanelState& p, const char* name) {
    const std::string s = shown(p), key = std::string(name) + "=";
    const size_t at = s.find(key);
    return at == std::string::npos ? -1 : std::atoi(s.c_str() + at + key.size());
}

// Presses an Analog Test screen button (Screen 0-4) and returns the display.
const PanelState& analogScreen(Machine& m, Button screen) {
    press(m, screen);
    return m.panel();
}

}  // namespace

// Service manual PDF p13, p20 and p21 (manual pp. 10, 17 and 18): with the K13 resting values
// the battery reads inside its 125-205 band, the unplugged CV pedal 127 and the pressure with no
// key pressed 0. The other rests: pitch wheel centred (64), mod wheel 0, volume at maximum (127).
TEST(ks32_firmware_analog_resting_values) {
    PanelRig& r = panelKs32();
    if (!r.m) return;
    Machine& m = *r.m;
    std::printf("  screen 0: %s\n", serviceScreen(m, Button::Screen0).c_str());
    CHECK_EQ(reading(m.panel(), "PW"), 64);
    CHECK_EQ(reading(m.panel(), "MOD"), 0);
    std::printf("  screen 1: %s\n", shown(analogScreen(m, Button::Screen1)).c_str());
    CHECK_EQ(reading(m.panel(), "CV"), 127);
    CHECK_EQ(reading(m.panel(), "VOL"), 127);
    std::printf("  screen 2: %s\n", shown(analogScreen(m, Button::Screen2)).c_str());
    const int bat = reading(m.panel(), "BAT");
    CHECK(bat >= 125 && bat <= 205);
    std::printf("  screen 4: %s\n", shown(analogScreen(m, Button::Screen4)).c_str());
    CHECK_EQ(reading(m.panel(), "PRESSURE"), 0);
    press(m, Button::Screen5);  // leaves the page (PDF p21)
}

// The two sliders at both ends of the converter's range read the ends of the manual's table
// (PDF p20, rendered: Vol= 0-127, Knob= 0-255). The firmware smooths the readings, so each
// setting runs 1.5 s before it is read.
TEST(ks32_firmware_slider_extremes) {
    PanelRig& r = panelKs32();
    if (!r.m) return;
    Machine& m = *r.m;
    serviceScreen(m, Button::Screen0);
    for (const uint16_t raw : {uint16_t{0}, uint16_t{0x3FF}}) {
        m.setAnalog(Analog::Volume, raw);
        m.setAnalog(Analog::DataEntry, raw);
        runMs(m, 1500);
        const int vol = reading(analogScreen(m, Button::Screen1), "VOL");
        const int knob = reading(analogScreen(m, Button::Screen2), "KNOB");
        std::printf("  raw 0x%03X: vol=%d knob=%d\n", raw, vol, knob);
        CHECK_EQ(vol, raw ? 127 : 0);
        CHECK_EQ(knob, raw ? 255 : 0);
    }
    m.setAnalog(Analog::Volume, kAnalogRest[static_cast<int>(Analog::Volume)]);
    m.setAnalog(Analog::DataEntry, kAnalogRest[static_cast<int>(Analog::DataEntry)]);
    press(m, Button::Screen5);
}

// ---- MIDI (Task 7) ------------------------------------------------------------------------------

namespace {

// FNV-1a 64 over the samples, each as two little-endian bytes.
uint64_t pcmHash(const std::vector<int16_t>& pcm) {
    uint64_t h = 14695981039346656037ull;
    for (const int16_t s : pcm)
        for (int i = 0; i < 2; ++i) {
            h ^= static_cast<uint8_t>(static_cast<uint16_t>(s) >> (8 * i));
            h *= 1099511628211ull;
        }
    return h;
}

// Runs ms of emulated time; the largest |sample| of either channel.
int peakMs(Machine& m, uint32_t ms) {
    const uint64_t frames = static_cast<uint64_t>(ms) * kWavRate / 1000;
    int16_t l, r;
    int peak = 0;
    for (uint64_t f = 0; f < frames; ++f) {
        m.runFrame(l, r);
        peak = std::max({peak, std::abs(static_cast<int>(l)), std::abs(static_cast<int>(r))});
    }
    return peak;
}

void sendMidi(Machine& m, std::initializer_list<uint8_t> bytes) {
    for (const uint8_t b : bytes) m.midiIn(b);
}

// One booted machine for the MIDI checks, run in sequence at construction (one boot, suite
// runtime). The firmware's default sound (ROM 00) and MIDI settings (System/MIDI shows "Base
// Channel= 01", "MIDI Mode=OMNI"), so channel 1 is played:
//   200 ms of silence; middle C at velocity 100 for 300 ms (noteOn), then its note-off and 1 s;
//   a burst of 32 note-ons and 32 note-offs queued at once, then 2 s, polling the display;
//   2 s more, then the peak over 200 ms (released); program change 5, then 500 ms.
struct MidiRig {
    std::unique_ptr<Machine> m;
    std::vector<uint8_t> bootOut;  // what the boot sent on MIDI out
    int silence = 0, noteOn = 0, released = 0;
    uint32_t overruns = 0;
    bool saw144 = false, sawEvent = false;
    uint32_t otherAcks = 0;  // interrupt acknowledges not answered with the DUART's vector 64 (K2)
    std::vector<std::string> beforeProgram, afterProgram;
};

MidiRig& midiKs32() {
    static MidiRig r = [] {
        MidiRig x;
        x.m = loadKs32();
        if (!x.m) return x;
        Machine& m = *x.m;
        runMs(m, kBootMs);
        x.bootOut = m.takeMidiOut();
        m.onBusEvent = [&x](const BusEvent& e) {
            if (e.dev == Device::Iack && e.value != 64) ++x.otherAcks;
        };
        x.silence = peakMs(m, 200);
        sendMidi(m, {0x90, 0x3C, 0x64});
        x.noteOn = peakMs(m, 300);
        sendMidi(m, {0x80, 0x3C, 0x40});
        runMs(m, 1000);
        for (uint8_t k = 0; k < 32; ++k) sendMidi(m, {0x90, static_cast<uint8_t>(36 + k), 100});
        for (uint8_t k = 0; k < 32; ++k) sendMidi(m, {0x80, static_cast<uint8_t>(36 + k), 64});
        int16_t l, rr;
        for (uint64_t f = 0; f < 2ull * kWavRate; ++f) {
            m.runFrame(l, rr);
            const std::string s = shown(m.panel());
            x.saw144 |= s.find("144") != std::string::npos;
            x.sawEvent |= s.find("EVENT") != std::string::npos;
        }
        x.overruns = m.duart().overruns(kMidiChannel);
        runMs(m, 2000);
        x.released = peakMs(m, 200);
        x.beforeProgram = m.panel().rows;
        sendMidi(m, {0xC0, 0x05});
        runMs(m, 500);
        x.afterProgram = m.panel().rows;
        m.onBusEvent = nullptr;
        return x;
    }();
    if (!r.m) std::printf("  skip: ROMs not found\n");
    return r;
}

}  // namespace

// K3: channel A runs from the 500 kHz external 16x clock (CSRA = 0xEE): 128 X1 clocks per bit,
// 31 250 baud.
TEST(ks32_firmware_midi_rate) {
    Machine* m = bootedKs32();
    if (!m) return;
    CHECK_EQ(m->duart().rxBitClocks(kMidiChannel), 128u);
    MidiRig& r = midiKs32();
    if (!r.m) return;
    std::printf("  boot MIDI out: %zu bytes, first %02X\n", r.bootOut.size(), r.bootOut.empty() ? 0 : r.bootOut[0]);
    CHECK(!r.bootOut.empty() && (r.bootOut[0] & 0x80));  // a status byte
}

TEST(ks32_firmware_midi_note_sounds) {
    MidiRig& r = midiKs32();
    if (!r.m) return;
    std::printf("  peak: silence %d, note %d\n", r.silence, r.noteOn);
    CHECK(r.silence < 64);
    CHECK(r.noteOn > 1000);
}

// 64 messages queued at once enter at the line rate (D2) and none is lost to an overrun. Every
// interrupt is the DUART's: the firmware never enables an OTIS voice interrupt (K2). Afterwards
// the notes have released.
TEST(ks32_firmware_midi_burst_no_overrun) {
    MidiRig& r = midiKs32();
    if (!r.m) return;
    std::printf("  overruns %u, peak 4 s after the burst %d\n", r.overruns, r.released);
    CHECK_EQ(r.overruns, 0u);
    CHECK(!r.saw144);
    CHECK(!r.sawEvent);
    CHECK_EQ(r.otherAcks, 0u);
    CHECK(r.released < 256);
}

TEST(ks32_firmware_program_change_changes_display) {
    MidiRig& r = midiKs32();
    if (!r.m) return;
    PanelState before, after;
    before.rows = r.beforeProgram;
    after.rows = r.afterProgram;
    std::printf("  %s -> %s\n", shown(before).c_str(), shown(after).c_str());
    CHECK(r.afterProgram != r.beforeProgram);
    CHECK(shown(after).find("ROM 05") != std::string::npos);
}

namespace {

// Boots a fresh machine, then renders 2 s of a fixed event list (two notes, a program change);
// the PCM hash.
uint64_t renderTwoSeconds() {
    auto m = loadKs32();
    if (!m) return 0;
    runMs(*m, kBootMs);
    std::vector<int16_t> pcm;
    int16_t l, r;
    for (uint64_t f = 0; f < 2ull * kWavRate; ++f) {
        if (f == 0) sendMidi(*m, {0x90, 60, 100});
        if (f == kWavRate / 4) sendMidi(*m, {0x90, 64, 80});
        if (f == kWavRate / 2) sendMidi(*m, {0x80, 60, 64, 0x80, 64, 64});
        if (f == kWavRate) sendMidi(*m, {0xC0, 5, 0x90, 67, 100});
        if (f == kWavRate * 3 / 2) sendMidi(*m, {0x80, 67, 64});
        m->runFrame(l, r);
        pcm.push_back(l);
        pcm.push_back(r);
    }
    return pcmHash(pcm);
}

}  // namespace

TEST(ks32_firmware_render_is_deterministic) {
    const uint64_t a = renderTwoSeconds();
    if (!a) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    const uint64_t b = renderTwoSeconds();
    std::printf("  render hash %016llx\n", static_cast<unsigned long long>(a));
    CHECK_EQ(a, b);
}

// ---- Local keyboard (Task 8) --------------------------------------------------------------------

// K12: the keyboard rides the panel link. A key sounds on the sound screen, and the firmware sends
// it on MIDI out with note = code + 28: middle C at strike 127 is 90 3C 7F, then its release
// 80 3C; the lowest key (E1, 28) and the highest (G7, 103) go out as notes 0x1C and 0x67.
TEST(ks32_firmware_key_sounds) {
    PanelRig& r = panelKs32();
    if (!r.m) return;
    Machine& m = *r.m;
    press(m, Button::SelectSound);
    (void)m.takeMidiOut();
    const int silence = peakMs(m, 100);
    m.key(60, true, 127);
    const int on = peakMs(m, 300);
    m.key(60, false, 64);
    runMs(m, 100);
    for (const int note : {kLowestKey, kLowestKey + kKeyCount - 1}) {
        m.key(note, true, 127);
        runMs(m, 100);
        m.key(note, false, 127);
        runMs(m, 100);
    }
    runMs(m, 1500);
    const std::vector<uint8_t> out = m.takeMidiOut();
    std::printf("  peak: silence %d, key %d; MIDI out:", silence, on);
    for (const uint8_t b : out) std::printf(" %02X", b);
    std::printf("\n");
    CHECK(silence < 64);
    CHECK(on > 1000);
    CHECK_EQ(out.size(), 18u);
    if (out.size() == 18) {
        CHECK(std::vector<uint8_t>(out.begin(), out.begin() + 5) == (std::vector<uint8_t>{0x90, 0x3C, 0x7F, 0x80, 0x3C}));
        CHECK(std::vector<uint8_t>(out.begin() + 6, out.begin() + 11) == (std::vector<uint8_t>{0x90, 0x1C, 0x7F, 0x80, 0x1C}));
        CHECK(std::vector<uint8_t>(out.begin() + 12, out.begin() + 17) == (std::vector<uint8_t>{0x90, 0x67, 0x7F, 0x80, 0x67}));
    }
}
