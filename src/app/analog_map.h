#pragma once
// Host parameter position (0..1) <-> raw analog value (0..0x3FF). The wheels read inverted
// (docs/ks32-hardware-interface.md, "Analog inputs"): position 1 is raw 0. Header-only; float is
// allowed here (the engine and the machine stay integer).
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "ks32/machine.h"

namespace ens::app {

inline bool analogInverted(ks32::Analog a) { return a == ks32::Analog::PitchWheel || a == ks32::Analog::ModWheel; }

// DECISION A17: lround(position x 1023), the wheels taken from 1 - position, clamped to 0..0x3FF.
// The position may come from a project file: NaN gives the rest value (K13), an infinity clamps.
inline uint16_t analogRaw(ks32::Analog a, float position) {
    if (std::isnan(position)) return ks32::kAnalogRest[static_cast<size_t>(a)];
    const float p = position < 0.0f ? 0.0f : position > 1.0f ? 1.0f : position;
    return static_cast<uint16_t>(std::lround((analogInverted(a) ? 1.0f - p : p) * 1023.0f));
}

inline float analogPosition(ks32::Analog a, uint16_t raw) {
    const float p = static_cast<float>(raw > 0x3FF ? 0x3FF : raw) / 1023.0f;
    return analogInverted(a) ? 1.0f - p : p;
}

}  // namespace ens::app
