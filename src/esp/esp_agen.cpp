#include "esp/esp_agen.h"

namespace ens::esp {

namespace {
constexpr uint32_t kMask24 = 0xFFFFFF;
constexpr uint32_t kAlign = 0xFFFFF0;  // 20-bit registers keep GPR bits 23..4
}  // namespace

uint32_t Agen::unit() const { return (memsiz & kMask24) + 1; }

int Agen::shift() const {
    int n = 0;
    while (n < 24 && ((memsiz >> n) & 1)) n++;
    return n;
}

bool Agen::isIo(RamOp op) const { return op == RamOp::ReadIo || op == RamOp::WriteIo; }

bool Agen::isWrite(RamOp op) const {
    return op == RamOp::WriteDelay || op == RamOp::WriteTableA || op == RamOp::WriteIo;
}

uint32_t Agen::physical(RamOp op, uint32_t offset24) const {
    const uint32_t keep = ~memsiz & kMask24;  // address bits above the unused (fraction) bits
    const int sh = shift();
    uint32_t s;
    switch (op) {
        case RamOp::ReadDelay:
        case RamOp::WriteDelay:
        case RamOp::ReadDelayDump: {
            // DECISION E7: circular delay; compare and wrap on address bits only, the wrap
            // subtracts DLENGTH + unit so the ring period equals DBASE's period.
            s = ((dbase + offset24) & kMask24) & keep;
            const uint32_t len = dlength & keep;
            if (s > len) s = (s - (len + unit())) & kMask24 & keep;
            return s >> sh;
        }
        case RamOp::ReadTableA:
        case RamOp::WriteTableA:
            // DECISION E7: table address is BASE + offset, masked by MEMSIZ but not DLENGTH.
            return (((abase + offset24) & kMask24) & keep) >> sh;
        case RamOp::ReadTableB:
            return (((bbase + offset24) & kMask24) & keep) >> sh;
        case RamOp::ReadIo:
        case RamOp::WriteIo:
            // DECISION E7: I/O space is absolute, 24-bit offset -> word address >> 4.
            return (offset24 & kMask24) >> 4;
    }
    return 0;
}

void Agen::endOfSample() {
    // DECISION E7: DBASE -= unit each sample; going below 0 reloads DLENGTH (masked like DBASE).
    if (dbase < unit()) dbase = dlength & ~memsiz & kMask24;
    else dbase -= unit();
}

void Agen::writeReg(int spr, uint32_t v24) {
    v24 &= kMask24;
    switch (spr) {
        case 244:
            memsiz = v24;
            // DECISION E20: writing MEMSIZ also masks DBASE (DBASE &= ~MEMSIZ).
            dbase &= ~memsiz & kMask24;
            break;
        // DECISION E7: 20-bit registers keep bits 23..4; low 4 bits read back 0.
        case 245: dlength = v24 & kAlign; break;
        case 246: abase = v24 & kAlign; break;
        case 247: bbase = v24 & kAlign; break;
        case 248: dbase = v24 & kAlign; break;
        default: break;
    }
}

uint32_t Agen::readReg(int spr) const {
    switch (spr) {
        case 244: return memsiz;
        case 245: return dlength;
        case 246: return abase;
        case 247: return bbase;
        case 248: return dbase;
        default: return 0;
    }
}

void DolFifo::push(uint32_t v24) {
    // DECISION E18: 2-deep FIFO; overstuffing bumps out the oldest word.
    v24 &= kMask24;
    if (count_ == 2) {
        q_[0] = q_[1];
        count_ = 1;
    }
    q_[count_++] = v24;
    last_ = v24;
}

bool DolFifo::pop(uint32_t& out) {
    // DECISION E18: a pop on an empty FIFO re-outputs the last word pushed.
    if (count_ == 0) {
        out = last_;
        return true;
    }
    out = q_[0];
    q_[0] = q_[1];
    count_--;
    return true;
}

void DolFifo::dump() {
    // DECISION E18: dump pops and discards; on an empty FIFO it does nothing.
    if (count_ == 0) return;
    q_[0] = q_[1];
    count_--;
}

void DolFifo::reset() {
    // DECISION E18: halt at END resets the FIFO pointers (empties it); the last-pushed
    // register is left as is, so a following empty write may or may not match the datasheet.
    count_ = 0;
}

}  // namespace ens::esp
