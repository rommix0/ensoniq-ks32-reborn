#include "esp/esp.h"

namespace ens {

using esp::Op;
using esp::RamOp;

namespace {

constexpr uint32_t kMask24 = 0xFFFFFF;

// Operand select field (bits 11..8), ESP notes §4.1.3 table rows 0..F, verbatim.
struct OpSel {
    bool aluSrcDil;  // ALU X-bus (A-side) input is DIL instead of reg A
    bool aluToA, aluToFifo;
    bool mulSrcDil;  // MUL X-bus (C-side) input is DIL instead of reg C
    bool mulToC, mulToFifo;
};

constexpr OpSel kOpSel[16] = {
    //  aluSrc  A      FIFO   mulSrc C      FIFO
    {false, true, false, false, true, false},   // 0: A    > A      | C   > C
    {false, true, false, false, false, true},   // 1: A    > A      | C   > FIFO
    {false, true, false, false, true, true},    // 2: A    > A      | C   > C/FIFO
    {false, true, false, true, true, false},    // 3: A    > A      | DIL > C
    {false, true, false, true, true, true},     // 4: A    > A      | DIL > C/FIFO
    {false, false, true, false, true, false},   // 5: A    > FIFO   | C   > C
    {false, false, true, true, true, false},    // 6: A    > FIFO   | DIL > C
    {false, true, true, false, true, false},    // 7: A    > A/FIFO | C   > C
    {false, true, true, true, true, false},     // 8: A    > A/FIFO | DIL > C
    {true, true, false, false, true, false},    // 9: DIL  > A      | C   > C
    {true, true, false, false, false, true},    // A: DIL  > A      | C   > FIFO
    {true, true, false, false, true, true},     // B: DIL  > A      | C   > C/FIFO
    {true, true, false, true, true, false},     // C: DIL  > A      | DIL > C
    {true, true, false, true, true, true},      // D: DIL  > A      | DIL > C/FIFO
    {true, true, true, false, true, false},     // E: DIL  > A/FIFO | C   > C
    {true, true, true, true, true, false},      // F: DIL  > A/FIFO | DIL > C
};

constexpr int kSer0R = 234, kSer3L = 241, kMacl = 242, kMach = 243, kDil = 244;
constexpr int kDbase = 248, kSigreg = 249, kCcr = 250, kCmr = 251;
constexpr uint32_t kShiftOneBit = 0x400000;  // SIGREG bit 22
constexpr uint32_t kCmrMask = 0xFC0000;      // N C V LT Z NOT

}  // namespace

Esp::Esp(EspBus& bus) : bus_(bus) { reset(); }

void Esp::reset() {
    // DECISION E23: reset clears GPRs, program, SPRs, the MAC, the AGEN, the DOL FIFO
    // (a fresh one, including its last-pushed word) and the pipeline; PC = 0.
    for (uint32_t& g : gpr_) g = 0;
    for (uint64_t& w : instr_) w = 0;
    for (uint16_t& s : ser_) s = 0;
    dil_ = sigreg_ = cmr_ = 0;
    ccr_ = esp::Flags{};
    mac_ = esp::Mac{};
    agen_ = esp::Agen{};
    fifo_ = esp::DolFifo{};
    pAlu_ = PendingAlu{};
    pMul_ = PendingMul{};
    pRam_ = PendingRam{};
    pc_ = 0;
    // DECISION E23 (host side): the host registers return to 0 and the ESP is running; the
    // HALT pin is an external level and is kept.
    halted_ = false;
    hostGpr_ = 0;
    hostInstr_ = 0;
    hostDol_ = 0;
    dadr_ = 0;
    ramCtl_ = serCtl_ = 0;
    for (uint8_t& r : selReg_) r = 0;
    refreshDisable_ = clearActive_ = false;
    req_ = HostRequest{};
}

// ---- register file -------------------------------------------------------------------

uint32_t Esp::readReg(int addr) const {
    if (addr < 0 || addr > 255) return 0;
    if (addr < 192) return gpr_[addr];
    if (addr < kSer0R) return 0;  // DECISION E9: addresses 192..233 read 0
    // DECISION E8: SER registers are 16-bit, left-justified at bits 23..8.
    if (addr <= kSer3L) return static_cast<uint32_t>(ser_[addr - kSer0R]) << 8;
    switch (addr) {
        case kMacl: return mac_.readLow();
        case kMach: return mac_.readHigh();
        case kDil: return dil_;  // 244 read = DIL (the write side is MEMSIZ)
        case kSigreg: return sigreg_;
        case kCcr: return esp::packCcr(ccr_);
        case kCmr: return cmr_;
        // DECISION E12: constants -1, MIN, MAX, 0.
        case 252: return 0xFFFFFF;
        case 253: return 0x800000;
        case 254: return 0x7FFFFF;
        case 255: return 0;
        default: return agen_.readReg(addr);  // 245..248 DLENGTH, ABASE, BBASE, DBASE
    }
}

void Esp::writeReg(int addr, uint32_t v24) {
    v24 &= kMask24;
    if (addr < 0 || addr > 255) return;
    if (addr < 192) {
        gpr_[addr] = v24;
        return;
    }
    if (addr < kSer0R) return;  // DECISION E9: addresses 192..233 ignore writes
    if (addr <= kSer3L) {
        ser_[addr - kSer0R] = static_cast<uint16_t>(v24 >> 8);  // DECISION E8: keep bits 23..8
        return;
    }
    switch (addr) {
        case kMacl: mac_.writeLow(v24); return;
        case kMach: mac_.writeHigh(v24); return;
        // DECISION E22: SIGREG keeps only bit 22, CCR bits 23..19, CMR bits 23..18.
        case kSigreg: sigreg_ = v24 & kShiftOneBit; return;
        case kCcr: ccr_ = esp::unpackCcr(v24); return;
        case kCmr: cmr_ = v24 & kCmrMask; return;
        default:
            if (addr <= kDbase) agen_.writeReg(addr, v24);  // 244 MEMSIZ .. 248 DBASE
            // DECISION E12: writes to the constants 252..255 are discarded.
            return;
    }
}

uint32_t Esp::gpr(int addr) const { return readReg(addr); }
void Esp::setGpr(int addr, uint32_t v24) { writeReg(addr, v24); }

uint64_t Esp::instr(int i) const { return (i >= 0 && i < kProgramSize) ? instr_[i] : 0; }

void Esp::setInstr(int i, uint64_t w48) {
    if (i >= 0 && i < kProgramSize) instr_[i] = w48 & 0xFFFFFFFFFFFFull;
}

// ---- serial ports (plain latches until Task 9 adds serial control) --------------------

void Esp::setSerialIn(int port, int16_t l, int16_t r) {
    if (port < 0 || port > 3) return;
    ser_[2 * port] = static_cast<uint16_t>(r);
    ser_[2 * port + 1] = static_cast<uint16_t>(l);
}

void Esp::serialOut(int port, int16_t& l, int16_t& r) const {
    if (port < 0 || port > 3) {
        l = r = 0;
        return;
    }
    r = static_cast<int16_t>(ser_[2 * port]);
    l = static_cast<int16_t>(ser_[2 * port + 1]);
}

// ---- execution -------------------------------------------------------------------------

int Esp::runSample() {
    if (halted_) return 0;  // halted: nothing executes until HALT goes low
    int steps = 0;
    // DECISION E11: step() treats word 159 as END, so this runs at most 160 steps.
    do {
        ++steps;
    } while (!step());
    return steps;
}

// DECISION E17: the nine-phase step. ALU(k-1) commits after MUL(k) has fetched its operands
// (so an ALU result reaches the MUL two steps later), MUL(k-1) commits before ALU(k) fetches
// (next step), and ALU(k-1)'s MACH/MACL preload lands just before MUL(k) accumulates.
bool Esp::step() {
    const uint64_t w = instr_[pc_];
    const uint8_t d = static_cast<uint8_t>(w >> 40);
    const uint8_t c = static_cast<uint8_t>(w >> 32);
    const uint8_t b = static_cast<uint8_t>(w >> 24);
    const uint8_t a = static_cast<uint8_t>(w >> 16);
    const Op op = static_cast<Op>((w >> 12) & 0xF);
    const OpSel& os = kOpSel[(w >> 8) & 0xF];
    const bool skippable = ((w >> 7) & 1) != 0;
    const bool macBit = ((w >> 6) & 1) != 0;
    const RamOp ram = static_cast<RamOp>((w >> 3) & 7);

    // (1) MUL(k-1) result to C and/or the DOL FIFO.
    commitMul();
    // (2) MUL(k) operand fetch and product.
    const uint32_t mulX = os.mulSrcDil ? dil_ : readReg(c);
    const int64_t prod = mac_.product(mulX, readReg(d), (sigreg_ & kShiftOneBit) != 0);
    // (3) ALU(k-1) result: register, DOL FIFO, CCR, MACH/MACL preload.
    commitAlu();
    // (4) Skip for step k. DECISION E4: CMP ignores the Skip bit.
    const bool skip = skippable && op != Op::CMP && esp::skipCondition(esp::packCcr(ccr_), cmr_);
    // (5) ALU(k) operand fetch. One-operand ops use B only; B = 244 reads DIL (case ii).
    const uint32_t aluA = os.aluSrcDil ? dil_ : readReg(a);
    pAlu_ = {true, op, esp::alu(op, aluA, readReg(b), ccr_), a, os.aluToA, os.aluToFifo,
             skippable, skip};
    // (6) MUL(k) accumulate. DECISION E5: a skipped step leaves the accumulator alone.
    if (!skip) mac_.accumulate(prod, macBit);
    pMul_ = {true, mac_.readHigh(), c, os.mulToC, os.mulToFifo, skip};
    // (7) RAM cycle of word k-1. DECISION E5: runs even when that word was skipped.
    ramCycle();
    // A host halt-enable write issued from the bus callback stops the program right here.
    if (halted_) return true;
    // (8) AGEN: address for word k from GPR[pc] and RAM-ctrl(k).
    pRam_ = {true, ram, agen_.physical(ram, gpr_[pc_])};

    // (9) DECISION E19: END (or E11: word 159) ends the sample after its MUL and RAM fields
    // have been issued; their results and the RAM cycle carry into the next sample.
    if (op == Op::END || pc_ == kProgramSize - 1) {
        agen_.endOfSample();
        // DECISION E19: at most one pending host GPR/INSTR transfer, then sample HALT.
        hostTransfer();
        if (haltPin_) {
            // DECISION E18: an END taken while HALT is high empties the DOL FIFO.
            halted_ = true;
            fifo_.reset();
        }
        pc_ = 0;
        return true;
    }
    ++pc_;
    return false;
}

void Esp::commitMul() {
    if (!pMul_.valid || pMul_.skipped) return;
    if (pMul_.toC) writeReg(pMul_.dest, pMul_.value);
    if (pMul_.toFifo) fifo_.push(pMul_.value);
}

void Esp::commitAlu() {
    if (!pAlu_.valid) return;
    const PendingAlu& p = pAlu_;
    // DECISION E4: a skippable word never sets the CCR (executed or not), except CMP.
    // DECISION E19: END is an ALU NOP and leaves the CCR alone.
    if (p.op != Op::END && (!p.skippable || p.op == Op::CMP)) ccr_ = p.r.flags;
    // DECISION E5: a skipped word writes no register and pushes no DOL word.
    if (p.skipped || !p.r.writes) return;
    // DECISION E17: flags first, so an explicit CCR destination overrides the flag update.
    if (p.toA) writeReg(p.dest, p.r.value);
    if (p.toFifo) fifo_.push(p.r.value);
}

// DECISION E18: the RAM cycle for the word issued at the previous step. A read latches
// DIL = data << 8 here, after this step's operand fetches, so it is visible from the next
// step (two words after the issuing word). A write pops the DOL FIFO (empty: the last
// pushed word) and outputs bits 23..8. Read-delay/dump reads and discards one FIFO word.
void Esp::ramCycle() {
    if (!pRam_.valid) return;
    const bool io = agen_.isIo(pRam_.op);
    if (agen_.isWrite(pRam_.op)) {
        uint32_t v = 0;
        fifo_.pop(v);
        bus_.write(io, pRam_.addr, static_cast<uint16_t>((v >> 8) & 0xFFFF));
        return;
    }
    if (pRam_.op == RamOp::ReadDelayDump) fifo_.dump();
    dil_ = (static_cast<uint32_t>(bus_.read(io, pRam_.addr)) << 8) & kMask24;
}

// ---- host interface ----------------------------------------------------------------------

namespace {
constexpr int kHostCtl = 0x12, kRamCtl = 0x14;
constexpr uint8_t kCtlBusy = 4, kCtlClear = 2, kCtlRefreshOff = 1;
}  // namespace

void Esp::setHaltPin(bool high) {
    haltPin_ = high;
    if (!high && halted_) {
        halted_ = false;  // level sampled low while halted: resume at pc 0
        // DECISION E25: a RAM clear still running when HALT is released is abandoned.
        clearActive_ = false;
    }
}

// DECISION E24: a halt-enable write with HALT high stops the ESP at once, wherever it is. The
// in-flight pipeline (pending MUL/ALU results and the RAM cycle) is discarded and the DOL FIFO
// pointers reset, because the machine state is declared unknown; execution restarts at pc 0.
void Esp::haltNow() {
    if (halted_) return;
    halted_ = true;
    pc_ = 0;
    pAlu_ = PendingAlu{};
    pMul_ = PendingMul{};
    pRam_ = PendingRam{};
    fifo_.reset();
}

// DECISION E26: one request latch. A select-register write replaces whatever request is still
// pending (the data registers keep their last contents). A read captures GPR/SPR and INSTR; an
// address with no instruction word (>= 160) captures 0 for the instruction. GPR addresses use
// the program-side register map (SPR semantics; holes 192..233 read 0 and ignore writes).
void Esp::hostTransfer() {
    if (!req_.valid) return;
    const int a = req_.addr;
    if (req_.read) {
        hostGpr_ = readReg(a);
        hostInstr_ = instr(a);
    } else {
        if (req_.wrGpr) writeReg(a, hostGpr_);
        if (req_.wrInstr) setInstr(a, hostInstr_);
    }
    req_.valid = false;
}

// DECISION E27: DADR HI starts one RAM/IO cycle only while halted (running: ignored). The
// DADR value bypasses the address generator: RAM = (DADR & ~MEMSIZ) >> shift (the table
// addressing rule), I/O = DADR >> 4 (20-bit). A write sends the 16-bit host DOL; a read
// latches DIL = data << 8. The cycle completes at once (datasheet: within 6 clocks).
void Esp::hostRamAccess() {
    if (!halted_) return;
    const bool io = (ramCtl_ & 0x40) == 0;
    const uint32_t addr = io ? ((dadr_ >> 4) & 0xFFFFF)
                             : (((dadr_ & ~agen_.memsiz) & kMask24) >> agen_.shift());
    if (ramCtl_ & 0x80) dil_ = (static_cast<uint32_t>(bus_.read(io, addr)) << 8) & kMask24;
    else bus_.write(io, addr, hostDol_);
}

// DECISION E28: a halted instruction period does two independent things: it services one
// pending GPR/INSTR host request (internal DRAM) and, for the external RAM, either one
// RAM-clear write or one DBASE refresh decrement. The clear and the refresh never overlap.
void Esp::idleStep() {
    if (!halted_) return;
    hostTransfer();
    if (clearActive_) {
        // DECISION E25: DBASE starts at DLENGTH and counts down by one unit per period, writing
        // zero at each address; the bit clears once address 0 has been written.
        const uint32_t addr = ((agen_.dbase & ~agen_.memsiz) & kMask24) >> agen_.shift();
        bus_.write(false, addr, 0);
        if (agen_.dbase < agen_.unit()) clearActive_ = false;
        else agen_.dbase -= agen_.unit();
    } else if (!refreshDisable_) {
        agen_.endOfSample();  // refresh: same decrement/test/reload as the END update
    }
}

uint8_t Esp::hostRead(uint8_t addr) {
    switch (addr) {
        case 0x00: return static_cast<uint8_t>(hostGpr_ >> 16);
        case 0x01: return static_cast<uint8_t>(hostGpr_ >> 8);
        case 0x02: return static_cast<uint8_t>(hostGpr_);
        case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
            return static_cast<uint8_t>(hostInstr_ >> (40 - 8 * (addr - 3)));
        case 0x08: return static_cast<uint8_t>(hostInstr_) & 0xF8;  // bits 2..0 unused
        case 0x09: return static_cast<uint8_t>(dil_ >> 16);
        case 0x0A: return static_cast<uint8_t>(dil_ >> 8);
        case 0x0B: return 0;  // DIL is 16 bits wide
        case 0x0C: return static_cast<uint8_t>(hostDol_ >> 8);
        case 0x0D: return static_cast<uint8_t>(hostDol_);
        case 0x0E: return 0;  // DECISION E10: the DOL low byte reads 0x00 (text), not 0xFF (table)
        case 0x0F: return static_cast<uint8_t>(dadr_ >> 16);
        case 0x10: return static_cast<uint8_t>(dadr_ >> 8);
        case 0x11: return static_cast<uint8_t>(dadr_) & 0xF0;
        case kHostCtl:
            return static_cast<uint8_t>((req_.valid ? kCtlBusy : 0) |
                                        (clearActive_ ? kCtlClear : 0) |
                                        (refreshDisable_ ? kCtlRefreshOff : 0));
        case kRamCtl: return ramCtl_;
        case 0x16: return static_cast<uint8_t>(pc_);
        // DECISION E29: $17 (refresh counter) and $1F read (frame counter) are test-only and
        // read 0; the serial control register has bits 1:0 hard-wired to 1.
        case 0x18: return static_cast<uint8_t>((serCtl_ & 0xFC) | 0x03);
        case 0x80: return selReg_[0];
        case 0xA0: return selReg_[1];
        case 0xC0: return selReg_[2];
        case 0xE0: return selReg_[3];
        default: return 0;  // DECISION E26: unlisted host addresses read 0 and ignore writes
    }
}

void Esp::hostWrite(uint8_t addr, uint8_t v) {
    switch (addr) {
        case 0x00: hostGpr_ = (hostGpr_ & 0x00FFFF) | (static_cast<uint32_t>(v) << 16); return;
        case 0x01: hostGpr_ = (hostGpr_ & 0xFF00FF) | (static_cast<uint32_t>(v) << 8); return;
        case 0x02: hostGpr_ = (hostGpr_ & 0xFFFF00) | v; return;
        case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: {
            const int sh = 40 - 8 * (addr - 3);
            hostInstr_ = (hostInstr_ & ~(0xFFull << sh)) | (static_cast<uint64_t>(v) << sh);
            return;
        }
        case 0x0C: hostDol_ = static_cast<uint16_t>((hostDol_ & 0x00FF) | (v << 8)); return;
        case 0x0D: hostDol_ = static_cast<uint16_t>((hostDol_ & 0xFF00) | v); return;
        case 0x10: dadr_ = (dadr_ & 0xFF00F0) | (static_cast<uint32_t>(v) << 8); return;
        case 0x11: dadr_ = (dadr_ & 0xFFFF00) | (v & 0xF0); return;
        case 0x0F:
            dadr_ = (dadr_ & 0x00FFFF) | (static_cast<uint32_t>(v) << 16);
            hostRamAccess();  // writing the most significant byte last starts the cycle
            return;
        case kHostCtl: {
            // DECISION E25: bit 2 is read only. Bit 1 (RAM Clear) takes effect only while
            // halted and with Refresh Disable = 0; writing it 0 (or 1 with Refresh Disable = 1)
            // leaves the clear off, which is the power-up "reset the RAM Clear bit" step.
            refreshDisable_ = (v & kCtlRefreshOff) != 0;
            const bool want = (v & kCtlClear) != 0 && !refreshDisable_ && halted_;
            if (want && !clearActive_) agen_.dbase = agen_.dlength & ~agen_.memsiz & kMask24;
            clearActive_ = want;
            return;
        }
        case kRamCtl: ramCtl_ = v; return;
        case 0x18: serCtl_ = v; return;  // DECISION E29: direction/format bits are stored only
        case 0x1F:
            // Halt enable (write only): with HALT high the ESP stops immediately (E24).
            if (haltPin_) haltNow();
            return;
        case 0x80: case 0xA0: case 0xC0: case 0xE0: {
            const int idx = (addr - 0x80) >> 5;  // 0, 1, 2, 3
            selReg_[idx] = v;
            req_ = HostRequest{true, addr == 0x80, idx == 1 || idx == 3, idx == 2 || idx == 3, v};
            return;
        }
        default: return;
    }
}

}  // namespace ens
