#include "otis/otis.h"

#include <cstring>

#include "common/bits.h"

namespace ens {

namespace {
constexpr uint16_t kSerModeBits = 0xF807;  // SERMODE implemented bits [15:11] and [2:0]
constexpr uint16_t kSerModeRead1 = 0x07F8; // unused bits [10:3] read back as 1
constexpr uint16_t kSerAD = 0x0001;
constexpr uint16_t kSerTest = 0x0002;
}  // namespace

Otis::Otis(WaveMemory& mem) : mem_(mem) { reset(); }

void Otis::reset() {
    // DECISION O10: reset = all voice regs 0 except CR=STOP1|STOP0, ACT=31, PAGE=0, IRQV free, SERMODE=A/D.
    std::memset(voice_, 0, sizeof(voice_));
    for (Voice& v : voice_) v.cr = 0x0003;
    act_ = 31;
    page_ = 0;
    sermodeBits_ = kSerAD;
    std::memset(chAcc_, 0, sizeof(chAcc_));
    irqPending_ = false;
    irqVec_ = 0;
}

// Implemented-bit mask for the pages 0-31 registers (index = register number).
uint16_t Otis::voiceMask(uint8_t reg) {
    switch (reg) {
        case 0: return 0x0FFF;   // CR
        case 1: return 0xFFFE;   // FC
        case 2: return 0x1FFF;   // STRT-H
        case 3: return 0xFFE0;   // STRT-L
        case 4: return 0x1FFF;   // END-H
        case 5: return 0xFFE0;   // END-L
        case 6: return 0xFFF0;   // K2
        case 7: return 0xFFF0;   // K1
        case 8: return 0xFF00;   // LVOL
        case 9: return 0xFF00;   // RVOL
        case 10: return 0x1FFF;  // ACCH
        case 11: return 0xFFFF;  // ACCL
        default: return 0;
    }
}

uint16_t* Otis::voiceReg(Voice& v, uint8_t reg) {
    switch (reg) {
        case 0: return &v.cr;
        case 1: return &v.fc;
        case 2: return &v.strtH;
        case 3: return &v.strtL;
        case 4: return &v.endH;
        case 5: return &v.endL;
        case 6: return &v.k2;
        case 7: return &v.k1;
        case 8: return &v.lvol;
        case 9: return &v.rvol;
        case 10: return &v.acch;
        case 11: return &v.accl;
        default: return nullptr;  // DECISION O9: unlisted registers read 0 and ignore writes
    }
}

uint16_t* Otis::filterReg(Voice& v, uint8_t reg) {
    switch (reg) {
        case 1: return &v.o4;
        case 2: return &v.o3_1;
        case 3: return &v.o3_2;
        case 4: return &v.o2_1;
        case 5: return &v.o2_2;
        case 6: return &v.o1;
        default: return nullptr;  // DECISION O9: unlisted registers read 0 and ignore writes
    }
}

uint16_t Otis::read(uint8_t reg) {
    reg &= 0xF;
    // Registers 13/14/15 are global in every page.
    switch (reg) {
        case 13: return act_;
        case 14:
            // DECISION O8: IRQV reads 0x60 | (pending ? 0 : 0x80) | vec; bits 6:5 read as 1.
            // (Task 3 adds the "read frees the vector" side effect.)
            return static_cast<uint16_t>(0x60 | (irqPending_ ? 0 : 0x80) | (irqVec_ & 0x1F));
        case 15: return page_;
        default: break;
    }

    Voice& v = voice_[page_ & 0x1F];
    if (page_ & 0x40) {  // pages 64-127: channel / serial, global to the voice number
        if (reg < 6) return 0;  // DECISION O11: CH0-CH2 are write-only (test), reads return 0
        if (reg < 8) return chRead(chAcc_[reg]);  // DECISION O11: CH3L/R read back last latched value
        if (reg == 8) return static_cast<uint16_t>(kSerModeRead1 | sermodeBits_);  // DECISION O9
        return 0;  // PAR (reg 9) not modelled; other regs unlisted
    }
    if (page_ & 0x20) {  // pages 32-63: filter storage
        const uint16_t* p = filterReg(v, reg);
        return p ? *p : 0;
    }
    const uint16_t* p = voiceReg(v, reg);
    if (!p) return 0;
    // DECISION O9: CR returns 0xF000 | cr12 (unused bits read as 1).
    return reg == 0 ? static_cast<uint16_t>(0xF000 | *p) : *p;
}

void Otis::write(uint8_t reg, uint16_t data) {
    reg &= 0xF;
    switch (reg) {
        case 13: act_ = data & 0x1F; return;
        case 14: return;  // IRQV is read-only
        case 15: page_ = data & 0x7F; return;
        default: break;
    }

    Voice& v = voice_[page_ & 0x1F];
    if (page_ & 0x40) {
        if (reg < 8) {
            // DECISION O11: channel registers are writable only with SERMODE.TEST=1; the five
            // SERMODE.MSB bits become the accumulator's guard bits (datasheet §4.9).
            if (!(sermodeBits_ & kSerTest)) return;
            const uint32_t msb = (sermodeBits_ >> 11) & 0x1F;
            chAcc_[reg] = sext((msb << 16) | data, 21);
        } else if (reg == 8) {
            sermodeBits_ = data & kSerModeBits;  // DECISION O9: unused bits ignored
        }
        return;
    }
    if (page_ & 0x20) {
        if (uint16_t* p = filterReg(v, reg)) *p = data;
        return;
    }
    if (uint16_t* p = voiceReg(v, reg)) *p = data & voiceMask(reg);
}

bool Otis::irqAsserted() const { return irqPending_; }

int Otis::activeVoices() const { return act_ + 1; }

void Otis::setSerialIn(int16_t l, int16_t r) {
    if (!(sermodeBits_ & kSerAD)) return;  // port 3 is an output unless A/D=1
    chAcc_[6] = l;
    chAcc_[7] = r;
}

}  // namespace ens
