#pragma once
#include <cstdint>

namespace ens {

// Sign-extend the low `bits` bits of v (1 <= bits <= 32) to a 32-bit signed value.
inline int32_t sext(uint32_t v, int bits) {
    const int s = 32 - bits;
    return static_cast<int32_t>(v << s) >> s;
}

// Clamp v to the signed range of a `bits`-wide integer (1 <= bits <= 32).
inline int32_t sat(int64_t v, int bits) {
    const int64_t hi = (int64_t(1) << (bits - 1)) - 1;
    const int64_t lo = -hi - 1;
    return static_cast<int32_t>(v > hi ? hi : (v < lo ? lo : v));
}

// Keep the low 24 bits (two's-complement wrap into the ESP 24-bit word).
inline uint32_t mask24(int64_t v) {
    return static_cast<uint32_t>(v & 0xFFFFFF);
}

}  // namespace ens
