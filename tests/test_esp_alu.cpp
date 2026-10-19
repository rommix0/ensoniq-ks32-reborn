#include <utility>

#include "check.h"
#include "esp/esp_alu.h"

using namespace ens::esp;

namespace {

const Flags kNone{};
Flags allCvLt() {
    Flags f;
    f.c = f.v = f.lt = true;
    return f;
}
bool sameFlags(const Flags& x, const Flags& y) {
    return x.n == y.n && x.c == y.c && x.v == y.v && x.lt == y.lt && x.z == y.z;
}

}  // namespace

TEST(alu_add_saturates) {
    AluResult r = alu(Op::ADD, 0x7FFFFF, 1, kNone);
    CHECK_EQ(r.value, 0x7FFFFFu);
    CHECK(r.flags.v);
    CHECK(!r.flags.n);
    CHECK(!r.flags.lt);
    CHECK(r.writes);
    r = alu(Op::ADD, 0x800000, 0xFFFFFF, kNone);
    CHECK_EQ(r.value, 0x800000u);
    CHECK(r.flags.v);
    CHECK(r.flags.n);
    CHECK(r.flags.lt);
    r = alu(Op::ADD, 0xFFFFFF, 1, kNone);  // no saturation, carry out, zero
    CHECK_EQ(r.value, 0u);
    CHECK(r.flags.z);
    CHECK(r.flags.c);
    CHECK(!r.flags.v);
}

TEST(alu_addu_wraps) {
    AluResult r = alu(Op::ADDU, 0x7FFFFF, 1, kNone);
    CHECK_EQ(r.value, 0x800000u);
    CHECK(r.flags.v);
    CHECK(r.flags.n);
    CHECK(!r.flags.lt);  // N xor V
    CHECK(!r.flags.c);
    r = alu(Op::ADDU, 0xFFFFFF, 1, kNone);
    CHECK_EQ(r.value, 0u);
    CHECK(r.flags.z);
    CHECK(r.flags.c);
}

TEST(alu_sub_borrow) {
    AluResult r = alu(Op::SUB, 1, 2, kNone);
    CHECK_EQ(r.value, 0xFFFFFFu);
    CHECK(r.flags.c);
    CHECK(r.flags.n);
    CHECK(r.flags.lt);
    r = alu(Op::SUB, 2, 1, kNone);
    CHECK_EQ(r.value, 1u);
    CHECK(!r.flags.c);
    r = alu(Op::SUB, 0x800000, 1, kNone);  // saturates negative
    CHECK_EQ(r.value, 0x800000u);
    CHECK(r.flags.v);
    CHECK(r.flags.lt);
    r = alu(Op::SUB, 0x7FFFFF, 0xFFFFFF, kNone);  // 0x7FFFFF - (-1): saturates positive
    CHECK_EQ(r.value, 0x7FFFFFu);
    CHECK(r.flags.v);
    CHECK(!r.flags.lt);
    CHECK(!r.flags.n);
}

TEST(alu_subu_cmp_flags) {
    for (Op op : {Op::SUBU, Op::CMP}) {
        AluResult r = alu(op, 1, 2, kNone);
        CHECK(r.flags.c && r.flags.n && r.flags.lt && !r.flags.v && !r.flags.z);
        r = alu(op, 0x800000, 1, kNone);  // wraps to 0x7FFFFF
        CHECK(r.flags.v && r.flags.lt && !r.flags.n && !r.flags.c);  // LT = N xor V = 1 (true less-than)
        CHECK_EQ(r.value, 0x7FFFFFu);
    }
}

TEST(alu_cmp_does_not_write) {
    for (auto [a, b] : {std::pair<uint32_t, uint32_t>{1, 2}, {2, 1}, {0x7FFFFF, 0xFFFFFF}, {5, 5}}) {
        const AluResult c = alu(Op::CMP, a, b, kNone);
        const AluResult s = alu(Op::SUBU, a, b, kNone);
        CHECK(!c.writes);
        CHECK(s.writes);
        CHECK(sameFlags(c.flags, s.flags));
    }
    CHECK(alu(Op::CMP, 5, 5, kNone).flags.z);
}

TEST(alu_logic_keeps_cvlt) {
    AluResult r = alu(Op::AND, 0xF0F0F0, 0x0F0F0F, allCvLt());
    CHECK_EQ(r.value, 0u);
    CHECK(r.flags.z);
    CHECK(!r.flags.n);
    CHECK(r.flags.c && r.flags.v && r.flags.lt);
    r = alu(Op::OR, 0xF0F0F0, 0x0F0F0F, kNone);
    CHECK_EQ(r.value, 0xFFFFFFu);
    CHECK(r.flags.n);
    r = alu(Op::XOR, 0xFFFFFF, 0xFFFFFF, allCvLt());
    CHECK_EQ(r.value, 0u);
    CHECK(r.flags.z && r.flags.c && r.flags.v && r.flags.lt);
}

