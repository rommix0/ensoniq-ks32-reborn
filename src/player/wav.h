#pragma once
// 16-bit PCM RIFF/WAVE writer (bytes only; the caller writes the file).
#include <cstdint>
#include <vector>

namespace ens::player {

// 44-byte header followed by the samples, little-endian. `interleaved` holds frames * channels samples.
std::vector<uint8_t> makeWav16(const std::vector<int16_t>& interleaved, int channels, uint32_t rate);

}  // namespace ens::player
