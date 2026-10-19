#pragma once
// ES5510 ESP core: microprogram execution with the documented ALU/MUL/RAM pipeline
// (ESP notes §4.3), the special purpose registers (§3.2) and the serial port latches.
#include <cstdint>

#include "esp/esp_agen.h"
#include "esp/esp_alu.h"
#include "esp/esp_mac.h"

namespace ens {

// External RAM / I/O bus. Word addresses come from the address generator; data are the
// 16 bits on the RAM data pins (GPR bits 23..8).
// Address range the core may pass (implementations must mask to their memory size):
//   io == false (delay/table): 0 .. 0xFFFFFF >> shift, where shift = trailing 1 bits of
//     MEMSIZ; i.e. up to 24-bit word addresses when MEMSIZ = 0, 16-bit when MEMSIZ = 0xFF.
//     Out-of-contract offsets can exceed DLENGTH/unit, so do not assume the delay ring.
//   io == true: 0 .. 0xFFFFF (20-bit, offset >> 4).
struct EspBus {
    virtual ~EspBus() = default;
    virtual uint16_t read(bool io, uint32_t wordAddr) = 0;
    virtual void write(bool io, uint32_t wordAddr, uint16_t v) = 0;
};

class Esp {
public:
    static constexpr int kProgramSize = 160;

    explicit Esp(EspBus& bus);
    void reset();
    int runSample();  // executes from PC until END (E11 cap); returns steps run
    void setSerialIn(int port, int16_t l, int16_t r);  // ports 0..3, latched into SERnL/R
    void serialOut(int port, int16_t& l, int16_t& r) const;
    bool halted() const;

    // Direct state (tests, trace runner). Addresses 0..255 with SPR semantics.
    uint32_t gpr(int addr) const;
    void setGpr(int addr, uint32_t v24);
    uint64_t instr(int i) const;
    void setInstr(int i, uint64_t w48);
    const esp::Mac& mac() const { return mac_; }
    const esp::Agen& agen() const { return agen_; }
    // Task 9 adds the host interface.

private:
    struct PendingAlu {
        bool valid = false;
        esp::Op op = esp::Op::END;
        esp::AluResult r{};
        uint8_t dest = 0;
        bool toA = false, toFifo = false;
        bool skippable = false;  // Skip bit of the word (E4: never sets the CCR unless CMP)
        bool skipped = false;    // skip condition was true (E5: no writes)
    };
    struct PendingMul {
        bool valid = false;
        uint32_t value = 0;  // MACH after the accumulate (Z-bus result)
        uint8_t dest = 0;
        bool toC = false, toFifo = false, skipped = false;
    };
    struct PendingRam {
        bool valid = false;
        esp::RamOp op = esp::RamOp::ReadDelay;
        uint32_t addr = 0;
    };

    uint32_t readReg(int addr) const;  // shared by ALU, MUL and host
    void writeReg(int addr, uint32_t v24);
    bool step();  // one microinstruction; true after an END step
    void commitMul();
    void commitAlu();
    void ramCycle();

    EspBus& bus_;
    uint32_t gpr_[192];
    uint64_t instr_[kProgramSize];
    uint16_t ser_[8];  // index = address - 234: SER0R, SER0L, ..., SER3R, SER3L
    uint32_t dil_ = 0, sigreg_ = 0, cmr_ = 0;
    esp::Flags ccr_;
    esp::Mac mac_;
    esp::Agen agen_;
    esp::DolFifo fifo_;
    PendingAlu pAlu_;
    PendingMul pMul_;
    PendingRam pRam_;
    int pc_ = 0;
};

}  // namespace ens
