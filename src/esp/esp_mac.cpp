#include "esp/esp_mac.h"

#include "common/bits.h"

namespace ens::esp {

namespace {

// Wrap to a 50-bit signed value: ((v << 14) >> 14), done unsigned to avoid UB on the shift.
int64_t wrap50(int64_t v) {
    return static_cast<int64_t>(static_cast<uint64_t>(v) << 14) >> 14;
}

// Clamp to the signed 48-bit range [-2^47, 2^47-1].
int64_t sat48(int64_t v) {
    const int64_t hi = (int64_t(1) << 47) - 1;
    const int64_t lo = -hi - 1;
    return v > hi ? hi : (v < lo ? lo : v);
}

}  // namespace

// DECISION E6: acc is a 50-bit wrapping signed value; readout clamps to 48 bits; ALU preload replaces a half.
int64_t Mac::product(uint32_t x24, uint32_t y24, bool shiftOne) const {
    const int64_t p = static_cast<int64_t>(sext(x24, 24)) * sext(y24, 24);
    return p * (shiftOne ? 2 : 4);  // multiply, not shift: p may be negative
}

void Mac::accumulate(int64_t product, bool mac) {
    acc_ = wrap50((mac ? acc_ : 0) + product);
}

uint32_t Mac::readHigh() const {
    return static_cast<uint32_t>(sat48(acc_) >> 24) & 0xFFFFFF;
}

uint32_t Mac::readLow() const {
    return static_cast<uint32_t>(sat48(acc_) & 0xFFFFFF);
}

void Mac::writeHigh(uint32_t v) {
    acc_ = static_cast<int64_t>(sext(v, 24)) * (int64_t(1) << 24) + (acc_ & 0xFFFFFF);
}

void Mac::writeLow(uint32_t v) {
    acc_ = (acc_ & ~int64_t(0xFFFFFF)) | (v & 0xFFFFFF);
}

}  // namespace ens::esp
