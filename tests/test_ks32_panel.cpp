// The front-panel model fed with byte sequences that use only the commands decoded from the OS
// v3.01's panel stream (docs/ks32-hardware-interface.md, "Front panel: display and LEDs", K7).
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "ks32/panel.h"

using namespace ens::ks32;

namespace {

void feed(Panel& p, std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) p.fromCpu(b);
}

void feed(Panel& p, const std::string& text) {
    for (char c : text) p.fromCpu(static_cast<uint8_t>(c));
}

const std::string kBlank(16, ' ');

// The queued bytes for the firmware, oldest first.
std::vector<uint8_t> toCpu(Panel& p) {
    std::vector<uint8_t> out;
    for (uint8_t b = 0; p.popToCpu(b);) out.push_back(b);
    return out;
}

}  // namespace

TEST(panel_reset_state) {
    Panel p;
    CHECK(rowText(p.state(), 0) == kBlank);
    CHECK(rowText(p.state(), 1) == kBlank);
    for (Led l : p.state().leds) CHECK(l == Led::Off);
    CHECK_EQ(p.state().blink, 0u);
    CHECK_EQ(p.state().unknown, 0u);
    CHECK(toCpu(p).empty());
}

// 0x87 pp: the next character goes to position pp (row = pp / 16, column = pp % 16).
TEST(panel_text_at_position) {
    Panel p;
    feed(p, {0x87, 0x06});
    feed(p, "00");
    feed(p, {0x87, 0x10});
    feed(p, " KS Grand Piano ");
    CHECK(rowText(p.state(), 0) == "      00        ");
    CHECK(rowText(p.state(), 1) == " KS Grand Piano ");
}

// Text runs on from row 0 into row 1 (the boot's 32-character warning after 0x8C).
TEST(panel_text_runs_into_second_row) {
    Panel p;
    feed(p, {0x87, 0x0E});
    feed(p, "abcd");
    CHECK(rowText(p.state(), 0) == "              ab");
    CHECK(rowText(p.state(), 1) == "cd              ");
}

// 0x8C blanks the display and homes the cursor.
TEST(panel_clear_homes_and_blanks) {
    Panel p;
    feed(p, {0x87, 0x08});
    feed(p, "Battery");
    feed(p, {0x87, 0x1A});
    feed(p, {0x90});
    feed(p, "x");
    feed(p, {0x91, 0x8C});
    feed(p, "Replace edit");
    CHECK(rowText(p.state(), 0) == "Replace edit    ");
    CHECK(rowText(p.state(), 1) == kBlank);
    CHECK_EQ(p.state().blink, 0u);
}

// 0x8D n: LED n on; 0x8E n: LED n off.
TEST(panel_led_on_off) {
    Panel p;
    feed(p, {0x8D, 0x08, 0x8D, 0x0E});
    CHECK(p.state().leds[8] == Led::On);
    CHECK(p.state().leds[14] == Led::On);
    CHECK(p.state().leds[7] == Led::Off);
    feed(p, {0x8E, 0x08});
    CHECK(p.state().leds[8] == Led::Off);
    CHECK(p.state().leds[14] == Led::On);
}

// 0x8F n: LED n blinks, until 0x8D or 0x8E sets it again.
TEST(panel_led_blink) {
    Panel p;
    feed(p, {0x8D, 0x0A, 0x8F, 0x0A});
    CHECK(p.state().leds[10] == Led::Blink);
    feed(p, {0x8E, 0x0A});
    CHECK(p.state().leds[10] == Led::Off);
}

// 0x90 ... 0x91 marks the characters written between them (the value being edited).
TEST(panel_blink_bracket) {
    Panel p;
    feed(p, {0x87, 0x0D, 0x90});
    feed(p, "+00");
    feed(p, {0x91});
    feed(p, "x");
    CHECK(rowText(p.state(), 0) == "             +00");
    CHECK_EQ(p.state().blink, 0x0000E000u);  // positions 13-15; 16 ('x') is not marked
    feed(p, {0x87, 0x0E});
    feed(p, "1");  // rewritten outside the bracket: no longer marked
    CHECK_EQ(p.state().blink, 0x0000A000u);
}

// Commands whose effect is not known: they take their observed parameter bytes and change
// nothing (0x93's nine bytes and the one-byte parameters are not written as text).
TEST(panel_commands_without_visible_effect) {
    Panel p;
    feed(p, {0x80, 0x94, 0xFF, 0x93, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x81, 0x83});
    feed(p, "AB");
    feed(p, {0x92, 0x98, 0x86, 0x00, 0x85, 0x10, 0x95, 0x03});
    feed(p, "CD");
    CHECK(rowText(p.state(), 0) == "ABCD            ");
    CHECK(rowText(p.state(), 1) == kBlank);
    CHECK_EQ(p.state().unknown, 0u);
}

