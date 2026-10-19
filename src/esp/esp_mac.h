#pragma once
// ES5510 ESP multiplier/accumulator: 24x24 multiplier feeding a 50-bit accumulator
// (48 bits + 2 guard bits). Plain value type; the Esp core owns one.
#include <cstdint>

namespace ens::esp {

class Mac {
public:
    // sext24(x) * sext24(y), shifted left by 1 (shiftOne) or 2.
    int64_t product(uint32_t x24, uint32_t y24, bool shiftOne) const;
    // acc = wrap50((mac ? acc : 0) + product). A plain multiply (mac=false) discards
    // any preloaded value. The caller does not call this for a skipped step.
    void accumulate(int64_t product, bool mac);

    // Saturated readout (24-bit): MACH = sat48(acc) >> 24, MACL = sat48(acc) & 0xFFFFFF.
    uint32_t readHigh() const;
    uint32_t readLow() const;

    // ALU preload. writeHigh replaces the upper half (sign-extended into the guard bits).
    void writeHigh(uint32_t v);
    void writeLow(uint32_t v);

    int64_t raw() const { return acc_; }  // 50-bit signed accumulator, for tests

private:
    int64_t acc_ = 0;
};

}  // namespace ens::esp
