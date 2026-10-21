#include <cstdint>
#include <string>
#include <vector>

#include "check.h"
#include "esp/esp_isa.h"
#include "ks32/board.h"
#include "ks32/wave_roms.h"

using ens::Esp;
using ens::ks32::Board;
using ens::ks32::buildWaveBanks;
using ens::esp::Instr;
using ens::esp::Op;
using ens::esp::RamOp;

namespace {

constexpr uint8_t kZero = 255;
constexpr uint8_t kSer0R = 234, kSer0L = 235, kSer3R = 240, kSer3L = 241;

Instr mov(uint8_t src, uint8_t dst) {
    return Instr{kZero, kZero, src, dst, Op::MOV, 0, false, true, RamOp::ReadDelay};
}
Instr endWord() { return Instr{kZero, kZero, kZero, kZero, Op::END, 0, false, true, RamOp::ReadDelay}; }

// Loads program word `idx` through the host interface while halted (INSTR only, select $C0).
void loadWord(Board& b, unsigned idx, const Instr& in) {
    const uint64_t w = in.encode();
    for (int i = 0; i < 6; ++i) b.espHostWrite(static_cast<uint8_t>(3 + i), static_cast<uint8_t>(w >> (40 - 8 * i)));
    b.espHostWrite(0xC0, static_cast<uint8_t>(idx));
}

// Host GPR write: data $00-$02, then select $A0 = address (lands on the idle step).
void writeGpr(Board& b, uint8_t addr, uint32_t v) {
    b.espHostWrite(0x00, static_cast<uint8_t>(v >> 16));
    b.espHostWrite(0x01, static_cast<uint8_t>(v >> 8));
    b.espHostWrite(0x02, static_cast<uint8_t>(v));
    b.espHostWrite(0xA0, addr);
}

// One looped voice, all four poles low-pass and open, loop start at word 0, loop end 16 samples,
// FC = one wave sample per frame (same setup as tests/test_trace.cpp voice()).
void setVoice(ens::Otis& o, unsigned v, unsigned ca, unsigned lvol, unsigned rvol) {
    o.write(15, static_cast<uint16_t>(v));
    o.write(0, static_cast<uint16_t>(0x0C00 | 0x0008 | (ca << 8)));
    o.write(1, 0x0400);
    o.write(2, 0);
    o.write(3, 0);
    o.write(4, 0);
    o.write(5, 0x2000);
    o.write(6, 0xFFF0);
    o.write(7, 0xFFF0);
    o.write(8, static_cast<uint16_t>(lvol << 8));
    o.write(9, static_cast<uint16_t>(rvol << 8));
    o.write(10, 0);
    o.write(11, 0);
}

void otisSetup(ens::Otis& o, unsigned act) {
    o.write(13, static_cast<uint16_t>(act));
    o.write(15, 0x0040);
    o.write(8, 0);
}

}  // namespace

TEST(wave_banks_layout) {
    std::vector<uint8_t> u25(524288, 0), u26(524288, 0), rom2(1048576, 0), rom3(1048576, 0);
    u25[0] = 0x12;
    u26[0] = 0x80;
    rom2[0] = 0x34;
    rom2[1] = 0x12;
    rom3[0] = 0xCD;
    rom3[1] = 0xAB;
    std::vector<uint16_t> b0, b1;
    std::string err;
    CHECK(buildWaveBanks(u25, u26, rom2, rom3, b0, b1, err));
    CHECK_EQ(b0.size(), 0x100000u);
    CHECK_EQ(b1.size(), 0x100000u);
    CHECK_EQ(b0[0], 0x1200);
    CHECK_EQ(b0[0x80000], 0x8000);
    CHECK_EQ(b1[0], 0x1234);
    CHECK_EQ(b1[0x80000], 0xABCD);
    CHECK_EQ(b0[1], 0);
    CHECK_EQ(b1[1], 0);
}

