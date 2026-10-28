// The KS-32 OS ROM (v3.01) booting on the machine. The firmware is run, never studied: these
// tests look only at device accesses, exception counts and device state after kBootMs, and at
// the panel after button presses.
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>

#include "check.h"
#include "ks32_firmware.h"

using namespace ens::ks32;

namespace {

// One machine booted for kBootMs, shared by the tests that only read its state (suite runtime);
// nullptr without the ROMs.
Machine* bootedKs32() {
    static const std::unique_ptr<Machine> m = [] {
        auto b = loadKs32();
        if (b) runMs(*b, kBootMs);
        return b;
    }();
    if (!m) std::printf("  skip: ROMs not found\n");
    return m.get();
}

}  // namespace

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

// The steady display the boot reaches without a button press. With no analog inputs yet (Task 6)
// it is the low-battery warning (service manual PDF p12: "WARNING! Battery low. See manual.",
// then "Press any button to continue"), not the play screen. The boot sends no LED command.
// Task 6 updates this pin when the warning goes away.
TEST(ks32_firmware_boot_shows_play_screen) {
    Machine* m = bootedKs32();
    if (!m) return;
    const PanelState& p = m->panel();
    CHECK_EQ(p.rows.size(), 2u);
    if (p.rows.size() == 2) {
        CHECK(p.rows[0] == "WARNING! Battery");
        CHECK(p.rows[1] == "low. See manual.");
    }
    for (Led l : p.leds) CHECK(l == Led::Off);
    CHECK_EQ(p.blink, 0u);
    CHECK_EQ(p.unknown, 0u);  // every byte the boot sends is in the decoded table (K7)
}

namespace {

// One machine past the battery warning for the button tests, which mutate it (one boot for all
// of them, suite runtime). The warning is dismissed by a press and release of Select Sound
// queued in the same slot; afterTap is the panel 600 ms later. Each test starts by pressing Select
// Sound and leaves no prompt open, so the tests do not depend on their order.
struct PanelRig {
    std::unique_ptr<Machine> m;
    PanelState afterTap;
};

PanelRig& panelKs32() {
    static PanelRig r = [] {
        PanelRig x;
        x.m = loadKs32();
        if (!x.m) return x;
        runMs(*x.m, kBootMs);
        x.m->button(Button::SelectSound, true);
        x.m->button(Button::SelectSound, false);
        runMs(*x.m, 600);
        x.afterTap = x.m->panel();
        return x;
    }();
    if (!r.m) std::printf("  skip: ROMs not found\n");
    return r;
}

// Press b, hold it holdMs, release it, then give the firmware 300 ms.
void press(Machine& m, Button b, uint32_t holdMs = 100) {
    m.button(b, true);
    runMs(m, holdMs);
    m.button(b, false);
    runMs(m, 300);
}

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

// A press and release queued in one slot does what a 100 ms press does: both dismiss the warning
// to the same display and LEDs.
TEST(ks32_firmware_quick_tap_is_seen) {
    PanelRig& r = panelKs32();
    if (!r.m) return;
    auto m = loadKs32();
    runMs(*m, kBootMs);
    m->button(Button::SelectSound, true);
    runMs(*m, 100);
    m->button(Button::SelectSound, false);
    runMs(*m, 500);
    std::printf("  display: %s\n", shown(r.afterTap).c_str());
    CHECK(shown(r.afterTap).find("WARNING") == std::string::npos);
    CHECK(m->panel().rows == r.afterTap.rows);
    CHECK(m->panel().leds == r.afterTap.leds);
}
