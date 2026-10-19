#include "check.h"
#include "esp/esp_mac.h"

using namespace ens::esp;

TEST(mac_fractional_half_times_half) {
    Mac m;
    m.accumulate(m.product(0x400000, 0x400000, true), false);
    CHECK_EQ(m.readHigh(), 0x200000u);
    CHECK_EQ(m.readLow(), 0u);
}

TEST(mac_shift_two) {
    Mac m;
    m.accumulate(m.product(0x400000, 0x400000, false), false);
    CHECK_EQ(m.readHigh(), 0x400000u);
    CHECK_EQ(m.readLow(), 0u);
}

TEST(mac_neg_full_scale_squared_saturates) {
    Mac m;
    m.accumulate(m.product(0x800000, 0x800000, true), false);
    CHECK_EQ(m.raw(), int64_t(1) << 47);  // +1.0 does not fit 48 bits, lives in a guard bit
    CHECK_EQ(m.readHigh(), 0x7FFFFFu);
    CHECK_EQ(m.readLow(), 0xFFFFFFu);
}

TEST(mac_guard_bits_recover) {
    // R3: +0.9 = product(0x733333, 0x7FFFFF); -0.9 = product(0x8CCCCD, 0x7FFFFF); shiftOne.
    // One term = 0x733333*2^24 - 0x733333*2 = 0x733332:19999A (0.9 * 0x7FFFFF, truncated).
    Mac m;
    const int64_t plus = m.product(0x733333, 0x7FFFFF, true);
    const int64_t minus = m.product(0x8CCCCD, 0x7FFFFF, true);
    CHECK_EQ(minus, -plus);
    bool sawSaturated = false;

    m.accumulate(plus, false);
    CHECK_EQ(m.readHigh(), 0x733332u);
    m.accumulate(plus, true);
    sawSaturated = sawSaturated || m.readHigh() == 0x7FFFFFu;
    m.accumulate(plus, true);
    CHECK_EQ(m.readHigh(), 0x7FFFFFu);  // 2.7 is outside the 24-bit range
    CHECK_EQ(m.readLow(), 0xFFFFFFu);
    sawSaturated = sawSaturated || m.readHigh() == 0x7FFFFFu;
    m.accumulate(minus, true);
    sawSaturated = sawSaturated || m.readHigh() == 0x7FFFFFu;  // 1.8, still saturated
    m.accumulate(minus, true);
    CHECK(sawSaturated);
    CHECK_EQ(m.readHigh(), 0x733332u);  // 0x733333 minus 1 LSB of product truncation
    CHECK_EQ(m.readLow(), 0x19999Au);
}

TEST(mac_wraps_at_50_bits) {
    // Largest product: 0x800000^2 << 2 = 2^48. Accumulator range is [-2^49, 2^49-1].
    Mac m;
    const int64_t p = m.product(0x800000, 0x800000, false);
    CHECK_EQ(p, int64_t(1) << 48);
    m.accumulate(p, false);
    CHECK_EQ(m.raw(), int64_t(1) << 48);
    CHECK_EQ(m.readHigh(), 0x7FFFFFu);
    m.accumulate(p, true);  // 2^49 wraps to -2^49
    CHECK_EQ(m.raw(), -(int64_t(1) << 49));
    CHECK_EQ(m.readHigh(), 0x800000u);  // saturates by the wrapped (negative) value
    CHECK_EQ(m.readLow(), 0u);
    m.accumulate(p, true);  // -2^49 + 2^48
    CHECK_EQ(m.raw(), -(int64_t(1) << 48));
    CHECK_EQ(m.readHigh(), 0x800000u);
    m.accumulate(p, true);  // -2^48 + 2^48 = 0
    CHECK_EQ(m.raw(), 0);
    CHECK_EQ(m.readHigh(), 0u);
}

TEST(mac_preload) {
    Mac m;
    m.writeHigh(0x100000);
    m.writeLow(0x000001);
    m.accumulate(0, true);
    CHECK_EQ(m.readHigh(), 0x100000u);
    CHECK_EQ(m.readLow(), 1u);
    // Negative preload sign-extends into the guard bits and survives accumulate.
    m.writeHigh(0x800000);
    CHECK_EQ(m.raw(), (-(int64_t(1) << 47)) | 1);
    m.accumulate(0, true);
    CHECK_EQ(m.readHigh(), 0x800000u);
    CHECK_EQ(m.readLow(), 1u);
    // writeLow keeps the upper half; writeHigh keeps the lower half.
    m.writeLow(0xABCDEF);
    CHECK_EQ(m.readHigh(), 0x800000u);
    CHECK_EQ(m.readLow(), 0xABCDEFu);
    m.writeHigh(0x123456);
    CHECK_EQ(m.readLow(), 0xABCDEFu);
}

TEST(mac_plain_multiply_discards_preload) {
    Mac m;
    m.writeHigh(0x100000);
    m.writeLow(0x000123);
    const int64_t p = m.product(0x400000, 0x400000, true);
    m.accumulate(p, false);
    CHECK_EQ(m.raw(), p);
    CHECK_EQ(m.readHigh(), 0x200000u);
    CHECK_EQ(m.readLow(), 0u);
}
