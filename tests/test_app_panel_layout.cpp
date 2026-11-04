// The front-panel layout table against the artboard's rules: every button, LED and key once,
// everything inside 2040 x 720, no overlapping controls, and each LED beside its button.
#include <algorithm>
#include <cstdio>
#include <limits>

#include "app/analog_map.h"
#include "app/panel_layout.h"
#include "check.h"
#include "ks32/panel.h"

using namespace ens::app;
using ens::ks32::Button;
using ens::ks32::kButtons;

namespace {

int count(ControlKind kind, int id) {
    int n = 0;
    for (const Control& c : panelLayout()) n += c.kind == kind && c.id == id;
    return n;
}

const Control* find(ControlKind kind, int id) {
    for (const Control& c : panelLayout())
        if (c.kind == kind && c.id == id) return &c;
    return nullptr;
}

bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool isBlack(int note) {
    const int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

// R5: the gap between two rects, 0 when they touch or overlap.
int gap(const Rect& a, const Rect& b) {
    const int dx = std::max({0, b.x - (a.x + a.w), a.x - (b.x + b.w)});
    const int dy = std::max({0, b.y - (a.y + a.h), a.y - (b.y + b.h)});
    return std::max(dx, dy);
}

}  // namespace

TEST(panel_layout_has_every_button) {
    for (const auto& b : kButtons) {
        const int n = count(ControlKind::Button, static_cast<int>(b.button));
        if (n != 1) std::printf("    %s appears %d times\n", b.name, n);
        CHECK_EQ(n, 1);
    }
}

TEST(panel_layout_has_every_led_and_key) {
    for (int led = 0; led < ens::ks32::kPanelLeds; ++led) CHECK_EQ(count(ControlKind::Led, led), 1);
    for (int note = 28; note <= 103; ++note) CHECK_EQ(count(ControlKind::Key, note), 1);
    int keys = 0;
    for (const Control& c : panelLayout()) keys += c.kind == ControlKind::Key;
    CHECK_EQ(keys, 76);
}

TEST(panel_layout_fits_and_buttons_do_not_overlap) {
    const auto& t = panelLayout();
    CHECK(!t.empty());
    for (const Control& c : t) {
        CHECK(c.r.w > 0 && c.r.h > 0);
        CHECK(c.r.x >= 0 && c.r.y >= 0 && c.r.x + c.r.w <= kDesignW && c.r.y + c.r.h <= kDesignH);
    }
    auto solid = [](const Control& c) {
        return c.kind == ControlKind::Button || c.kind == ControlKind::Slider || c.kind == ControlKind::Wheel ||
               c.kind == ControlKind::Key;
    };
    for (size_t i = 0; i < t.size(); ++i) {
        for (size_t j = i + 1; j < t.size(); ++j) {
            const Control &a = t[i], &b = t[j];
            if (!solid(a) || !solid(b) || !overlap(a.r, b.r)) continue;
            // A black key may cover the white keys beside it.
            if (a.kind == ControlKind::Key && b.kind == ControlKind::Key && isBlack(a.id) != isBlack(b.id)) continue;
            std::printf("    overlap: %s and %s\n", a.label, b.label);
            CHECK(false);
        }
    }
}

TEST(panel_layout_led_matches_button) {
    // The LED map from Plan 6 (docs/ks32-hardware-interface.md, "Front panel").
    const Button byLed[ens::ks32::kPanelLeds] = {
        Button::EditSound, Button::SelectSeqPreset, Button::Track1, Button::Track2, Button::Track3,
        Button::Track4, Button::EditTrack, Button::SystemMidi, Button::SelectSound, Button::EditSeqPreset,
        Button::Track5, Button::Track6, Button::Track7, Button::Track8, Button::ReplaceTrackSound,
    };
    for (int led = 0; led < ens::ks32::kPanelLeds; ++led) {
        const Control* l = find(ControlKind::Led, led);
        const Control* b = find(ControlKind::Button, static_cast<int>(byLed[led]));
        CHECK(l != nullptr && b != nullptr);
        if (l == nullptr || b == nullptr) continue;
        if (gap(l->r, b->r) > 40) std::printf("    LED %d is %d from %s\n", led, gap(l->r, b->r), b->label);
        CHECK(gap(l->r, b->r) <= 40);
        // Nearer to its own button than to any other.
        for (const Control& c : panelLayout())
            if (c.kind == ControlKind::Button && &c != b) CHECK(gap(l->r, c.r) >= gap(l->r, b->r));
    }
}

TEST(analog_map_round_trips_rest_values) {
    using ens::ks32::Analog;
    using ens::ks32::kAnalogRest;
    for (const Analog a : {Analog::PitchWheel, Analog::ModWheel, Analog::Volume, Analog::DataEntry}) {
        const uint16_t rest = kAnalogRest[static_cast<size_t>(a)];
        CHECK(analogRaw(a, analogPosition(a, rest)) == rest);
    }
    CHECK(analogRaw(Analog::PitchWheel, 0.5f) == 0x200);
    CHECK(analogRaw(Analog::ModWheel, 0.0f) == 0x3FF);
    CHECK(analogRaw(Analog::Volume, 1.0f) == 0x3FF);
    CHECK(analogRaw(Analog::Volume, 0.0f) == 0);
}

// A value from a project file is not trusted: NaN gives the rest value, an infinity clamps.
TEST(analog_map_non_finite) {
    using ens::ks32::Analog;
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(analogRaw(Analog::PitchWheel, std::numeric_limits<float>::quiet_NaN()) == 0x200);
    CHECK(analogRaw(Analog::Volume, std::numeric_limits<float>::quiet_NaN()) == 0x3FF);
    CHECK(analogRaw(Analog::Volume, inf) == 0x3FF);
    CHECK(analogRaw(Analog::Volume, -inf) == 0);
    CHECK(analogRaw(Analog::PitchWheel, inf) == 0);
}
