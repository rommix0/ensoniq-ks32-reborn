#pragma once
// Motorola MC68681 DUART (data sheet MC68681/D, Sep 1985), as the KS-32 uses it for MIDI and the
// front-panel link: two serial channels, a 16-bit counter/timer, a 6-bit input port and an
// 8-bit output port. Character-level serial timing (D1); features the KS-32 does not need are
// counted, not modelled (D3). Instantiable: no globals, no statics, no OS calls.
#include <cstdint>

#include "common/ring.h"

namespace ens::duart {

class Duart {
public:
    static constexpr std::size_t kRxQueue = 1024;  // DECISION A1: characters on the wire per channel
    explicit Duart(uint32_t x1Hz);  // crystal frequency; the KS-32 passes 4'000'000
    void reset();                   // RESET pin: data-sheet reset state (D4)
    uint8_t read(uint8_t reg);      // reg 0..15 = A4..A1, with the data-sheet read side effects
    void write(uint8_t reg, uint8_t v);
    void advance(uint32_t x1Clocks);  // baud timing, C/T, character shifting (D5)
    bool irq() const;                 // INTR asserted: (ISR & IMR) != 0
    uint8_t ivr() const { return ivr_; }  // vector answered on interrupt acknowledge
    uint8_t outputPins() const { return static_cast<uint8_t>(~opr_); }  // pin = NOT OPR bit
    void setInputPins(uint8_t ip);          // IP5..IP0 levels, bit set = high
    // External 16x clock on input pin 0..5 (0 Hz = none), used by CSR code 1110 (D3): TxCA = IP3,
    // RxCA = IP4, TxCB = IP5, RxCB = IP2. Board wiring: reset() keeps it.
    void setExternalClock(int ipPin, uint32_t hz);
    void receive(int ch, uint8_t byte);     // ch 0 = A, 1 = B: queue a character on RxD (D2, A1)
    bool popTransmitted(int ch, uint8_t& b);  // the oldest character that finished leaving TxD (A2)
    uint32_t rxDropped(int ch) const { return ch_[ch & 1].pending.dropped(); }  // A1, since reset
    uint32_t txDropped(int ch) const { return ch_[ch & 1].sent.dropped(); }     // A2, since reset
    std::size_t rxQueued(int ch) const { return ch_[ch & 1].pending.size(); }  // characters on the wire (A1)
    uint32_t rxBitClocks(int ch) const;     // X1 clocks per receive bit, 0 if unclocked
    uint32_t overruns(int ch) const { return ch_[ch & 1].overruns; }  // lost to overrun since reset
    uint32_t unsupportedWrites() const { return unsupported_; }        // D3
    uint32_t x1Hz() const { return x1Hz_; }

private:
    // A character in flight, counted in 16x ticks; `phase` is the X1 clocks already spent on
    // the current tick, so a tick of any length is counted exactly across advance() calls.
    struct Shifter {
        bool busy = false;
        uint32_t ticksLeft = 0;
        uint32_t phase = 0;
    };
    struct Channel {
        uint8_t mr1 = 0, mr2 = 0, csr = 0;
        bool mr2Next = false;  // the mode-register pointer points at MR2
        bool txEnabled = false, rxEnabled = false;
        // Transmitter: THR (one character of buffering) and the shift register.
        bool txReady = false, txEmpty = false, thrFull = false;
        uint8_t thr = 0, txByte = 0;
        Shifter tx;
        // DECISION A2: 256 sent characters wait to be popped; when full the newest is dropped.
        ByteRing<256> sent;
        // Receiver: characters on the wire, the shift register, the 3-deep FIFO.
        // DECISION A1: the wire holds 1024 characters; later ones are dropped and counted.
        ByteRing<kRxQueue> pending;
        Shifter rx;
        uint8_t rxByte = 0;
        bool held = false;  // a finished character waiting in the shift register for a FIFO slot
        uint8_t heldByte = 0;
        uint8_t fifo[3] = {0, 0, 0};
        int fifoCount = 0;
        bool overrun = false;
        uint32_t overruns = 0;
    };

    uint32_t tick16(int ch, bool rx) const;
    static int extClockPin(int ch, bool rx);
    uint32_t timerTick() const;
    uint32_t charTicks(const Channel& c) const;
    uint8_t dataMask(const Channel& c) const;
    uint8_t status(int ch) const;
    uint8_t isr() const;
    uint8_t readHolding(Channel& c);
    void command(int ch, uint8_t v);
    void writeMode(Channel& c, uint8_t v);
    void writeHolding(Channel& c, uint8_t v);
    void startTx(Channel& c, uint8_t byte);
    void startRx(Channel& c);
    void advanceTx(int ch, uint32_t n);
    void advanceRx(int ch, uint32_t n);
    void advanceCounter(uint32_t n);
    uint32_t counterDivisor() const;
    void startCounter();
    static bool runShifter(Shifter& s, uint32_t k, uint32_t& n);

    uint32_t x1Hz_;
    Channel ch_[2];
    uint8_t acr_ = 0, imr_ = 0, ivr_ = 0x0F, opr_ = 0;
    uint8_t ctur_ = 0, ctlr_ = 0;
    uint8_t ip_ = 0x3F, ipcrDelta_ = 0;
    uint32_t ipClock_[6] = {0, 0, 0, 0, 0, 0};
    bool ipChange_ = false;
    // Counter/timer: `ctCount_` counts down in units of the clock source; `ctPhase_` is the X1
    // clocks into the current unit (D5).
    uint32_t ctCount_ = 0, ctPhase_ = 0;
    bool ctRunning_ = false, ctReady_ = false, ctOdd_ = false;
    uint32_t unsupported_ = 0;
};

}  // namespace ens::duart