TEST(alu_abs_ones_complement) {
    Flags prev = allCvLt();
    prev.z = true;
    AluResult r = alu(Op::ABS, 0, 0xFFFFFF, prev);
    CHECK_EQ(r.value, 0u);
    CHECK(!r.flags.n);
    CHECK(r.flags.c);
    CHECK(r.flags.z);  // unchanged from prev, not recomputed
    r = alu(Op::ABS, 0, 0x800000, kNone);
    CHECK_EQ(r.value, 0x7FFFFFu);
    CHECK(r.flags.c);
    r = alu(Op::ABS, 0, 0x123456, allCvLt());  // positive: C = 0
    CHECK_EQ(r.value, 0x123456u);
    CHECK(!r.flags.c);
    CHECK(r.flags.v && r.flags.lt);
    // Z is never recomputed: prev disagrees with the result in both directions
    Flags zClear;  // z=0 while the result is 0
    r = alu(Op::ABS, 0, 0xFFFFFF, zClear);
    CHECK_EQ(r.value, 0u);
    CHECK(!r.flags.z);
    Flags zSet;  // z=1 while the result is nonzero
    zSet.z = true;
    r = alu(Op::ABS, 0, 0x000005, zSet);
    CHECK_EQ(r.value, 5u);
    CHECK(r.flags.z);
}

TEST(alu_mov_no_flags) {
    Flags prev;
    prev.n = prev.v = true;
    const AluResult r = alu(Op::MOV, 0x111111, 0x800000, prev);
    CHECK_EQ(r.value, 0x800000u);
    CHECK(r.writes);
    CHECK(sameFlags(r.flags, prev));
    // prev disagrees with what recomputation from the value would give
    Flags zSetNeg;  // z=1, n=0, negative value
    zSetNeg.z = true;
    AluResult m = alu(Op::MOV, 0, 0x800000, zSetNeg);
    CHECK(sameFlags(m.flags, zSetNeg));
    Flags zClearNeg;  // z=0, n=1, value 0
    zClearNeg.n = true;
    m = alu(Op::MOV, 0, 0, zClearNeg);
    CHECK_EQ(m.value, 0u);
    CHECK(sameFlags(m.flags, zClearNeg));
    m = alu(Op::MOV, 0, 0x123456, allCvLt());
    CHECK(sameFlags(m.flags, allCvLt()));
    const AluResult e = alu(Op::END, 0, 0, prev);
    CHECK(!e.writes);
    CHECK(sameFlags(e.flags, prev));
}

TEST(alu_asl2) {
    AluResult r = alu(Op::ASL2, 0, 0x100000, kNone);
    CHECK_EQ(r.value, 0x400000u);
    CHECK(!r.flags.v);
    r = alu(Op::ASL2, 0, 0x200000, kNone);
    CHECK_EQ(r.value, 0x7FFFFFu);
    CHECK(r.flags.v);
    r = alu(Op::ASL2, 0, 0xE00000, kNone);
    CHECK_EQ(r.value, 0x800000u);
    CHECK(!r.flags.v);
    r = alu(Op::ASL2, 0, 0xC00000, allCvLt());  // negative overflow; C and LT unchanged
    CHECK_EQ(r.value, 0x800000u);
    CHECK(r.flags.v && r.flags.n && r.flags.c && r.flags.lt);
    r = alu(Op::ASL2, 0, 0xBFFFFF, kNone);
    CHECK_EQ(r.value, 0x800000u);
    CHECK(r.flags.v);
}

TEST(alu_asl8) {
    AluResult r = alu(Op::ASL8, 0, 0x00007F, kNone);
    CHECK_EQ(r.value, 0x007F00u);
    CHECK(!r.flags.v);
    r = alu(Op::ASL8, 0, 0x008000, kNone);
    CHECK_EQ(r.value, 0x7FFFFFu);
    CHECK(r.flags.v);
    r = alu(Op::ASL8, 0, 0, allCvLt());
    CHECK(r.flags.z && !r.flags.v && r.flags.c && r.flags.lt);
}

TEST(alu_ls15) {
    Flags prev;
    prev.z = prev.v = prev.lt = true;
    AluResult r = alu(Op::LS15, 0, 0x0001FF, prev);
    CHECK_EQ(r.value, 0x7F8000u);
    CHECK(!r.flags.n);
    CHECK(r.flags.c);
    CHECK(r.flags.z && r.flags.v && r.flags.lt);  // unchanged
    r = alu(Op::LS15, 0, 0x0000FF, allCvLt());
    CHECK_EQ(r.value, 0x7F8000u);
    CHECK(!r.flags.c);
}

