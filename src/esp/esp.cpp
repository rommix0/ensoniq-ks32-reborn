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

bool Esp::halted() const { return false; }  // Task 9: HALT pin

// ---- execution -------------------------------------------------------------------------

int Esp::runSample() {
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
    // (8) AGEN: address for word k from GPR[pc] and RAM-ctrl(k).
    pRam_ = {true, ram, agen_.physical(ram, gpr_[pc_])};

    // (9) DECISION E19: END (or E11: word 159) ends the sample after its MUL and RAM fields
    // have been issued; their results and the RAM cycle carry into the next sample.
    if (op == Op::END || pc_ == kProgramSize - 1) {
        agen_.endOfSample();
        // Task 9 hook (E19): perform at most one pending host GPR/INSTR transfer here, then
        // sample HALT (an END taken with HALT high resets the DOL FIFO, E18).
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

}  // namespace ens
