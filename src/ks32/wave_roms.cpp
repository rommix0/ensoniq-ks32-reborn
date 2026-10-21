#include "ks32/wave_roms.h"

namespace ens::ks32 {

namespace {

constexpr size_t kByteRomSize = 524288;   // u25, u26: 512 KiB each
constexpr size_t kWordRomSize = 1048576;  // rom2, rom3: 1 MiB each
constexpr size_t kHalfBank = 0x80000;     // words per ROM half; the second ROM starts here

bool checkSize(const std::vector<uint8_t>& rom, size_t want, const char* name, std::string& err) {
    if (rom.size() == want) return true;
    err = std::string(name) + ": expected " + std::to_string(want) + " bytes, got " +
          std::to_string(rom.size());
    return false;
}

}  // namespace

bool buildWaveBanks(const std::vector<uint8_t>& u25, const std::vector<uint8_t>& u26,
                    const std::vector<uint8_t>& rom2, const std::vector<uint8_t>& rom3,
                    std::vector<uint16_t>& bank0, std::vector<uint16_t>& bank1, std::string& err) {
    if (!checkSize(u25, kByteRomSize, "u25", err) || !checkSize(u26, kByteRomSize, "u26", err) ||
        !checkSize(rom2, kWordRomSize, "rom2", err) || !checkSize(rom3, kWordRomSize, "rom3", err))
        return false;
    bank0.assign(2 * kHalfBank, 0);
    bank1.assign(2 * kHalfBank, 0);
    for (size_t i = 0; i < kByteRomSize; ++i) {
        bank0[i] = static_cast<uint16_t>(u25[i] << 8);
        bank0[kHalfBank + i] = static_cast<uint16_t>(u26[i] << 8);
    }
    for (size_t i = 0; i < kHalfBank; ++i) {
        bank1[i] = static_cast<uint16_t>(rom2[2 * i] | (rom2[2 * i + 1] << 8));
        bank1[kHalfBank + i] = static_cast<uint16_t>(rom3[2 * i] | (rom3[2 * i + 1] << 8));
    }
    return true;
}

}  // namespace ens::ks32
