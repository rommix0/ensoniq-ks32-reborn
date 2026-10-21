#include "check.h"
#include "esp/esp_agen.h"

using namespace ens::esp;

TEST(agen_memsiz_64k) {
    Agen a;
    a.memsiz = 0x0000FF;
    CHECK_EQ(a.unit(), 0x100u);
    CHECK_EQ(a.shift(), 8);
}

TEST(agen_delay_no_wrap) {
    Agen a;
    a.memsiz = 0xFF;
    a.dlength = 0x00FF00;
    a.dbase = 0x001000;
    CHECK_EQ(a.physical(RamOp::ReadDelay, 0x000200), 0x12u);
    CHECK_EQ(a.physical(RamOp::WriteDelay, 0x000200), 0x12u);
    CHECK_EQ(a.physical(RamOp::ReadDelayDump, 0x000200), 0x12u);
}

TEST(agen_delay_wraps_circular) {
    Agen a;
    a.memsiz = 0xFF;
    a.dlength = 0x00FF00;
    a.dbase = 0x00FF00;
    // sum 0x010100 > dlength: minus (dlength + unit) = 0x010000 -> 0x000100 -> word 1.
    CHECK_EQ(a.physical(RamOp::ReadDelay, 0x000200), 0x01u);
}

TEST(agen_delay_top_of_buffer_with_fraction_does_not_wrap) {
    Agen a;
    a.memsiz = 0xFF;
    a.dlength = 0x00FF00;  // delay words 0..0xFF inclusive
    a.dbase = 0x00FF00;
    // 0xFF.80 is still inside word 0xFF.
    CHECK_EQ(a.physical(RamOp::ReadDelay, 0x000080), 0xFFu);
}

TEST(agen_delay_dlength_all_ones_stays_in_range) {
    Agen a;
    a.memsiz = 0xFF;
    a.writeReg(245, 0xFFFFFF);  // firmware value: read back as written (R13), arithmetic ignores bits 3..0
    CHECK_EQ(a.dlength, 0xFFFFFFu);
    CHECK_EQ(a.dlength20(), 0xFFFFF0u);
    const uint32_t offs[] = {0x000000u, 0x0000F8u, 0x000100u, 0x123456u, 0x7FFFFFu, 0xFFFFFFu};
    const uint32_t bases[] = {0xFFFFF0u, 0xFFFF00u, 0x000000u, 0x00AB00u};
    for (uint32_t off : offs) {
        for (uint32_t d : bases) {
            a.dbase = d;
            CHECK(a.physical(RamOp::ReadDelay, off) <= 0xFFFFu);
        }
    }
    a.dbase = 0xFFFF00;
    CHECK_EQ(a.physical(RamOp::ReadDelay, 0x0000F8), 0xFFFFu);  // fraction does not wrap
    CHECK_EQ(a.physical(RamOp::ReadDelay, 0x000100), 0x0000u);  // ring is the full 64K
}

TEST(agen_dbase_decrements_and_reloads) {
    Agen a;
    a.memsiz = 0xFF;
    a.dlength = 0x00FF00;
    a.dbase = 0x000000;
    a.endOfSample();
    CHECK_EQ(a.dbase, a.dlength);
    a.dbase = 0x000100;
    a.endOfSample();
    CHECK_EQ(a.dbase, 0u);
    a.dbase = 0x000500;
    a.endOfSample();
    CHECK_EQ(a.dbase, 0x400u);
}

TEST(agen_period_matches_wrap) {
    Agen a;
    a.memsiz = 0xFF;
    a.dlength = 0x00FF00;
    a.dbase = a.dlength;
    const uint32_t period = a.dlength / a.unit() + 1;
    CHECK_EQ(period, 256u);
    const uint32_t off = 0x003400;
    const uint32_t first = a.physical(RamOp::ReadDelay, off);
    bool recurred_early = false;
    for (uint32_t i = 1; i < period; i++) {
        a.endOfSample();
        if (a.physical(RamOp::ReadDelay, off) == first) recurred_early = true;
    }
    CHECK(!recurred_early);  // all addresses distinct within one period
    a.endOfSample();
    CHECK_EQ(a.physical(RamOp::ReadDelay, off), first);
    // The address walks down by one word per sample (circularly).
    Agen b = a;
    const uint32_t prev = b.physical(RamOp::ReadDelay, off);
    b.endOfSample();
    const uint32_t cur = b.physical(RamOp::ReadDelay, off);
    CHECK_EQ((prev + 255u) % 256u, cur);
}

