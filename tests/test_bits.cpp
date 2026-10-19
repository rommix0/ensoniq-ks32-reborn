#include "check.h"
#include "common/bits.h"

using namespace ens;

TEST(bits_sext) {
    CHECK_EQ(sext(0x800000, 24), -8388608);
    CHECK_EQ(sext(0x7FFFFF, 24), 8388607);
    CHECK_EQ(sext(0x1F, 5), -1);
}

TEST(bits_sat) {
    CHECK_EQ(sat(40000, 16), 32767);
    CHECK_EQ(sat(-40000, 16), -32768);
    CHECK_EQ(sat(1LL << 30, 24), 0x7FFFFF);
}

TEST(bits_mask24) {
    CHECK_EQ(mask24(-1), 0xFFFFFFu);
}
