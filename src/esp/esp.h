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
    bool halted() const { return halted_; }

    // Direct state (tests, trace runner). Addresses 0..255 with SPR semantics.
    uint32_t gpr(int addr) const;
    void setGpr(int addr, uint32_t v24);
    uint64_t instr(int i) const;
    void setInstr(int i, uint64_t w48);
    const esp::Mac& mac() const { return mac_; }
    const esp::Agen& agen() const { return agen_; }

    // ---- host interface (ESP notes part 2 §5.1-§5.4) -------------------------------------
    // 8-bit host register map: $00-$02 GPR, $03-$08 INSTR, $09-$0B DIL, $0C-$0E DOL,
    // $0F-$11 DADR, $12 host control, $14 RAM access, $16 PC, $17 refresh, $18 serial control,
    // $1F halt enable / frame counter, $80/$A0/$C0/$E0 select registers.
    uint8_t hostRead(uint8_t addr) const;
    void hostWrite(uint8_t addr, uint8_t v);
    // HALT pin level. Sampled at END (runSample) and while halted: a low level resumes at pc 0.
    void setHaltPin(bool high);
    // One instruction period while halted (about 300 ns): one pending GPR/INSTR transfer,
    // and independently one RAM-clear write or one DBASE refresh decrement. No-op if running.
    // Halted cadence contract: while halted nothing advances (host requests, RAM clear,
    // refresh) unless the caller calls idleStep(), about once per 300 ns of emulated time
    // (~3.33 per microsecond). A RAM clear needs DLENGTH/unit + 1 steps to finish (unit =
    // MEMSIZ + 1; 256 for DLENGTH 0x00FF00 with MEMSIZ 0xFF, 65536 for the firmware's
    // DLENGTH 0xFFFFFF), and a host GPR/INSTR request needs one step.
    void idleStep();
    int dolFifoSize() const { return fifo_.size(); }
    uint8_t serialControl() const { return serCtl_; }  // host serial control register as written

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
    void latchDil(uint16_t data);  // DIL = data << 8 (RAM data is left-justified)
    void hostTransfer();   // the pending GPR/INSTR host request, if any
    void hostRamAccess();  // DADR HI write while halted: one RAM/IO cycle
    void haltNow();        // immediate halt (halt-enable write with HALT high)

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

    // Host interface state.
    struct HostRequest {
        bool valid = false;
        bool read = false, wrGpr = false, wrInstr = false;
        uint8_t addr = 0;
    };
    bool halted_ = false, haltPin_ = false;
    uint32_t hostGpr_ = 0;     // $00-$02
    uint64_t hostInstr_ = 0;   // $03-$08
    uint16_t hostDol_ = 0;     // $0C-$0D (16-bit, left-justified)
    uint32_t dadr_ = 0;        // $0F-$11, bits 23..4
    uint8_t ramCtl_ = 0;       // $14
    uint8_t serCtl_ = 0;       // $18
    uint8_t selReg_[4] = {0, 0, 0, 0};  // read-back of $80, $A0, $C0, $E0
    bool refreshDisable_ = false, clearActive_ = false;
    HostRequest req_;
};

}  // namespace ens