TEST(wave_banks_reject_wrong_sizes) {
    std::vector<uint8_t> u25(524288, 0), u26(524287, 0), rom2(1048576, 0), rom3(1048576, 0);
    std::vector<uint16_t> b0, b1;
    std::string err;
    CHECK(!buildWaveBanks(u25, u26, rom2, rom3, b0, b1, err));
    CHECK(err.find("u26") != std::string::npos);
    u26.assign(524288, 0);
    rom3.assign(1048575, 0);
    CHECK(!buildWaveBanks(u25, u26, rom2, rom3, b0, b1, err));
    CHECK(err.find("rom3") != std::string::npos);
}

TEST(board_routes_ca1_to_ser0_to_main) {
    // The program copies SER0 (OTIS CH1) to SER3 (main). A reference board runs the same voice
    // on CA 0 and reads the OTIS frame directly; main must equal that CH0 signal with no lag
    // (same frame: runFrame -> setSerialIn -> advance -> serialOut; see trace_esp_passthrough).
    // The brief sketches ADD; SER3 is a latch that keeps its value between frames, so ADD would
    // accumulate every frame. MOV is the ALU op that gives the "track" behaviour asked for.
    const std::vector<uint16_t> wave(64, 0x4000);
    Board b(wave, {});
    Board ref(wave, {});
    b.esp().setHaltPin(true);
    b.espHostWrite(0x1F, 0xFF);
    CHECK(b.esp().halted());
    loadWord(b, 0, mov(kSer0L, kSer3L));
    loadWord(b, 1, mov(kSer0R, kSer3R));
    loadWord(b, 2, endWord());
    b.esp().setHaltPin(false);
    otisSetup(b.otis(), 0);
    setVoice(b.otis(), 0, 1, 0xF0, 0xB8);
    otisSetup(ref.otis(), 0);
    setVoice(ref.otis(), 0, 0, 0xF0, 0xB8);
    int nonzero = 0;
    for (int n = 0; n < 8; ++n) {
        int16_t l = 0, r = 0;
        b.frame(l, r);
        const ens::OtisFrame rf = ref.otis().runFrame();
        CHECK_EQ(l, rf.ch[0][0]);
        CHECK_EQ(r, rf.ch[0][1]);
        if (l != 0 && r != 0) ++nonzero;
    }
    CHECK(nonzero >= 6);
    int16_t l = 0, r = 0;
    b.frame(l, r);
    CHECK(l != r);  // different L/R volumes, so a channel swap would show
}

TEST(board_dram_clear_takes_737_frames) {
    // The firmware's clear: halt, MEMSIZ 0xFF, DLENGTH 0xFFFFFF, host control 0x03 then 0x02.
    // The clear needs 65536 idle steps (E25/E28: DLENGTH/unit + 1 with unit 65536... pinned by
    // host_firmware_boot_sequence). The final espHostWrite(0x12, 0x02) starts the clear and its
    // own TR2 idle step performs the first write, leaving 65535 steps. One frame is 89 idle
    // steps: 736 * 89 = 65504 < 65535 <= 737 * 89 = 65593, so the bit is still set after 736
    // frames and clear after 737.
    Board b({}, {});
    b.esp().setHaltPin(true);
    b.espHostWrite(0x1F, 0x02);
    CHECK(b.esp().halted());
    writeGpr(b, 0xF4, 0x0000FF);
    writeGpr(b, 0xF5, 0xFFFFFF);
    b.espHostWrite(0x12, 0x03);
    b.espHostWrite(0x12, 0x02);
    CHECK_EQ(b.esp().hostRead(0x12) & 2, 2);
    int16_t l = 0, r = 0;
    for (int n = 0; n < 736; ++n) b.frame(l, r);
    CHECK_EQ(b.esp().hostRead(0x12) & 2, 2);
    b.frame(l, r);
    CHECK_EQ(b.esp().hostRead(0x12) & 2, 0);
    CHECK_EQ(b.dram(0), 0);
}
