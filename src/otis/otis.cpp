#include "otis/otis.h"

#include <cstring>

#include "common/bits.h"

namespace ens {

namespace {
constexpr uint16_t kSerModeBits = 0xF807;  // SERMODE implemented bits [15:11] and [2:0]
constexpr uint16_t kSerModeRead1 = 0x07F8; // unused bits [10:3] read back as 1
constexpr uint16_t kSerAD = 0x0001;
constexpr uint16_t kSerTest = 0x0002;

// CR bits.
constexpr uint16_t kStop0 = 0x0001, kStop1 = 0x0002, kBs = 0x0004, kLpe = 0x0008, kBle = 0x0010,
                   kIrqe = 0x0020, kDir = 0x0040, kIrq = 0x0080;

constexpr uint32_t kAcc29Mask = 0x1FFFFFFF;
constexpr uint32_t kPosMask = 0x1FFFFFE0;  // loop positions: low 5 fraction bits are 0
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
    irqClear_ = 0;
    slot_ = 0;
    ready_ = false;
    std::memset(&frame_, 0, sizeof(frame_));
    s1_ = s2_ = 0;
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
        case 14: {
            // DECISION O8: IRQV reads 0x60 | (pending ? 0 : 0x80) | vec; bits 6:5 read as 1.
            // The read frees the vector and releases IRQ; the serviced voice's CR.IRQ is
            // cleared when that voice is next processed.
            const uint16_t val =
                static_cast<uint16_t>(0x60 | (irqPending_ ? 0 : 0x80) | (irqVec_ & 0x1F));
            if (irqPending_) {
                irqClear_ |= 1u << (irqVec_ & 0x1F);
                irqPending_ = false;
            }
            return val;
        }
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

uint32_t Otis::acc29(int voice) const {
    const Voice& v = voice_[voice & 0x1F];
    return ((static_cast<uint32_t>(v.acch) << 16) | v.accl) & kAcc29Mask;
}

OtisFrame Otis::takeFrame() {
    ready_ = false;
    return frame_;
}

OtisFrame Otis::runFrame() {
    while (!ready_) stepVoice();
    return takeFrame();
}

void Otis::stepVoice() {
    const int idx = slot_;
    Voice& v = voice_[idx];

    // O8: the serviced voice's CR.IRQ is cleared on its next slot after the IRQV read.
    if (irqClear_ & (1u << idx)) {
        irqClear_ &= ~(1u << idx);
        v.cr &= static_cast<uint16_t>(~kIrq);
    }

    // Wave fetches use the accumulator as it stands before this slot's update.
    const uint32_t acc = acc29(idx);
    const int bank = (v.cr & kBs) ? 1 : 0;
    const uint32_t a1 = acc >> 9;  // 20-bit integer address
    // DECISION O2: S2 is at int + 1 in both directions, same bank, wrapping within 20 bits.
    const uint32_t a2 = (a1 + 1) & 0xFFFFF;
    s1_ = mem_.read(bank, a1);
    s2_ = mem_.read(bank, a2);

    // DECISION O7: STOP0/STOP1 freeze only the address ALU; fetch (and, in Task 4, the
    // signal path) still runs for a stopped voice.
    if (!(v.cr & (kStop0 | kStop1))) updateAddress(idx, v);

    serviceIrq(idx, v);

    if (idx >= act_) {
        // Frame complete after slot ACT. (If ACT is lowered mid-frame below the current slot,
        // this slot is still processed and the frame ends here.)
        ready_ = true;
        slot_ = 0;
    } else {
        ++slot_;
    }
}

void Otis::updateAddress(int voice, Voice& v) {
    // DECISION O1: step = FC>>1 in 1/512 sample; single-add; boundary at n >= END (forward) or
    // n <= START (reverse); remainder is kept across loops; BLE reflects and toggles DIR.
    const int64_t start = (((static_cast<uint32_t>(v.strtH) << 16) | v.strtL) & kPosMask);
    const int64_t end = (((static_cast<uint32_t>(v.endH) << 16) | v.endL) & kPosMask);
    const int64_t step = v.fc >> 1;
    const int64_t acc = acc29(voice);
    const bool lpe = v.cr & kLpe;
    const bool ble = v.cr & kBle;
    const bool dir = v.cr & kDir;

    int64_t next;
    bool hit;
    if (!dir) {
        next = (acc + step) & kAcc29Mask;  // the accumulator wraps within 29 bits
        hit = next >= end;
    } else {
        next = acc - step;  // may go negative; compared signed, wrapped on store
        hit = next <= start;
    }

    if (hit) {
        if (!lpe) {
            v.cr |= kStop0;
            next = dir ? start : end;
        } else if (!ble) {
            next = dir ? end - (start - next) : start + (next - end);
        } else if (!dir) {
            next = end - (next - end);
            v.cr |= kDir;
        } else {
            next = start + (start - next);
            v.cr &= static_cast<uint16_t>(~kDir);
        }
        if (v.cr & kIrqe) v.cr |= kIrq;  // IRQ at the loop end in every loop mode
    }

    const uint32_t n = static_cast<uint32_t>(next) & kAcc29Mask;
    v.acch = static_cast<uint16_t>(n >> 16);
    v.accl = static_cast<uint16_t>(n & 0xFFFF);
}

void Otis::serviceIrq(int voice, Voice& v) {
    // DECISION O8: a voice with CR.IRQ and IRQE latches into IRQV when it is free (bit 7 = 1);
    // otherwise it waits (stacks) in CR.IRQ until a later slot finds the vector free.
    if ((v.cr & kIrq) && (v.cr & kIrqe) && !irqPending_) {
        irqVec_ = static_cast<uint8_t>(voice);
        irqPending_ = true;
    }
}

}  // namespace ens
