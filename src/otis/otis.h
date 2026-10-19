#pragma once
// ES5505 "OTIS" sample chip core: host register interface and paging.
// Integer-only, no OS calls, no globals. See docs/reference/es5505-otis-datasheet-notes.md.
#include <cstdint>

namespace ens {

struct WaveMemory {
    virtual ~WaveMemory() = default;
    virtual uint16_t read(int bank, uint32_t wordAddr) = 0;  // bank 0/1, addr 20-bit
};

struct OtisFrame {
    int16_t ch[4][2];  // [channel][0=L,1=R]
};

class Otis {
public:
    explicit Otis(WaveMemory& mem);
    void reset();
    uint16_t read(uint8_t reg);  // reg = A3..A0 (0..15)
    void write(uint8_t reg, uint16_t data);
    bool irqAsserted() const;   // true = IRQ pin pulled low
    int activeVoices() const;   // ACT+1
    void setSerialIn(int16_t l, int16_t r);  // SER3 input when SERMODE.A/D=1 (latched into CH3L/R)

    // Voice scan. stepVoice() processes the next voice slot (0..ACT): wave fetches, address
    // update and IRQ. After slot ACT the frame is marked ready.
    void stepVoice();
    bool frameReady() const { return ready_; }
    OtisFrame takeFrame();  // returns the latched channels and clears ready
    OtisFrame runFrame();   // steps until a frame is ready, then takes it
    uint32_t acc29(int voice) const;  // test accessor: 29-bit accumulator (20.9 fixed point)

private:
    // Per-voice registers, stored already masked to their implemented bits.
    struct Voice {
        uint16_t cr;      // [11:0]
        uint16_t fc;      // [15:1]
        uint16_t strtH;   // [12:0]
        uint16_t strtL;   // [15:5]
        uint16_t endH;    // [12:0]
        uint16_t endL;    // [15:5]
        uint16_t k2;      // [15:4]
        uint16_t k1;      // [15:4]
        uint16_t lvol;    // [15:8]
        uint16_t rvol;    // [15:8]
        uint16_t acch;    // [12:0]
        uint16_t accl;    // [15:0]
        // Filter storage (pages 32-63), all 16 bits.
        uint16_t o1;      // reg 6: O1(n-1)
        uint16_t o2_1;    // reg 4: O2(n-1)
        uint16_t o2_2;    // reg 5: O2(n-2)
        uint16_t o3_1;    // reg 2: O3(n-1)
        uint16_t o3_2;    // reg 3: O3(n-2)
        uint16_t o4;      // reg 1: O4(n-1)
    };

    uint16_t* voiceReg(Voice& v, uint8_t reg);  // pages 0-31 map, nullptr if unlisted
    uint16_t* filterReg(Voice& v, uint8_t reg); // pages 32-63 map, nullptr if unlisted
    static uint16_t voiceMask(uint8_t reg);
    void updateAddress(int voice, Voice& v);  // O1 address ALU for one slot; raises CR.IRQ
    void serviceIrq(int voice, Voice& v);     // O8 latch into IRQV when free
    static uint16_t chRead(int32_t acc) { return static_cast<uint16_t>(acc & 0xFFFF); }

    WaveMemory& mem_;
    Voice voice_[32];
    uint8_t act_;        // ACT [4:0]
    uint8_t page_;       // PAGE [6:0]
    uint16_t sermodeBits_;  // SERMODE bits [15:11] and [2:0] only
    int32_t chAcc_[8];   // CH0L,CH0R..CH3L,CH3R: 21-bit signed accumulators
    bool irqPending_;    // an interrupt is pending (IRQ pin low)
    uint8_t irqVec_;     // IRQV [4:0]: voice that raised the interrupt
    uint32_t irqClear_;  // bit per voice: IRQV was read, clear that voice's CR.IRQ on its next slot
    uint8_t slot_;       // next voice slot to process
    bool ready_;         // a frame is latched in frame_
    OtisFrame frame_;
    uint16_t s1_, s2_;   // wave words fetched by the current slot (consumed by the signal path)
};

}  // namespace ens
