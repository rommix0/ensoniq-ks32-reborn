// 68000 debug-trace disassembler (Plan 5 Task 8): known encodings, and agreement with decodeOp
// over every first opcode word.
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "check.h"
#include "m68k/m68k_decode.h"
#include "m68k/m68k_dis.h"

using ens::m68k::DisLine;
using ens::m68k::Op;
using ens::m68k::decodeOp;
using ens::m68k::disassemble;

namespace {

constexpr uint32_t kBase = 0xC10000;

// Disassembles `words` placed at kBase and checks text and length (bytes = 2 * word count).
void expectDis(std::initializer_list<uint16_t> words, const char* text) {
    const std::vector<uint16_t> w(words);
    const DisLine d = disassemble(
        [&w](uint32_t a) -> uint16_t {
            const uint32_t i = (a - kBase) / 2;
            return i < w.size() ? w[i] : static_cast<uint16_t>(0xDEAD);
        },
        kBase);
    const bool ok = d.text == text && d.length == static_cast<int>(w.size() * 2);
    if (!ok) {
        std::printf("  FAIL %04X...: got \"%s\" (%d), expected \"%s\" (%zu)\n", w[0], d.text.c_str(),
                    d.length, text, w.size() * 2);
        ++ens_test::failures();
    }
}

}  // namespace

TEST(m68k_dis_known_encodings) {
    expectDis({0x4E71}, "nop");
    expectDis({0x46FC, 0x2700}, "move.w  #$2700,sr");
    expectDis({0x21FC, 0x00C1, 0x1138, 0x0100}, "move.l  #$c11138,($100).w");
    expectDis({0x13FC, 0x0010, 0x0028, 0x0005}, "move.b  #$10,($280005).l");
    expectDis({0x0839, 0x0002, 0x0026, 0x0025}, "btst    #2,($260025).l");
    expectDis({0x3038, 0x8000}, "move.w  ($ff8000).w,d0");
    expectDis({0x41FA, 0x0010}, "lea     ($c10012,pc),a0");
    expectDis({0x6100, 0x0010}, "bsr.w   $c10012");
    expectDis({0x6604}, "bne.s   $c10006");
    expectDis({0x51C8, 0xFFFE}, "dbf     d0,$c10000");
    expectDis({0x4ED0}, "jmp     (a0)");
    expectDis({0x4CDF, 0x7FFF}, "movem.l (sp)+,d0-d7/a0-a6");
    expectDis({0x48E7, 0xFFFE}, "movem.l d0-d7/a0-a6,-(sp)");
    expectDis({0x3030, 0x1004}, "move.w  (4,a0,d1.w),d0");
    expectDis({0x302E, 0xFFFC}, "move.w  (-$4,a6),d0");
    expectDis({0x4E4F}, "trap    #15");
    expectDis({0x4AFC}, "illegal");
    expectDis({0xA000}, "dc.w    $a000");
}

// Forms the brief does not list: every operand shape and the mnemonics around them.
TEST(m68k_dis_operand_forms) {
    expectDis({0xF123}, "dc.w    $f123");
    expectDis({0x4E75}, "rts");
    expectDis({0x4E72, 0x2000}, "stop    #$2000");
    expectDis({0x7EFF}, "moveq   #-$1,d7");
    expectDis({0x7005}, "moveq   #5,d0");
    expectDis({0x5E48}, "addq.w  #7,a0");
    expectDis({0x5148}, "subq.w  #8,a0");
    expectDis({0x4E56, 0xFFF8}, "link    a6,#-$8");
    expectDis({0x4E5E}, "unlk    a6");
    expectDis({0x4E60}, "move.l  a0,usp");
    expectDis({0x4E69}, "move.l  usp,a1");
    expectDis({0xC141}, "exg     d0,d1");
    expectDis({0xC949}, "exg     a4,a1");
    expectDis({0xCB8A}, "exg     d5,a2");
    expectDis({0x0108, 0x0004}, "movep.w (4,a0),d0");
    expectDis({0x01C9, 0x0010}, "movep.l d0,($10,a1)");
    expectDis({0xD347}, "addx.w  d7,d1");
    expectDis({0x9F0A}, "subx.b  -(a2),-(sp)");
    expectDis({0xD3C8}, "adda.l  a0,a1");
    expectDis({0xB308}, "cmpm.b  (a0)+,(a1)+");
    expectDis({0x0644, 0x1234}, "addi.w  #$1234,d4");
    expectDis({0x0A39, 0x00FF, 0x0000, 0x1000}, "eori.b  #$ff,($1000).l");
    expectDis({0x0AB9, 0x0001, 0x0002, 0x0000, 0x0100}, "eori.l  #$10002,($100).l");
    expectDis({0xE548}, "lsl.w   #2,d0");
    expectDis({0xE3A1}, "asl.l   d1,d1");
    expectDis({0xE6D0}, "ror.w   (a0)");
    expectDis({0xE0F9, 0x0000, 0x0100}, "asr.w   ($100).l");
    expectDis({0x0301}, "btst    d1,d1");
    expectDis({0x08C0, 0x001F}, "bset    #$1f,d0");
    expectDis({0x56C0}, "sne     d0");
    expectDis({0x4AD0}, "tas     (a0)");
    expectDis({0x6000, 0xFFFE}, "bra.w   $c10000");
}

// Every first word, with the extension words all zero and again all ones: the disassembler
// classifies exactly as decodeOp does, consumes an even 2..10 bytes, and reads nothing past them.
TEST(m68k_dis_every_opcode_agrees_with_decoder) {
    int bad = 0;
    for (const uint16_t ext : {static_cast<uint16_t>(0x0000), static_cast<uint16_t>(0xFFFF)}) {
        for (uint32_t w = 0; w <= 0xFFFF; ++w) {
            const uint16_t op = static_cast<uint16_t>(w);
            uint32_t highest = kBase;
            const DisLine d = disassemble(
                [&](uint32_t a) -> uint16_t {
                    if (a > highest) highest = a;
                    return a == kBase ? op : ext;
                },
                kBase);
            const Op k = decodeOp(op);
            const bool dc = k == Op::Illegal || k == Op::LineA || k == Op::LineF;
            const bool isIllegalWord = op == 0x4AFC;
            const bool gotDc = d.text.compare(0, 4, "dc.w") == 0;
            bool ok = d.length >= 2 && d.length <= 10 && (d.length % 2) == 0;
            ok = ok && !d.text.empty() && gotDc == (dc && !isIllegalWord);
            ok = ok && (isIllegalWord ? d.text == "illegal" : true);
            ok = ok && (!dc || d.length == 2);
            ok = ok && highest < kBase + static_cast<uint32_t>(d.length);
            if (!ok && bad++ < 10) {
                std::printf("  FAIL op %04X ext %04X: \"%s\" (%d)\n", op, ext, d.text.c_str(),
                            d.length);
            }
        }
    }
    CHECK_EQ(bad, 0);
}
