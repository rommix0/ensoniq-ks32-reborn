#include "ks32/panel.h"

namespace ens::ks32 {

namespace {

constexpr int kPositions = kPanelRows * kPanelCols;

// DECISION K7: parameter bytes per command, as observed in the OS v3.01 stream. A byte of 0x80 or
// above outside this table is counted in `unknown` and takes none.
int paramCount(uint8_t cmd) {
    switch (cmd) {
        case 0x80: case 0x81: case 0x83: case 0x8C: case 0x90: case 0x91: case 0x92: case 0x98:
            return 0;
        case 0x86: case 0x87: case 0x88: case 0x89: case 0x8D: case 0x8E: case 0x8F: case 0x94: case 0x95:
            return 1;
        case 0x93:
            return 9;
        default:
            return -1;
    }
}

}  // namespace

void Panel::reset() {
    s_ = PanelState{};
    s_.rows.assign(kPanelRows, std::string(kPanelCols, ' '));
    cursor_ = 0;
    blinkOn_ = false;
    cmd_ = 0;
    got_ = need_ = 0;
}

void Panel::fromCpu(uint8_t b) {
    if (cmd_) {  // a parameter byte, whatever its value (0x94 takes 0xFF)
        if (got_++ == 0) param_ = b;
        if (got_ == need_) {
            const uint8_t c = cmd_;
            cmd_ = 0;
            run(c, param_);
        }
        return;
    }
    if (b < 0x80) {  // text: one character at the cursor, which moves on
        // DECISION K7: past position 31 the cursor wraps to 0 (the firmware never writes there).
        s_.rows[cursor_ / kPanelCols][cursor_ % kPanelCols] = static_cast<char>(b);
        const uint32_t bit = 1u << cursor_;
        s_.blink = blinkOn_ ? (s_.blink | bit) : (s_.blink & ~bit);
        cursor_ = static_cast<uint8_t>((cursor_ + 1) % kPositions);
        return;
    }
    const int n = paramCount(b);
    if (n < 0) {
        ++s_.unknown;
    } else if (n == 0) {
        run(b, 0);
    } else {
        cmd_ = b;
        got_ = 0;
        need_ = n;
    }
}

// The commands with a known effect; the others in paramCount change nothing that is modelled.
void Panel::run(uint8_t cmd, uint8_t param) {
    switch (cmd) {
        case 0x87:  // cursor to position param
            if (param < kPositions) cursor_ = param;
            else ++s_.unknown;
            break;
        case 0x8C:  // blank the display, cursor to 0
            for (std::string& r : s_.rows) r.assign(kPanelCols, ' ');
            s_.blink = 0;
            cursor_ = 0;
            break;
        case 0x8D:
        case 0x8E:
        case 0x8F:  // LED param on, off, blinking
            if (param < kPanelLeds) s_.leds[param] = cmd == 0x8D ? Led::On : cmd == 0x8E ? Led::Off : Led::Blink;
            else ++s_.unknown;
            break;
        case 0x90:  // characters written from here on blink ...
            blinkOn_ = true;
            break;
        case 0x91:  // ... until here
            blinkOn_ = false;
            break;
        default:
            break;
    }
}

}  // namespace ens::ks32
