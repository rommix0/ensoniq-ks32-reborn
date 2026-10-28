// The front-panel model fed with byte sequences that use only the commands decoded from the OS
// v3.01's panel stream (docs/ks32-hardware-interface.md, "Front panel: display and LEDs", K7).
#include <cstdint>
#include <initializer_list>
#include <string>

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

}  // namespace

TEST(panel_reset_state) {
    Panel p;
    CHECK_EQ(p.state().rows.size(), 2u);
    CHECK(p.state().rows[0] == kBlank);
    CHECK(p.state().rows[1] == kBlank);
    for (Led l : p.state().leds) CHECK(l == Led::Off);
    CHECK_EQ(p.state().blink, 0u);
    CHECK_EQ(p.state().unknown, 0u);
    CHECK(p.takeToCpu().empty());
}

// 0x87 pp: the next character goes to position pp (row = pp / 16, column = pp % 16).
TEST(panel_text_at_position) {
    Panel p;
    feed(p, {0x87, 0x06});
    feed(p, "00");
    feed(p, {0x87, 0x10});
    feed(p, " KS Grand Piano ");
    CHECK(p.state().rows[0] == "      00        ");
    CHECK(p.state().rows[1] == " KS Grand Piano ");
}

// Text runs on from row 0 into row 1 (the boot's 32-character warning after 0x8C).
TEST(panel_text_runs_into_second_row) {
    Panel p;
    feed(p, {0x87, 0x0E});
    feed(p, "abcd");
    CHECK(p.state().rows[0] == "              ab");
    CHECK(p.state().rows[1] == "cd              ");
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
    CHECK(p.state().rows[0] == "Replace edit    ");
    CHECK(p.state().rows[1] == kBlank);
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
    CHECK(p.state().rows[0] == "             +00");
    CHECK_EQ(p.state().blink, 0x0000E000u);  // positions 13-15; 16 ('x') is not marked
    feed(p, {0x87, 0x0E});
    feed(p, "1");  // rewritten outside the bracket: no longer marked
    CHECK_EQ(p.state().blink, 0x0000A000u);
}

// Commands whose effect is not known: they take their observed parameter bytes and change
// nothing (0x93's nine bytes and the one-byte parameters are not written as text).
TEST(panel_commands_without_visible_effect) {
    Panel p;
    feed(p, {0x80, 0x94, 0xFF, 0x93, 0x0C, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E, 0x12, 0x0C, 0x81, 0x83});
    feed(p, "AB");
    feed(p, {0x92, 0x98, 0x86, 0x00, 0x88, 0x00, 0x89, 0x00, 0x95, 0x03});
    feed(p, "CD");
    CHECK(p.state().rows[0] == "ABCD            ");
    CHECK(p.state().rows[1] == kBlank);
    CHECK_EQ(p.state().unknown, 0u);
}

// A command byte outside the decoded table, or an out-of-range parameter, is counted and ignored.
TEST(panel_unknown_bytes_are_counted) {
    Panel p;
    feed(p, {0xA0, 0x87, 0x20, 0x8D, 0x0F});
    feed(p, "A");
    CHECK_EQ(p.state().unknown, 3u);
    CHECK(p.state().rows[0] == "A               ");
    for (Led l : p.state().leds) CHECK(l == Led::Off);
}

TEST(panel_reset_clears_state) {
    Panel p;
    feed(p, {0x8D, 0x01, 0x90});
    feed(p, "Z");
    feed(p, {0x87});  // a command left waiting for its parameter
    p.reset();
    feed(p, "A");
    CHECK(p.state().rows[0] == "A               ");
    CHECK(p.state().leds[1] == Led::Off);
    CHECK_EQ(p.state().blink, 0u);
}