TEST(agen_period_odd_dlength) {
    Agen a;  // the firmware's second configuration: tables above DLENGTH = 0x1F7DFF
    a.memsiz = 0xFF;
    a.writeReg(245, 0x1F7DFF);
    a.dbase = a.dlength;
    const uint32_t period = 0x1F7Du + 1;
    const uint32_t first = a.physical(RamOp::ReadDelay, 0x000300);
    for (uint32_t i = 0; i < period; i++) {
        CHECK(a.physical(RamOp::ReadDelay, 0x000300) <= 0x1F7Du);
        a.endOfSample();
    }
    CHECK_EQ(a.physical(RamOp::ReadDelay, 0x000300), first);
}

TEST(agen_delay_wrap_is_modulo_when_dbase_above_dlength) {
    Agen a;  // program 10 after the firmware-style load: DBASE far above the ring
    a.memsiz = 0xFF;
    a.writeReg(245, 0x1F7DFF);  // ring words 0..0x1F7D, period 0x1F7E
    a.dbase = 0xF71700;
    // sum word 0xF71A; 0xF71A mod 0x1F7E = 0x1AA8 (a single subtraction would give 0xD79C).
    CHECK_EQ(a.physical(RamOp::ReadDelay, 0x000300), 0x1AA8u);
    CHECK_EQ(a.physical(RamOp::WriteDelay, 0x000300), 0x1AA8u);
    // An offset beyond one ring length wraps the same way.
    a.dbase = 0x000000;
    CHECK_EQ(a.physical(RamOp::ReadDelay, 0x5F7A00), 0x5F7Au % 0x1F7Eu);
}

TEST(agen_delay_never_leaves_ring_for_any_dbase) {
    Agen a;
    a.memsiz = 0xFF;
    a.writeReg(245, 0x1F7DFF);
    a.writeReg(246, 0x1FBF00);  // the program's tables sit above the ring
    a.writeReg(247, 0x1F7E00);
    const uint32_t offs[] = {0x000000u, 0x000300u, 0x1F7D00u, 0x1F7E00u, 0x7FFF00u, 0xFFFF00u};
    bool outside = false;
    for (uint32_t w = 0; w <= 0xFFFFu; w++) {
        a.dbase = w << 8;
        for (uint32_t off : offs)
            if (a.physical(RamOp::WriteDelay, off) > 0x1F7Du) outside = true;
    }
    CHECK(!outside);
}

TEST(agen_delay_walk_continuous_from_dbase_above_dlength) {
    Agen a;  // from an out-of-ring DBASE the tap still walks down one word per sample,
    a.memsiz = 0xFF;  // across the DBASE reload, with period 0x1F7E
    a.writeReg(245, 0x1F7DFF);
    a.dbase = 0x3F1000;  // sum word 0x3F13: above twice the ring length
    uint32_t prev = a.physical(RamOp::ReadDelay, 0x000300);
    bool continuous = prev <= 0x1F7Du;
    for (int i = 0; i < 0x3F20; i++) {
        a.endOfSample();
        const uint32_t cur = a.physical(RamOp::ReadDelay, 0x000300);
        if (cur != (prev + 0x1F7Du) % 0x1F7Eu) continuous = false;
        prev = cur;
    }
    CHECK(continuous);
    CHECK(a.dbase <= a.dlength20());  // DBASE passed 0 and reloaded inside the ring
}

TEST(agen_table_not_masked_by_dlength) {
    Agen a;
    a.memsiz = 0xFF;
    a.dlength = 0x00FF00;
    a.abase = 0x020000;
    a.bbase = 0x030000;
    CHECK_EQ(a.physical(RamOp::ReadTableA, 0x000300), 0x203u);
    CHECK_EQ(a.physical(RamOp::WriteTableA, 0x000300), 0x203u);
    CHECK_EQ(a.physical(RamOp::ReadTableB, 0x000300), 0x303u);
    // negative offsets (24-bit two's complement) work
    CHECK_EQ(a.physical(RamOp::ReadTableA, 0xFFFF00), 0x1FFu);
}

