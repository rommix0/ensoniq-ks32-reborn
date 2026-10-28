#pragma once
// The KS-32 keypad/display board as the firmware drives it over DUART channel B (K7): a 2 x 16
// character display and 15 LEDs. The command set is decoded from the byte stream the OS v3.01
// sends (docs/ks32-hardware-interface.md, "Front panel: display and LEDs"), never from ROM code.
// Pure emulation: no I/O, no statics.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ens::ks32 {

constexpr int kPanelRows = 2, kPanelCols = 16, kPanelLeds = 15;

enum class Led : uint8_t { Off, On, Blink };

struct PanelState {
    std::vector<std::string> rows;  // kPanelRows strings of kPanelCols character codes (0x00-0x7F)
    uint32_t blink = 0;             // bit p: position p (row * 16 + column) written inside 0x90 ... 0x91
    std::array<Led, kPanelLeds> leds{};  // by LED number, the parameter of 0x8D/0x8E/0x8F
    uint32_t unknown = 0;           // command bytes outside the decoded table and out-of-range parameters
};

class Panel {
public:
    Panel() { reset(); }
    void reset();                  // blank display, LEDs off, cursor at 0
    void fromCpu(uint8_t byte);    // one byte the firmware sent to the panel
    const PanelState& state() const { return s_; }
    std::vector<uint8_t> takeToCpu() { return {}; }  // no start-up handshake (K7); Task 5 adds buttons

private:
    void run(uint8_t cmd, uint8_t param);

    PanelState s_;
    uint8_t cursor_ = 0;    // next write position, 0..31
    bool blinkOn_ = false;  // inside 0x90 ... 0x91
    uint8_t cmd_ = 0;       // command waiting for parameter bytes (0 = none)
    uint8_t param_ = 0;     // its first parameter
    int got_ = 0, need_ = 0;
};

}  // namespace ens::ks32
