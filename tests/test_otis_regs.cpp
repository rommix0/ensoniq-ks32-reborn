#include "check.h"
#include "otis/otis.h"

using namespace ens;

namespace {
struct NullMem : WaveMemory {
    uint16_t read(int, uint32_t) override { return 0; }
};
}  // namespace

TEST(otis_reset_state) {
    NullMem m;
    Otis o(m);
    o.reset();
    o.write(15, 0);
    CHECK_EQ(o.read(0), 0xF003);
    CHECK_EQ(o.read(13), 0x001F);
    CHECK_EQ(o.read(14), 0x00E0);
    o.write(15, 64);
    CHECK_EQ(o.read(8), 0x07F8 | 1);
    CHECK(!o.irqAsserted());
    CHECK_EQ(o.activeVoices(), 32);
}

TEST(otis_page_selects_voice) {
    NullMem m;
    Otis o(m);
    o.write(15, 3);
    o.write(1, 0x1234);
    o.write(15, 4);
    o.write(1, 0x5678);
    o.write(15, 3);
    CHECK_EQ(o.read(1), 0x1234 & 0xFFFE);
    o.write(15, 4);
    CHECK_EQ(o.read(1), 0x5678 & 0xFFFE);
    CHECK_EQ(o.read(15), 4);
}

TEST(otis_masks) {
    NullMem m;
    Otis o(m);
    o.write(15, 0);
    o.write(2, 0xFFFF);
    CHECK_EQ(o.read(2), 0x1FFF);
    o.write(3, 0xFFFF);
    CHECK_EQ(o.read(3), 0xFFE0);
    o.write(4, 0xFFFF);
    CHECK_EQ(o.read(4), 0x1FFF);
    o.write(5, 0xFFFF);
    CHECK_EQ(o.read(5), 0xFFE0);
    o.write(6, 0xFFFF);
    CHECK_EQ(o.read(6), 0xFFF0);
    o.write(7, 0xFFFF);
    CHECK_EQ(o.read(7), 0xFFF0);
    o.write(8, 0xFFFF);
    CHECK_EQ(o.read(8), 0xFF00);
    o.write(9, 0xFFFF);
    CHECK_EQ(o.read(9), 0xFF00);
    o.write(10, 0xFFFF);
    CHECK_EQ(o.read(10), 0x1FFF);
    o.write(11, 0xFFFF);
    CHECK_EQ(o.read(11), 0xFFFF);
    o.write(0, 0xFFFF);
    CHECK_EQ(o.read(0), 0xFFFF);
    o.write(0, 0);
    CHECK_EQ(o.read(0), 0xF000);
    // Unlisted register 12 reads 0 and ignores writes (O9).
    o.write(12, 0xFFFF);
    CHECK_EQ(o.read(12), 0);
}

TEST(otis_filter_storage_page) {
    NullMem m;
    Otis o(m);
    o.write(15, 5);
    o.write(1, 0x1234);  // voice 5 FC
    o.write(15, 32 + 5);
    for (int r = 1; r <= 6; ++r) o.write(static_cast<uint8_t>(r), static_cast<uint16_t>(r * 0x1111));
    for (int r = 1; r <= 6; ++r) CHECK_EQ(o.read(static_cast<uint8_t>(r)), r * 0x1111);
    CHECK_EQ(o.read(7), 0);
    o.write(15, 5);
    CHECK_EQ(o.read(1), 0x1234 & 0xFFFE);
}

TEST(otis_global_regs_visible_on_all_pages) {
    NullMem m;
    Otis o(m);
    o.write(15, 70);
    o.write(13, 7);
    o.write(15, 0);
    CHECK_EQ(o.read(13), 7);
    CHECK_EQ(o.activeVoices(), 8);
    o.write(15, 40);
    CHECK_EQ(o.read(13), 7);
    CHECK_EQ(o.read(15), 40);
    CHECK_EQ(o.read(14), 0x00E0);
}

TEST(otis_ch_regs_test_mode) {
    NullMem m;
    Otis o(m);
    o.write(15, 64);
    // TEST=0: writes ignored, CH0 reads 0 (O11).
    o.write(8, 0x0000);  // A/D=0, TEST=0
    o.write(0, 0x4321);
    CHECK_EQ(o.read(0), 0);
    // TEST=1: write accepted but CH0-2 stay unreadable (O11); CH3 reads back.
    o.write(8, 0x0002);
    o.write(0, 0x4321);
    CHECK_EQ(o.read(0), 0);
    o.write(6, 0x4321);
    CHECK_EQ(o.read(6), 0x4321);
    // TEST=0 (and A/D=0): a channel write must not change the prior value.
    o.write(8, 0x0000);
    o.write(6, 0x4321);  // prior value
    o.write(6, 0x1111);
    CHECK_EQ(o.read(6), 0x4321);
    o.write(8, 0x0002);  // TEST=1: the same write takes effect
    o.write(6, 0x1111);
    CHECK_EQ(o.read(6), 0x1111);
    o.write(6, 0x4321);
    // A/D=1: serial input is latched into CH3L/R.
    o.write(8, 0x0001);
    o.setSerialIn(0x1234, -2);
    CHECK_EQ(o.read(6), 0x1234);
    CHECK_EQ(o.read(7), 0xFFFE);
    // A/D=0: serial input is ignored.
    o.write(8, 0x0000);
    o.setSerialIn(0x7777, 0x7777);
    CHECK_EQ(o.read(6), 0x1234);
}

TEST(otis_page_masking_and_decode) {
    NullMem m;
    Otis o(m);
    o.write(15, 0xFF);
    CHECK_EQ(o.read(15), 0x7F);
    // Page 96 (bits 5 and 6 set) resolves to the channel page: reg 8 is SERMODE, not a voice register.
    o.write(15, 96);
    CHECK_EQ(o.read(8), 0x07F8 | 1);
    o.write(8, 0x0002);
    o.write(6, 0x2468);
    CHECK_EQ(o.read(6), 0x2468);
    o.write(15, 64);  // same global channel registers as page 96
    CHECK_EQ(o.read(6), 0x2468);
}
