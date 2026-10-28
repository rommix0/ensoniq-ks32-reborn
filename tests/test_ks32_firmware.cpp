// The KS-32 OS ROM (v3.01) booting on the machine. The firmware is run, never studied: these
// tests look only at device accesses, exception counts and device state after kBootMs, and at
// the panel after button presses.
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>

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