// A command byte outside the decoded table, or an out-of-range parameter, is counted and ignored.
TEST(panel_unknown_bytes_are_counted) {
    Panel p;
    feed(p, {0xA0, 0x87, 0x20, 0x8D, 0x0F});
    feed(p, "A");
    CHECK_EQ(p.state().unknown, 3u);
    CHECK(rowText(p.state(), 0) == "A               ");
    for (Led l : p.state().leds) CHECK(l == Led::Off);
}

// 0x88 n saves the cursor in slot n, 0x89 n moves it back there (a value field redrawn in place:
// "Location = " 87 1B 88 00 "01", then 89 00 88 00 "02").
TEST(panel_cursor_save_restore) {
    Panel p;
    feed(p, {0x87, 0x1B, 0x88, 0x00});
    feed(p, "01");
    feed(p, {0x87, 0x10, 0x88, 0x01});
    feed(p, "Sequence-01");
    feed(p, {0x89, 0x00});
    feed(p, "02");
    feed(p, {0x89, 0x01});
    feed(p, "s");
    CHECK(rowText(p.state(), 1) == "sequence-0102   ");
    CHECK_EQ(p.state().unknown, 0u);
    feed(p, {0x88, 0x02, 0x89, 0x02});  // slots past 1 are not seen
    CHECK_EQ(p.state().unknown, 2u);
}

// A press queues code | 0x80 then 0x00, a release code then 0x00, in call order (K7).
TEST(panel_button_bytes) {
    Panel p;
    p.button(Button::Bank9, true);
    p.button(Button::SelectSound, true);
    CHECK(toCpu(p) == (std::vector<uint8_t>{0xAF, 0x00, 0xB4, 0x00}));
    CHECK(toCpu(p).empty());
    p.button(Button::SelectSound, false);
    p.button(Button::Bank9, false);
    CHECK(toCpu(p) == (std::vector<uint8_t>{0x34, 0x00, 0x2F, 0x00}));
}

// The name table covers each control once: distinct codes below 0x3C, distinct names.
TEST(panel_button_table) {
    std::set<int> codes;
    std::set<std::string> names;
    for (const ButtonName& b : kButtons) {
        CHECK(static_cast<int>(b.button) < 0x3C);
        codes.insert(static_cast<int>(b.button));
        names.insert(b.name);
    }
    CHECK_EQ(codes.size(), std::size(kButtons));
    CHECK_EQ(names.size(), std::size(kButtons));
    CHECK_EQ(std::size(kButtons), 43u);
}

TEST(panel_reset_clears_state) {
    Panel p;
    feed(p, {0x8D, 0x01, 0x90});
    feed(p, "Z");
    feed(p, {0x87});  // a command left waiting for its parameter
    p.button(Button::UpYes, true);
    p.reset();
    CHECK(toCpu(p).empty());
    feed(p, "A");
    CHECK(rowText(p.state(), 0) == "A               ");
    CHECK(p.state().leds[1] == Led::Off);
    CHECK_EQ(p.state().blink, 0u);
}

// K12: a key event is code | 0x80 or code (code = note - 28), then the strike value, never 0
// (that would be a button event). Notes outside the 76 keys (28-103) send nothing.
TEST(panel_key_bytes) {
    Panel p;
    p.key(60, true, 100);
    p.key(60, false, 0);
    p.key(28, true, 255);
    p.key(103, true, 1);
    p.key(27, true, 64);
    p.key(104, true, 64);
    CHECK(toCpu(p) == (std::vector<uint8_t>{0xA0, 100, 0x20, 1, 0x80, 127, 0xCB, 1}));
}

TEST(panel_pop_to_cpu) {
    Panel p;
    p.button(Button::Bank9, true);
    uint8_t b = 0;
    CHECK(p.popToCpu(b));
    CHECK_EQ(b, 0xAF);
    CHECK(p.popToCpu(b));
    CHECK_EQ(b, 0x00);
    CHECK(!p.popToCpu(b));
}

// The state is plain data (static_assert in panel.h): a blank display is 16 spaces a row.
TEST(panel_state_is_fixed_size) {
    CHECK(rowText(Panel{}.state(), 0) == std::string(16, ' '));
    CHECK(rowText(Panel{}.state(), 1) == std::string(16, ' '));
}