TEST(alu_diff_unsaturated) {
    AluResult r = alu(Op::DIFF, 0, 0, kNone);
    CHECK_EQ(r.value, 0x7FFFFFu);
    CHECK(!r.flags.v);
    r = alu(Op::DIFF, 0, 0x800000, kNone);
    CHECK_EQ(r.value, 0xFFFFFFu);
    CHECK(r.flags.v);
    CHECK(r.writes);
}

TEST(alu_asr) {
    AluResult r = alu(Op::ASR, 0, 0x800001, allCvLt());
    CHECK_EQ(r.value, 0xC00000u);
    CHECK(r.flags.c);
    CHECK(r.flags.n);
    CHECK(r.flags.v && r.flags.lt);  // unchanged
    r = alu(Op::ASR, 0, 0x000002, kNone);
    CHECK_EQ(r.value, 1u);
    CHECK(!r.flags.c);
    CHECK(!r.flags.n);
    // Z is never recomputed
    r = alu(Op::ASR, 0, 1, kNone);  // result 0, prev.z = 0
    CHECK_EQ(r.value, 0u);
    CHECK(r.flags.c);
    CHECK(!r.flags.z);
    Flags zSet;
    zSet.z = true;
    r = alu(Op::ASR, 0, 0x000010, zSet);  // result nonzero, prev.z = 1
    CHECK_EQ(r.value, 8u);
    CHECK(r.flags.z);
}

TEST(alu_ccr_pack_roundtrip) {
    Flags f;
    f.n = true;
    f.z = true;
    CHECK_EQ(packCcr(f), 0x880000u);
    CHECK(sameFlags(unpackCcr(0x880000), f));
    CHECK_EQ(packCcr(unpackCcr(0xF80000)), 0xF80000u);
}

TEST(alu_skip_masks) {
    auto skip = [](bool n, bool c, bool v, bool lt, bool z, uint32_t mask) {
        Flags f;
        f.n = n; f.c = c; f.v = v; f.lt = lt; f.z = z;
        return skipCondition(packCcr(f), mask << 16);
    };
    // EQ 0x0C: skips when z=0
    CHECK(skip(0, 0, 0, 0, 0, 0x0C));
    CHECK(!skip(0, 0, 0, 0, 1, 0x0C));
    // NEQ 0x08: skips when z=1
    CHECK(!skip(0, 0, 0, 0, 0, 0x08));
    CHECK(skip(0, 0, 0, 0, 1, 0x08));
    // LT 0x14: skips when lt=0
    CHECK(skip(0, 0, 0, 0, 0, 0x14));
    CHECK(!skip(0, 0, 0, 1, 0, 0x14));
    // GT 0x18: skips when lt=1 or z=1
    CHECK(!skip(0, 0, 0, 0, 0, 0x18));
    CHECK(skip(0, 0, 0, 1, 0, 0x18));
    CHECK(skip(0, 0, 0, 0, 1, 0x18));
    // TRUE 0x04 always skips, FALSE 0x00 never
    for (int bits = 0; bits < 32; ++bits) {
        CHECK(skip(bits & 16, bits & 8, bits & 4, bits & 2, bits & 1, 0x04));
        CHECK(!skip(bits & 16, bits & 8, bits & 4, bits & 2, bits & 1, 0x00));
    }
    // lone V mask (0x20) and lone C mask (0x40), no NOT: skip when that flag is set
    CHECK(skip(0, 0, 1, 0, 0, 0x20));
    CHECK(!skip(0, 1, 0, 1, 1, 0x20));
    CHECK(skip(0, 1, 0, 0, 0, 0x40));
    CHECK(!skip(1, 0, 1, 1, 1, 0x40));
    // CCR bit 18 is ignored (only CMR bit 18 is NOT)
    CHECK(!skipCondition(1u << 18, 0));
    CHECK(skipCondition(1u << 18, 0x04u << 16));
    CHECK(!skipCondition((1u << 18) | packCcr(unpackCcr(0)), 0x08u << 16));
    // NEG 0x84 skips when n=0; LS 0x4C skips when !(c|z)
    CHECK(skip(0, 0, 0, 0, 0, 0x84));
    CHECK(!skip(1, 0, 0, 0, 0, 0x84));
    CHECK(skip(0, 0, 0, 0, 0, 0x4C));
    CHECK(!skip(0, 1, 0, 0, 0, 0x4C));
    CHECK(!skip(0, 0, 0, 0, 1, 0x4C));
}
