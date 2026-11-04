// The "Front panel" artboard transcribed: the rects are the artboard's boxes as a browser lays out
// its inline styles (fonts loaded), rounded to whole design units. Two changes, both from design
// section 6.3: the status lines sit 40 units higher to make room for the gear button under them,
// and the keyboard is the script's 1826 units wide at the bottom panel's left padding plus the
// wheel box (the browser shrinks it by one unit to fit).
#include "app/panel_layout.h"

#include <cmath>

#include "ks32/machine.h"
#include "ks32/panel.h"

namespace ens::app {

namespace {

using K = ControlKind;
using ks32::Analog;
using ks32::Button;

constexpr int B(Button b) { return static_cast<int>(b); }
constexpr int A(Analog a) { return static_cast<int>(a); }

const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr bool isBlack(int note) {
    const int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

// The keyboard: E1-G7 as the artboard's script computes it, white key 1826 / 45 wide, black keys
// 0.6 of that and 62 % as long, centred on the boundary between their white keys.
constexpr int kKeysX = 182, kKeysY = 398, kKeysW = 1826, kKeysH = 296;

std::vector<Control> build() {
    std::vector<Control> t = {
        // Wordmark column: the gear sits under the status lines (design section 6.3).
        // The model decal, after the instrument's: a tall thin-stroke mark and its green line.
        {K::Label, -1, {32, 30, 150, 62}, "KS-32", Ink::Decal},
        {K::Label, -1, {32, 100, 150, 13}, "Weighted Action MIDI Studio", Ink::Decal},
        {K::Label, -1, {32, 278, 49, 15}, "ROM v3.01", Ink::Dim},
        {K::Label, -1, {32, 294, 47, 15}, "MIDI: Q25", Ink::Dim},
        {K::Button, kSettingsId, {32, 322, 34, 34}, "Settings"},

        // Sliders: the rect is the track; the cap is drawn from the value.
        {K::Slider, A(Analog::Volume), {199, 105, 28, 212}, "Volume"},
        {K::Slider, A(Analog::DataEntry), {265, 105, 28, 212}, "Data Entry"},
        {K::Label, -1, {195, 327, 37, 15}, "Volume"},
        {K::Label, -1, {254, 327, 51, 15}, "Data Entry"},

        // Display and the cursor and entry buttons.
        {K::Display, -1, {347, 28, 330, 104}, "Display"},
        {K::Button, B(Button::UpYes), {446, 172, 50, 32}, "Up / Yes"},
        {K::Button, B(Button::Left), {388, 227, 50, 32}, "Left"},
        {K::Button, B(Button::DownNo), {446, 227, 50, 32}, "Down / No"},
        {K::Button, B(Button::Right), {504, 227, 50, 32}, "Right"},
        {K::Button, B(Button::Compare), {580, 178, 56, 32}, "Compare"},
        {K::Button, B(Button::EnterSave), {580, 242, 56, 32}, "Enter / Save"},
        {K::Label, -1, {453, 154, 37, 15}, "Up / Yes"},
        {K::Label, -1, {405, 210, 17, 15}, "Left"},
        {K::Label, -1, {518, 210, 23, 15}, "Right"},
        {K::Label, -1, {448, 265, 47, 15}, "Down / No"},
        {K::Label, -1, {588, 160, 41, 15}, "Compare"},
        {K::Label, -1, {582, 224, 54, 15}, "Enter / Save"},

        // Sound column.
        {K::Button, B(Button::EditSound), {720, 57, 60, 40}, "Edit Sound"},
        {K::Button, B(Button::BankSet), {720, 174, 60, 32}, "BankSet"},
        {K::Button, B(Button::SelectSound), {720, 308, 60, 40}, "Select Sound"},
        {K::Led, 0, {720, 37, 9, 9}, "Edit Sound"},
        {K::Led, 8, {720, 278, 9, 9}, "Select Sound"},
        {K::Label, -1, {735, 32, 68, 19}, "Edit Sound", Ink::Bright},
        {K::Label, -1, {720, 155, 41, 15}, "BankSet"},
        {K::Label, -1, {735, 263, 39, 19}, "Select", Ink::Bright},
        {K::Label, -1, {735, 283, 40, 19}, "Sound", Ink::Bright},

        // Strip labels (the two outlined rows), banks and screens.
        {K::Label, -1, {1190, 27, 26, 13}, "Tracks"},
        {K::Label, -1, {842, 56, 27, 13}, "Locate"},
        {K::Label, -1, {899, 56, 30, 13}, "Control"},
        {K::Label, -1, {962, 56, 20, 13}, "Click"},
        {K::Label, -1, {1020, 56, 20, 13}, "Song"},
        {K::Label, -1, {1080, 56, 15, 13}, "Seq"},
        {K::Label, -1, {1134, 56, 23, 13}, "Event"},
        {K::Label, -1, {1186, 56, 36, 13}, "Quantize"},
        {K::Label, -1, {1252, 56, 21, 13}, "Mute"},
        {K::Label, -1, {1308, 56, 24, 13}, "Effect"},
        {K::Label, -1, {1367, 56, 22, 13}, "Store"},
        {K::Label, -1, {1368, 68, 20, 13}, "Seqs"},
        {K::Label, -1, {845, 101, 22, 13}, "Wave"},
        {K::Label, -1, {904, 101, 21, 13}, "Pitch"},
        {K::Label, -1, {962, 101, 20, 13}, "Env1"},
        {K::Label, -1, {1022, 101, 16, 13}, "LFO"},
        {K::Label, -1, {1075, 101, 26, 13}, "Filters"},
        {K::Label, -1, {1136, 101, 20, 13}, "Env2"},
        {K::Label, -1, {1194, 101, 19, 13}, "Amp"},
        {K::Label, -1, {1248, 101, 28, 13}, "Output"},
        {K::Label, -1, {1308, 101, 24, 13}, "Effect"},
        {K::Label, -1, {1367, 101, 22, 13}, "Store"},
        {K::Label, -1, {1363, 113, 30, 13}, "Sounds"},
        {K::Button, B(Button::Bank0), {830, 162, 50, 32}, "Bank 0"},
        {K::Button, B(Button::Bank1), {888, 162, 50, 32}, "Bank 1"},
        {K::Button, B(Button::Bank2), {946, 162, 50, 32}, "Bank 2"},
        {K::Button, B(Button::Bank3), {1004, 162, 50, 32}, "Bank 3"},
        {K::Button, B(Button::Bank4), {1062, 162, 50, 32}, "Bank 4"},
        {K::Button, B(Button::Bank5), {1120, 162, 50, 32}, "Bank 5"},
        {K::Button, B(Button::Bank6), {1178, 162, 50, 32}, "Bank 6"},
        {K::Button, B(Button::Bank7), {1236, 162, 50, 32}, "Bank 7"},
        {K::Button, B(Button::Bank8), {1294, 162, 50, 32}, "Bank 8"},
        {K::Button, B(Button::Bank9), {1352, 162, 50, 32}, "Bank 9"},
        {K::Label, -1, {1306, 147, 27, 13}, "Drums"},
        {K::Label, -1, {1364, 147, 27, 13}, "Drums"},
        {K::Label, -1, {839, 197, 33, 13}, "Internal", Ink::Dim},
        {K::Label, -1, {900, 197, 27, 13}, "ROM A", Ink::Dim},
        {K::Label, -1, {958, 197, 27, 13}, "ROM B", Ink::Dim},
        {K::Label, -1, {1016, 197, 27, 13}, "Card A", Ink::Dim},
        {K::Label, -1, {1074, 197, 27, 13}, "Card B", Ink::Dim},
        {K::Button, B(Button::Screen0), {830, 236, 50, 32}, "Screen 0"},
        {K::Button, B(Button::Screen1), {888, 236, 50, 32}, "Screen 1"},
        {K::Button, B(Button::Screen2), {946, 236, 50, 32}, "Screen 2"},
        {K::Button, B(Button::Screen3), {1004, 236, 50, 32}, "Screen 3"},
        {K::Button, B(Button::Screen4), {1062, 236, 50, 32}, "Screen 4"},
        {K::Button, B(Button::Screen5), {1120, 236, 50, 32}, "Screen 5"},
        {K::Button, B(Button::Screen6), {1178, 236, 50, 32}, "Screen 6"},
        {K::Button, B(Button::Screen7), {1236, 236, 50, 32}, "Screen 7"},
        {K::Button, B(Button::Screen8), {1294, 236, 50, 32}, "Screen 8"},
        {K::Button, B(Button::Screen9), {1352, 236, 50, 32}, "Screen 9"},
        {K::Label, -1, {852, 217, 7, 17}, "0", Ink::Bright},
        {K::Label, -1, {910, 217, 7, 17}, "1", Ink::Bright},
        {K::Label, -1, {968, 217, 7, 17}, "2", Ink::Bright},
        {K::Label, -1, {1026, 217, 7, 17}, "3", Ink::Bright},
        {K::Label, -1, {1084, 217, 7, 17}, "4", Ink::Bright},
        {K::Label, -1, {1142, 217, 7, 17}, "5", Ink::Bright},
        {K::Label, -1, {1200, 217, 7, 17}, "6", Ink::Bright},
        {K::Label, -1, {1258, 217, 7, 17}, "7", Ink::Bright},
        {K::Label, -1, {1316, 217, 7, 17}, "8", Ink::Bright},
        {K::Label, -1, {1374, 217, 7, 17}, "9", Ink::Bright},
        {K::Label, -1, {840, 270, 31, 13}, "Volume", Ink::Dim},
        {K::Label, -1, {848, 281, 15, 13}, "Pan", Ink::Dim},
        {K::Label, -1, {899, 270, 28, 13}, "Output", Ink::Dim},
        {K::Label, -1, {964, 270, 15, 13}, "Key", Ink::Dim},
        {K::Label, -1, {959, 281, 25, 13}, "Range", Ink::Dim},
        {K::Label, -1, {1018, 270, 23, 13}, "X-pos", Ink::Dim},
        {K::Label, -1, {1071, 270, 33, 13}, "Channel", Ink::Dim},
        {K::Label, -1, {1070, 281, 34, 13}, "Program", Ink::Dim},
        {K::Label, -1, {1135, 270, 21, 13}, "MIDI", Ink::Dim},
        {K::Label, -1, {1132, 281, 26, 13}, "Status", Ink::Dim},
        {K::Label, -1, {1185, 270, 36, 13}, "Pressure", Ink::Dim},
        {K::Label, -1, {1193, 281, 20, 13}, "Type", Ink::Dim},
        {K::Label, -1, {1246, 270, 30, 13}, "Sustain", Ink::Dim},
        {K::Label, -1, {1250, 281, 23, 13}, "Pedal", Ink::Dim},
        {K::Label, -1, {1305, 270, 29, 13}, "Timbre", Ink::Dim},
        {K::Label, -1, {1361, 270, 32, 13}, "Release", Ink::Dim},

        // Sequencer column.
        {K::Button, B(Button::EditSeqPreset), {1446, 70, 60, 40}, "Edit Seq/Preset"},
        {K::Button, B(Button::EditTrack), {1446, 189, 60, 32}, "Edit Track"},
        {K::Button, B(Button::SelectSeqPreset), {1446, 300, 60, 32}, "Select Seq/Preset"},
        {K::Led, 9, {1446, 44, 9, 9}, "Edit Seq/Preset"},
        {K::Led, 6, {1446, 171, 9, 9}, "Edit Track"},
        {K::Led, 1, {1446, 282, 9, 9}, "Select Seq/Preset"},
        {K::Label, -1, {1461, 31, 23, 19}, "Edit", Ink::Bright},
        {K::Label, -1, {1461, 47, 66, 19}, "Seq/Preset", Ink::Bright},
        {K::Label, -1, {1461, 168, 48, 15}, "Edit Track"},
        {K::Label, -1, {1461, 279, 87, 15}, "Select Seq/Preset"},

        // Tracks and transport.
        {K::Label, -1, {1695, 32, 32, 15}, "Tracks"},
        {K::Button, B(Button::Track1), {1607, 83, 46, 32}, "Track 1"},
        {K::Button, B(Button::Track2), {1661, 83, 46, 32}, "Track 2"},
        {K::Button, B(Button::Track3), {1715, 83, 46, 32}, "Track 3"},
        {K::Button, B(Button::Track4), {1769, 83, 46, 32}, "Track 4"},
        {K::Button, B(Button::Track5), {1607, 145, 46, 32}, "Track 5"},
        {K::Button, B(Button::Track6), {1661, 145, 46, 32}, "Track 6"},
        {K::Button, B(Button::Track7), {1715, 145, 46, 32}, "Track 7"},
        {K::Button, B(Button::Track8), {1769, 145, 46, 32}, "Track 8"},
        {K::Led, 2, {1621, 68, 8, 8}, "Track 1"},
        {K::Led, 3, {1675, 68, 8, 8}, "Track 2"},
        {K::Led, 4, {1729, 68, 8, 8}, "Track 3"},
        {K::Led, 5, {1783, 68, 8, 8}, "Track 4"},
        {K::Led, 10, {1621, 129, 8, 8}, "Track 5"},
        {K::Led, 11, {1675, 129, 8, 8}, "Track 6"},
        {K::Led, 12, {1729, 129, 8, 8}, "Track 7"},
        {K::Led, 13, {1783, 129, 8, 8}, "Track 8"},
        {K::Label, -1, {1633, 64, 6, 15}, "1", Ink::Bright},
        {K::Label, -1, {1687, 64, 6, 15}, "2", Ink::Bright},
        {K::Label, -1, {1741, 64, 6, 15}, "3", Ink::Bright},
        {K::Label, -1, {1795, 64, 6, 15}, "4", Ink::Bright},
        {K::Label, -1, {1633, 125, 6, 15}, "5", Ink::Bright},
        {K::Label, -1, {1687, 125, 6, 15}, "6", Ink::Bright},
        {K::Label, -1, {1741, 125, 6, 15}, "7", Ink::Bright},
        {K::Label, -1, {1795, 125, 6, 15}, "8", Ink::Bright},
        {K::Button, kRecordId, {1607, 217, 56, 32}, "Record"},
        {K::Button, B(Button::StopContinue), {1671, 217, 56, 32}, "Stop / Continue"},
        {K::Button, B(Button::Play), {1735, 217, 56, 32}, "Play"},
        {K::Label, -1, {1619, 199, 32, 15}, "Record"},
        {K::Label, -1, {1689, 199, 21, 15}, "Stop"},
        {K::Label, -1, {1681, 253, 36, 13}, "Continue", Ink::Dim},
        {K::Label, -1, {1754, 199, 19, 15}, "Play"},

        // Right column.
        {K::Button, B(Button::ReplaceTrackSound), {1858, 67, 60, 32}, "Replace Track Sound"},
        {K::Button, B(Button::MakeDefaultPreset), {1858, 188, 60, 32}, "Make Default Preset"},
        {K::Button, B(Button::SystemMidi), {1858, 300, 60, 32}, "System / MIDI"},
        {K::Led, 14, {1858, 35, 9, 9}, "Replace Track Sound"},
        {K::Led, 7, {1858, 280, 9, 9}, "System / MIDI"},
        {K::Label, -1, {1873, 31, 102, 15}, "Replace Track Sound"},
        {K::Label, -1, {1873, 47, 62, 13}, "(Replace w/FX)", Ink::Dim},
        {K::Label, -1, {1858, 153, 98, 15}, "Make Default Preset"},
        {K::Label, -1, {1858, 169, 58, 13}, "(Clone Preset)", Ink::Dim},
        {K::Label, -1, {1873, 275, 85, 19}, "System / MIDI", Ink::Bright},

        // Wheel box (R2: wheel ids are ks32::Analog).
        {K::Wheel, A(Analog::PitchWheel), {68, 474, 28, 122}, "Pitch wheel"},
        {K::Wheel, A(Analog::ModWheel), {118, 474, 28, 122}, "Mod wheel"},
        {K::Label, -1, {71, 603, 25, 15}, "Pitch"},
        {K::Label, -1, {122, 603, 21, 15}, "Mod"},
    };

    // Keys: whites first, so the blacks draw over them. The names outlive the table (static).
    static char names[ks32::kKeyCount][5];
    const double ww = kKeysW / 45.0, bw = ww * 0.6;
    const int bh = static_cast<int>(std::lround(kKeysH * 0.62));
    std::vector<Control> blacks;
    int wi = 0;
    for (int note = ks32::kLowestKey; note < ks32::kLowestKey + ks32::kKeyCount; ++note) {
        char* name = names[note - ks32::kLowestKey];
        const char* n = kNoteNames[note % 12];
        int len = 0;
        while (*n) name[len++] = *n++;
        name[len++] = static_cast<char>('0' + note / 12 - 1);
        name[len] = 0;
        if (isBlack(note)) {
            const int x = kKeysX + static_cast<int>(std::lround(wi * ww - bw / 2));
            blacks.push_back({K::Key, note, {x, kKeysY, static_cast<int>(std::lround(bw)), bh}, name});
        } else {
            const int x0 = static_cast<int>(std::lround(wi * ww)), x1 = static_cast<int>(std::lround((wi + 1) * ww));
            t.push_back({K::Key, note, {kKeysX + x0, kKeysY, x1 - x0, kKeysH}, name});
            ++wi;
        }
    }
    t.insert(t.end(), blacks.begin(), blacks.end());
    return t;
}

}  // namespace

const std::vector<Control>& panelLayout() {
    static const std::vector<Control> t = build();
    return t;
}

}  // namespace ens::app