TEST(agen_io_absolute) {
    Agen a;
    a.memsiz = 0xFF;
    a.dbase = 0x555500;
    a.abase = 0x777700;
    CHECK_EQ(a.physical(RamOp::ReadIo, 0x123450), 0x12345u);
    CHECK_EQ(a.physical(RamOp::WriteIo, 0x123450), 0x12345u);
    CHECK(a.isIo(RamOp::ReadIo));
    CHECK(a.isIo(RamOp::WriteIo));
    CHECK(!a.isIo(RamOp::ReadDelay));
    CHECK(!a.isIo(RamOp::WriteTableA));
}

TEST(agen_write_flags) {
    Agen a;
    CHECK(a.isWrite(RamOp::WriteDelay));
    CHECK(a.isWrite(RamOp::WriteTableA));
    CHECK(a.isWrite(RamOp::WriteIo));
    CHECK(!a.isWrite(RamOp::ReadDelay));
    CHECK(!a.isWrite(RamOp::ReadTableA));
    CHECK(!a.isWrite(RamOp::ReadTableB));
    CHECK(!a.isWrite(RamOp::ReadDelayDump));
    CHECK(!a.isWrite(RamOp::ReadIo));
}

TEST(agen_reg_alignment) {
    Agen a;
    a.writeReg(245, 0xFFFFFF);
    CHECK_EQ(a.readReg(245), 0xFFFFFFu);
    a.writeReg(246, 0x12345F);
    CHECK_EQ(a.readReg(246), 0x12345Fu);
    a.writeReg(247, 0xABCDEF);
    CHECK_EQ(a.readReg(247), 0xABCDEFu);
    a.writeReg(248, 0x00FFFF);
    CHECK_EQ(a.readReg(248), 0x00FFFFu);
    CHECK_EQ(a.readReg(249), 0u);  // not an AGEN register
    // E20: writing MEMSIZ masks DBASE
    a.writeReg(244, 0xFF);
    CHECK_EQ(a.memsiz, 0xFFu);
    CHECK_EQ(a.readReg(244), 0xFFu);
    CHECK_EQ(a.dbase, 0x00FF00u);
    // the other registers are left alone
    CHECK_EQ(a.readReg(245), 0xFFFFFFu);
    // R13: bits 3..0 are stored but ignored by the address arithmetic.
    Agen b;
    b.memsiz = 0xFF;
    b.writeReg(245, 0x00FF0F);
    b.writeReg(246, 0x00010F);
    b.writeReg(248, 0x00020F);
    CHECK_EQ(b.dbaseReload(), 0xFF00u);
    CHECK_EQ(b.physical(RamOp::ReadTableA, 0x000200), 0x03u);  // abase 0x100 + 0x200
    CHECK_EQ(b.physical(RamOp::ReadDelay, 0), 0x02u);
    b.endOfSample();
    CHECK_EQ(b.dbase, 0x000100u);
}

TEST(fifo_semantics) {
    DolFifo f;
    f.push(0x111111);
    f.push(0x222222);
    f.push(0x333333);
    CHECK_EQ(f.size(), 2);
    uint32_t v = 0;
    CHECK(f.pop(v));
    CHECK_EQ(v, 0x222222u);
    CHECK(f.pop(v));
    CHECK_EQ(v, 0x333333u);
    CHECK_EQ(f.size(), 0);
    CHECK(f.pop(v));  // empty: last pushed again
    CHECK_EQ(v, 0x333333u);
    f.dump();
    CHECK_EQ(f.size(), 0);
    f.push(0x444444);
    f.dump();
    CHECK_EQ(f.size(), 0);
    f.push(0x555555);
    f.reset();
    CHECK_EQ(f.size(), 0);
}

TEST(fifo_two_writes_two_reads_in_order) {
    DolFifo f;
    f.push(1);
    f.push(2);
    uint32_t v = 0;
    CHECK(f.pop(v));
    CHECK_EQ(v, 1u);
    f.push(3);
    CHECK_EQ(f.size(), 2);
    CHECK(f.pop(v));
    CHECK_EQ(v, 2u);
    CHECK(f.pop(v));
    CHECK_EQ(v, 3u);
}
