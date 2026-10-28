#include "duart/duart.h"

namespace ens::duart {

namespace {

// X1 clocks per 16x baud tick for each CSR code (data sheet table 4-5 and 4-6, crystal
// 3.6864 MHz: divisor = 3'686'400 / (16 x rate)). Index 0 is ACR[7] = 0 (set 1), 1 is set 2.
// The 110, 134.5 and 1050 baud divisors are the rounded ones (table 4-6 errors -0.069, +0.059,
// -0.260 percent). The divisors are fixed, so the rates scale with X1.
constexpr uint16_t kBaudDivisor[2][13] = {
    {4608, 2096, 1712, 1152, 768, 384, 192, 220, 96, 48, 32, 24, 6},   // 50 110 134.5 200 300 600 1200 1050 2400 4800 7200 9600 38.4k
    {3072, 2096, 1712, 1536, 768, 384, 192, 115, 96, 48, 128, 24, 12}  // 75 110 134.5 150 300 600 1200 2000 2400 4800 1800 9600 19.2k
};

constexpr uint32_t kFifoDepth = 3;

}  // namespace

Duart::Duart(uint32_t x1Hz) : x1Hz_(x1Hz) {}

void Duart::reset() {
    // DECISION D4: the data sheet's reset state (para 2.4): SR, IMR, ISR, OPR and OPCR clear,
    // IVR = 0x0F, OP0..OP3 high, channels inactive and TxD marking. It is silent on MR, CSR,
    // ACR, CTUR/CTLR and the counter, which also clear (it says the C/T is in timer mode, but
    // ACR = 0 encodes counter mode from IP2, table 4-4: the ACR bits decide, so the C/T is idle
    // until ACR selects a mode); the output pins are the complement of OPR, so OPR = 0 puts all of
    // OP7..OP0 high. The input pins are external.
    const uint8_t ip = ip_;
    const uint32_t hz = x1Hz_;
    *this = Duart(hz);
    ip_ = ip;
}

uint32_t Duart::timerTick() const {
    // X1 clocks per cycle of the C/T square wave, which is the 16x tick when the C/T is the baud
    // clock (data sheet 3.5.2: period = 2 x preload x source period). 0 if it is not a running
    // timer mode from X1 or X1/16, or the preload is below the minimum 2 (para 4.2.18).
    if (!(acr_ & 0x40)) return 0;
    const uint32_t src = (acr_ >> 4) & 3;
    if (src < 2) return 0;
    const uint32_t preload = (static_cast<uint32_t>(ctur_) << 8) | ctlr_;
    if (preload < 2) return 0;
    return 2 * preload * (src == 3 ? 16u : 1u);
}

uint32_t Duart::tick16(int ch, bool rx) const {
    const Channel& c = ch_[ch & 1];
    const uint32_t sel = rx ? (c.csr >> 4) : (c.csr & 15u);
    if (sel < 13) return kBaudDivisor[(acr_ >> 7) & 1][sel];
    if (sel == 13) return timerTick();
    return 0;  // IP2..IP5 external clocks (D3)
}

uint32_t Duart::rxBitClocks(int ch) const { return 16 * tick16(ch, true); }

uint8_t Duart::dataMask(const Channel& c) const {
    return static_cast<uint8_t>(0xFF >> (3 - (c.mr1 & 3)));  // 5..8 data bits
}

uint32_t Duart::charTicks(const Channel& c) const {
    // DECISION D1: a character takes (1 start + data bits + parity bit if enabled + stop bits)
    // bit times, counted in 16x ticks so the sixteenth-bit stop lengths of MR2[3:0] (table 4-5)
    // are exact; the receiver uses the same length. No bit-level line, no break detection and no
    // receive parity or framing errors.
    const uint32_t bits = 5 + (c.mr1 & 3u);
    const uint32_t parity = ((c.mr1 >> 3) & 3u) == 2 ? 0u : 1u;  // 10 = no parity; 11 = multidrop A/D bit
    const uint32_t code = c.mr2 & 15u;
    const uint32_t stop16 = (bits == 5 || code >= 8) ? 17 + code : 9 + code;
    return 16 * (1 + bits + parity) + stop16;
}

bool Duart::runShifter(Shifter& s, uint32_t k, uint32_t& n) {
    // Spend up to n X1 clocks on a character at k clocks per tick; true when it finishes (n
    // keeps what is left).
    const uint64_t needed = static_cast<uint64_t>(s.ticksLeft) * k - s.phase;
    if (n >= needed) {
        n -= static_cast<uint32_t>(needed);
        s.busy = false;
        return true;
    }
    const uint64_t total = static_cast<uint64_t>(s.phase) + n;
    s.ticksLeft -= static_cast<uint32_t>(total / k);
    s.phase = static_cast<uint32_t>(total % k);
    n = 0;
    return false;
}

void Duart::startTx(Channel& c, uint8_t byte) {
    c.txByte = static_cast<uint8_t>(byte & dataMask(c));
    c.tx.busy = true;
    c.tx.ticksLeft = charTicks(c);
    c.tx.phase = 0;
    c.txReady = true;  // THR is free again as soon as the character is in the shift register
    c.txEmpty = false;
}

void Duart::writeHolding(Channel& c, uint8_t v) {
    if (!c.txEnabled) return;  // para 3.1: characters cannot be loaded while disabled
    if (!c.tx.busy) {
        startTx(c, v);
    } else {
        c.thr = v;
        c.thrFull = true;
        c.txReady = false;
        c.txEmpty = false;
    }
}

void Duart::startRx(Channel& c) {
    // DECISION D2: bytes queued by receive() enter the receiver one per character time at the
    // channel's receive rate, back to back; lost while the receiver is disabled. FIFO of 3 plus
    // the shift register; a character finished with the FIFO full waits in the shift register,
    // and the start bit of the next one overruns it: the waiting character is lost and OE sets
    // (data sheet 3.2 and 4.2.9.4).
    const uint8_t byte = c.pending.front();
    c.pending.pop_front();
    if (c.held) {
        c.held = false;
        c.overrun = true;
        ++c.overruns;
    }
    c.rxByte = static_cast<uint8_t>(byte & dataMask(c));
    c.rx.busy = true;
    c.rx.ticksLeft = charTicks(c);
    c.rx.phase = 0;
}

void Duart::receive(int ch, uint8_t byte) {
    Channel& c = ch_[ch & 1];
    if (c.rxEnabled) c.pending.push_back(byte);  // D2: lost while the receiver is disabled
}

std::vector<uint8_t> Duart::takeTransmitted(int ch) {
    std::vector<uint8_t> out;
    out.swap(ch_[ch & 1].sent);
    return out;
}

void Duart::advanceTx(int ch, uint32_t n) {
    Channel& c = ch_[ch];
    const uint32_t k = tick16(ch, false);
    if (!k) return;
    while (c.tx.busy && runShifter(c.tx, k, n)) {
        c.sent.push_back(c.txByte);
        if (c.thrFull) {
            c.thrFull = false;
            startTx(c, c.thr);
        } else if (c.txEnabled) {
            c.txEmpty = true;
        }
    }
}

void Duart::advanceRx(int ch, uint32_t n) {
    Channel& c = ch_[ch];
    const uint32_t k = tick16(ch, true);
    if (!k) return;
    for (;;) {
        if (!c.rx.busy) {
            if (c.pending.empty()) return;
            startRx(c);
        }
        if (!runShifter(c.rx, k, n)) return;
        if (c.fifoCount < static_cast<int>(kFifoDepth)) {
            c.fifo[c.fifoCount++] = c.rxByte;
        } else {
            c.held = true;
            c.heldByte = c.rxByte;
        }
    }
}

uint8_t Duart::readHolding(Channel& c) {
    if (c.fifoCount == 0) return 0;
    const uint8_t v = c.fifo[0];
    c.fifo[0] = c.fifo[1];
    c.fifo[1] = c.fifo[2];
    --c.fifoCount;
    if (c.held) {  // the waiting character moves from the shift register into the FIFO
        c.held = false;
        c.fifo[c.fifoCount++] = c.heldByte;
    }
    return v;
}

uint32_t Duart::counterDivisor() const {
    // X1 clocks per C/T count (table 4-4): timer from X1 or X1/16, counter from X1/16. The IP2
    // and TxCA/TxCB sources are not modelled (D3): the C/T does not count from them.
    const uint32_t src = (acr_ >> 4) & 7;
    if (src == 6) return 1;
    if (src == 7 || src == 3) return 16;
    return 0;
}

void Duart::startCounter() {
    const uint32_t preload = (static_cast<uint32_t>(ctur_) << 8) | ctlr_;
    ctCount_ = (acr_ & 0x40) && preload == 0 ? 0x10000u : preload;
    ctPhase_ = 0;
    ctRunning_ = true;
    ctOdd_ = false;
}

void Duart::advanceCounter(uint32_t n) {
    // DECISION D5: n counts whole X1 clocks. The X1/16 prescaler phase is kept in ctPhase_, so
    // single-clock and bulk calls count alike; a start command restarts the phase.
    const uint32_t div = counterDivisor();
    // Only counter mode waits for a start command; a timer runs continuously (para 3.5.2).
    if (!div || (!(acr_ & 0x40) && !ctRunning_)) return;
    const uint64_t total = static_cast<uint64_t>(ctPhase_) + n;
    uint64_t steps = total / div;
    ctPhase_ = static_cast<uint32_t>(total % div);
    if (!steps) return;
    if (acr_ & 0x40) {
        // Timer: terminal count after `preload` counts, then every `preload` counts (square wave
        // period 2 x preload); ISR[3] sets on every second one (para 3.5.2, 4.2.15.5). A new
        // preload is picked up at the terminal count.
        uint64_t preload = (static_cast<uint32_t>(ctur_) << 8) | ctlr_;
        if (preload == 0) preload = 0x10000;
        uint64_t count = ctCount_ ? ctCount_ : preload;
        if (steps < count) {
            ctCount_ = static_cast<uint32_t>(count - steps);
            return;
        }
        steps -= count;
        const uint64_t tcs = 1 + steps / preload;
        ctCount_ = static_cast<uint32_t>(preload - steps % preload);
        if (tcs >= (ctOdd_ ? 1u : 2u)) ctReady_ = true;
        ctOdd_ = ((tcs + (ctOdd_ ? 1u : 0u)) & 1) != 0;
    } else {
        // Counter: ISR[3] sets at terminal count; the count then rolls over to 0xFFFF and runs on.
        const uint64_t count = ctCount_ ? ctCount_ : 0x10000;
        if (steps < count) {
            ctCount_ = static_cast<uint32_t>(count - steps);
            return;
        }
        ctReady_ = true;
        ctCount_ = static_cast<uint32_t>((0x10000 - (steps - count) % 0x10000) & 0xFFFF);
    }
}

void Duart::advance(uint32_t x1Clocks) {
    for (int ch = 0; ch < 2; ++ch) {
        advanceTx(ch, x1Clocks);
        advanceRx(ch, x1Clocks);
    }
    advanceCounter(x1Clocks);
}

void Duart::setInputPins(uint8_t ip) {
    ip &= 0x3F;
    const uint8_t changed = static_cast<uint8_t>((ip ^ ip_) & 0x0F);
    ip_ = ip;
    ipcrDelta_ |= changed;
    if (changed & acr_ & 0x0F) ipChange_ = true;
}

uint8_t Duart::status(int ch) const {
    const Channel& c = ch_[ch];
    uint8_t v = 0;
    if (c.fifoCount > 0) v |= 0x01;
    if (c.fifoCount == static_cast<int>(kFifoDepth)) v |= 0x02;
    if (c.txReady) v |= 0x04;
    if (c.txEmpty) v |= 0x08;
    if (c.overrun) v |= 0x10;
    return v;  // PE, FE and RB (bits 7..5) are never set (D1)
}

uint8_t Duart::isr() const {
    uint8_t v = 0;
    for (int ch = 0; ch < 2; ++ch) {
        const Channel& c = ch_[ch];
        const bool rx = (c.mr1 & 0x40) ? c.fifoCount == static_cast<int>(kFifoDepth) : c.fifoCount > 0;
        if (c.txReady) v |= static_cast<uint8_t>(0x01 << (4 * ch));
        if (rx) v |= static_cast<uint8_t>(0x02 << (4 * ch));
    }
    if (ctReady_) v |= 0x08;
    if (ipChange_) v |= 0x80;
    return v;  // the break-change bits (2, 6) never set (D1)
}

bool Duart::irq() const { return (isr() & imr_) != 0; }

uint8_t Duart::read(uint8_t reg) {
    reg &= 15;
    if (!(reg & 4)) {
        Channel& c = ch_[(reg >> 3) & 1];
        switch (reg & 3) {
            case 0: {
                const uint8_t v = c.mr2Next ? c.mr2 : c.mr1;
                c.mr2Next = true;
                return v;
            }
            case 1: return status((reg >> 3) & 1);
            case 2: return 0xFF;  // "do not access"
            default: return readHolding(c);
        }
    }
    switch (reg) {
        case 4: {
            const uint8_t v = static_cast<uint8_t>((ipcrDelta_ << 4) | (ip_ & 0x0F));
            ipcrDelta_ = 0;
            ipChange_ = false;
            return v;
        }
        case 5: return isr();
        case 6: return static_cast<uint8_t>(ctCount_ >> 8);
        case 7: return static_cast<uint8_t>(ctCount_);
        case 12: return ivr_;
        case 13: return static_cast<uint8_t>(0xC0 | ip_);  // bit 7 reads 1, bit 6 is IACK (negated)
        case 14:
            startCounter();
            return 0xFF;
        default:  // 15: stop counter / clear ISR[3]; a timer keeps running
            ctReady_ = false;
            if (!(acr_ & 0x40)) ctRunning_ = false;
            return 0xFF;
    }
}

void Duart::writeMode(Channel& c, uint8_t v) {
    // DECISION D3: a write that selects an unmodelled feature (here RxRTS, multidrop, echo and
    // loopback modes, TxRTS, CTS control) counts once and otherwise stores the value.
    bool bad;
    if (!c.mr2Next) {
        c.mr1 = v;
        bad = (v & 0x80) || ((v >> 3) & 3) == 3;
    } else {
        c.mr2 = v;
        bad = (v & 0xF0) != 0;
    }
    c.mr2Next = true;
    if (bad) ++unsupported_;
}

void Duart::command(int ch, uint8_t v) {
    Channel& c = ch_[ch];
    switch ((v >> 4) & 7) {
        case 1: c.mr2Next = false; break;
        case 2:  // reset receiver: disabled, FIFO and status cleared, character in progress lost
            c.rxEnabled = false;
            c.fifoCount = 0;
            c.held = false;
            c.overrun = false;
            c.rx.busy = false;
            c.pending.clear();
            break;
        case 3:  // reset transmitter: disabled, TxRDY and TxEMT cleared, character lost
            c.txEnabled = false;
            c.txReady = c.txEmpty = c.thrFull = false;
            c.tx.busy = false;
            break;
        case 4: c.overrun = false; break;
        case 5: case 6: case 7: ++unsupported_; break;  // D3: break commands
        default: break;
    }
    const uint8_t txCmd = (v >> 2) & 3;
    if (txCmd == 1 && !c.txEnabled) {
        c.txEnabled = true;
        c.txReady = true;
        c.txEmpty = !c.tx.busy;
    } else if (txCmd == 2) {  // a character in the shift register still finishes
        c.txEnabled = false;
        c.txReady = c.txEmpty = c.thrFull = false;
    }
    const uint8_t rxCmd = v & 3;
    if (rxCmd == 1) {
        c.rxEnabled = true;
    } else if (rxCmd == 2) {  // the character being received is lost
        c.rxEnabled = false;
        c.rx.busy = false;
        c.pending.clear();
    }
}

void Duart::write(uint8_t reg, uint8_t v) {
    reg &= 15;
    if (!(reg & 4)) {
        const int ch = (reg >> 3) & 1;
        Channel& c = ch_[ch];
        switch (reg & 3) {
            case 0: writeMode(c, v); break;
            case 1:
                c.csr = v;
                if ((v & 15) >= 14 || (v >> 4) >= 14) ++unsupported_;  // D3: external IP clocks
                break;
            case 2: command(ch, v); break;
            default: writeHolding(c, v); break;
        }
        return;
    }
    switch (reg) {
        case 4: {
            // DECISION D6: a free-running timer has no start command to fix its phase. When ACR[6:4]
            // changes, the count is cleared so that it loads the preload at the next advance() (a
            // preload written after ACR but before then is seen), and the counter run state is
            // dropped, so leaving counter mode stops it (para 3.5, 3.5.2).
            if (((acr_ ^ v) & 0x70) != 0) {
                ctCount_ = 0;
                ctPhase_ = 0;
                ctOdd_ = false;
                ctRunning_ = false;
            }
            acr_ = v;
            const uint8_t src = (v >> 4) & 7;  // 0, 1, 2, 4, 5: IP2 or TxC 1X sources (D3)
            if (src != 0 && src != 3 && src != 6 && src != 7) ++unsupported_;
            break;
        }
        case 5: imr_ = v; break;
        case 6: ctur_ = v; break;
        case 7: ctlr_ = v; break;
        case 12: ivr_ = v; break;
        case 13:
            if (v) ++unsupported_;  // D3: OPCR special outputs; OP7..OP0 stay the OPR complement
            break;
        case 14: opr_ |= v; break;
        case 15: opr_ = static_cast<uint8_t>(opr_ & ~v); break;
        default: break;
    }
}

}  // namespace ens::duart
