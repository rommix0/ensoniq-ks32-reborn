#pragma once
// KS-32 wave ROM layout: builds the two OTIS wave banks from the four ROM file images.
// Pure functions on byte vectors; reading the files is the caller's job (src/ has no file I/O).
#include <cstdint>
#include <string>
#include <vector>

namespace ens::ks32 {

// findings.md section A: u25/u26 hold signed 8-bit samples on D15..8 (D7..0 = 0), u26 at word
// 0x80000 of bank 0; rom2/rom3 are little-endian 16-bit words, rom3 at word 0x80000 of bank 1.
// Sizes: 524288 bytes (u25, u26), 1048576 bytes (rom2, rom3). On a wrong size returns false and
// sets `err` (naming the file); the banks are left untouched.
bool buildWaveBanks(const std::vector<uint8_t>& u25, const std::vector<uint8_t>& u26,
                    const std::vector<uint8_t>& rom2, const std::vector<uint8_t>& rom3,
                    std::vector<uint16_t>& bank0, std::vector<uint16_t>& bank1, std::string& err);

}  // namespace ens::ks32
