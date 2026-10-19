#pragma once
// ES5510 ESP address generator (DBASE/DLENGTH/ABASE/BBASE/MEMSIZ) and the 2-word DOL FIFO.
// Plain value types; the Esp core owns one of each.
#include <cstdint>

namespace ens::esp {

// RAM control field (B5..B3) of the microinstruction, datasheet section 4.1.6.
enum class RamOp : uint8_t {
    ReadDelay = 0,
    WriteDelay = 1,
    ReadTableA = 2,
    WriteTableA = 3,
    ReadTableB = 4,
    ReadDelayDump = 5,  // read delay line + dump (pop and discard) the DOL FIFO
    ReadIo = 6,
    WriteIo = 7
};

struct Agen {
    // 24-bit GPR-aligned values (bits 23..4 for the 20-bit registers, see writeReg).
    uint32_t memsiz = 0, dlength = 0, abase = 0, bbase = 0, dbase = 0;

    uint32_t unit() const;  // DBASE decrement and address LSB weight: MEMSIZ + 1
    int shift() const;      // position of the address LSB: trailing 1 bits of MEMSIZ

    // Word address (RAM or I/O) for an access with the given 24-bit offset GPR value.
    uint32_t physical(RamOp op, uint32_t offset24) const;
    bool isIo(RamOp op) const;
    bool isWrite(RamOp op) const;  // delay/table/I-O writes; the dump op is not a write

    // End of sample: DBASE -= unit; when that would go below 0, reload DLENGTH.
    void endOfSample();

    // spr: 244 MEMSIZ (write side of the DIL address), 245 DLENGTH, 246 ABASE, 247 BBASE,
    // 248 DBASE. Other numbers are ignored / read 0.
    void writeReg(int spr, uint32_t v24);
    uint32_t readReg(int spr) const;
};

// 2-word deep Data Output Latch FIFO (datasheet section 3.6.3). Values are kept as the
// 24-bit words pushed; the 16-bit left-justified truncation is done by the RAM write.
class DolFifo {
public:
    void push(uint32_t v24);  // full: the oldest word is bumped out
    bool pop(uint32_t& out);  // empty: re-outputs the last pushed word; always returns true
    void dump();              // pop and discard; does nothing when empty
    void reset();             // empty the FIFO (halt at END); the last-pushed word is kept
    int size() const { return count_; }

private:
    uint32_t q_[2] = {0, 0};  // q_[0] is the oldest
    int count_ = 0;
    uint32_t last_ = 0;
};

}  // namespace ens::esp
