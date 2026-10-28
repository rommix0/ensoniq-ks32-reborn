#pragma once
// The KS-32 keypad/display board on DUART channel B (K7): a 2 x 16 character display, 15 LEDs and
// 43 buttons, and the 76-key keyboard whose events share the link (K12). The command set is decoded
// from the byte stream the OS v3.01 sends, and the button and key codes from what it shows, plays
// and sends on MIDI out when they are sent (docs/ks32-hardware-interface.md, "Front panel",
// "Keyboard"), never from ROM code. Pure emulation: no I/O, no statics.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ens::ks32 {

constexpr int kPanelRows = 2, kPanelCols = 16, kPanelLeds = 15;
// The keyboard: 76 keys, E1 to G7 (MIDI notes 28-103); key code = note - kLowestKey (K12).
constexpr int kLowestKey = 28, kKeyCount = 76;

enum class Led : uint8_t { Off, On, Blink };

// The panel's controls by key code, named after the function the firmware shows for each
// (docs/ks32-hardware-interface.md, "Front panel: buttons"). Record, Stop and Play are not found.
enum class Button : uint8_t {
    Screen2 = 0x04, Bank2 = 0x05, Screen4 = 0x06, Bank4 = 0x07, Track7 = 0x08, SystemMidi = 0x0A,
    DownNo = 0x0C, UpYes = 0x0D, Screen5 = 0x0E, Bank5 = 0x0F, Track1 = 0x10, Track5 = 0x11,
    Screen1 = 0x14, Bank1 = 0x15, Screen6 = 0x16, Bank6 = 0x17, Track3 = 0x18, SelectSeqPreset = 0x19,
    EditTrack = 0x1A, Left = 0x1C, Right = 0x1D, Screen8 = 0x1E, Bank8 = 0x1F, Track2 = 0x20,
    Track6 = 0x21, Screen0 = 0x24, Bank0 = 0x25, Screen7 = 0x26, Bank7 = 0x27, Track4 = 0x28,
    ReplaceTrackSound = 0x2A, EnterSave = 0x2C, Compare = 0x2D, Screen9 = 0x2E, Bank9 = 0x2F,
    Track8 = 0x30, MakeDefaultPreset = 0x32, SelectSound = 0x34, EditSound = 0x35, Screen3 = 0x36,
    Bank3 = 0x37, EditSeqPreset = 0x38, BankSet = 0x39,
};

struct ButtonName {
    Button button;
    const char* name;  // the enumerator in upper case, as ks32emu --press takes it
};

inline constexpr ButtonName kButtons[] = {
    {Button::Bank0, "BANK0"}, {Button::Bank1, "BANK1"}, {Button::Bank2, "BANK2"}, {Button::Bank3, "BANK3"},
    {Button::Bank4, "BANK4"}, {Button::Bank5, "BANK5"}, {Button::Bank6, "BANK6"}, {Button::Bank7, "BANK7"},
    {Button::Bank8, "BANK8"}, {Button::Bank9, "BANK9"},
    {Button::Screen0, "SCREEN0"}, {Button::Screen1, "SCREEN1"}, {Button::Screen2, "SCREEN2"},
    {Button::Screen3, "SCREEN3"}, {Button::Screen4, "SCREEN4"}, {Button::Screen5, "SCREEN5"},
    {Button::Screen6, "SCREEN6"}, {Button::Screen7, "SCREEN7"}, {Button::Screen8, "SCREEN8"},
    {Button::Screen9, "SCREEN9"},
    {Button::Track1, "TRACK1"}, {Button::Track2, "TRACK2"}, {Button::Track3, "TRACK3"}, {Button::Track4, "TRACK4"},
    {Button::Track5, "TRACK5"}, {Button::Track6, "TRACK6"}, {Button::Track7, "TRACK7"}, {Button::Track8, "TRACK8"},
    {Button::UpYes, "UP_YES"}, {Button::DownNo, "DOWN_NO"}, {Button::Left, "LEFT"}, {Button::Right, "RIGHT"},
    {Button::Compare, "COMPARE"}, {Button::EnterSave, "ENTER_SAVE"},
    {Button::EditSound, "EDIT_SOUND"}, {Button::BankSet, "BANK_SET"}, {Button::SelectSound, "SELECT_SOUND"},
    {Button::EditSeqPreset, "EDIT_SEQ_PRESET"}, {Button::EditTrack, "EDIT_TRACK"},
    {Button::SelectSeqPreset, "SELECT_SEQ_PRESET"}, {Button::ReplaceTrackSound, "REPLACE_TRACK_SOUND"},
    {Button::MakeDefaultPreset, "MAKE_DEFAULT_PRESET"}, {Button::SystemMidi, "SYSTEM_MIDI"},
};

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
    void button(Button b, bool down);  // queues the press or release bytes for the firmware
    // Queues a key event: note 28-103 (others are ignored); strike is the value the keyboard
    // sends, 1-127 (clamped), which the firmware maps through its touch curve; it is not a MIDI velocity.
    void key(int note, bool down, uint8_t strike);
    const PanelState& state() const { return s_; }
    std::vector<uint8_t> takeToCpu();  // the queued bytes, oldest first (no start-up handshake, K7)

private:
    void run(uint8_t cmd, uint8_t param);

    PanelState s_;
    uint8_t cursor_ = 0;    // next write position, 0..31
    std::array<uint8_t, 2> saved_{};  // cursor slots of 0x88/0x89
    std::vector<uint8_t> toCpu_;
    bool blinkOn_ = false;  // inside 0x90 ... 0x91
    uint8_t cmd_ = 0;       // command waiting for parameter bytes (0 = none)
    uint8_t param_ = 0;     // its first parameter
    int got_ = 0, need_ = 0;
};

}  // namespace ens::ks32
