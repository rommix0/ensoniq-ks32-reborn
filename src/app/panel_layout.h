#pragma once
// The "Front panel" artboard (docs/design/mockups/ks32-app-front-panel.dc.html) as a table, in its
// 2040 x 720 design units: every control, LED and label with its rect. JUCE-free; the editor scales
// it uniformly to the window (A13).
#include <cstdint>
#include <vector>

namespace ens::app {

constexpr int kDesignW = 2040, kDesignH = 720;

struct Rect {
    int x, y, w, h;
};

enum class ControlKind : uint8_t { Button, Led, Slider, Wheel, Display, Key, Label };
// Label colour and weight, as the artboard draws them: #c9cbcf, bold #e6e7ea, #a7aab0; Decal is
// the model decal: the "KS-32" label is drawn as the stroked mark, any other in the decal's green.
enum class Ink : uint8_t { Text, Bright, Dim, Decal };

struct Control {
    ControlKind kind;
    // Button: ks32::Button code, kRecordId while unknown (A14), or kSettingsId (the gear).
    // Led: LED number 0-14. Slider and Wheel: ks32::Analog. Key: note 28-103. Display, Label: -1.
    int id;
    Rect r;  // Label: one line of text, as tall as its font (ascent plus descent)
    const char* label;  // Label: the text; the others: the name for tooltips and screen readers
    Ink ink = Ink::Text;
};

constexpr int kRecordId = -1;    // Record has no code (Task 3); drawn dimmed, does nothing
constexpr int kSettingsId = -4;  // the gear button that opens the settings page

const std::vector<Control>& panelLayout();  // built once, on first call (message thread)

}  // namespace ens::app
