// 68000 run loop, bus cycles, prefetch queue, status register and exception entry.
#include "m68k/m68k.h"

namespace ens::m68k {

namespace {

constexpr uint32_t kAddrMask = 0xFFFFFF;  // 24-bit address bus (A23..A0)
constexpr uint16_t kSrMask = 0xA71F;      // T, S, I2..I0, X N Z V C; the other bits read 0 (PRM 1.3.2)
constexpr uint16_t kSrS = 0x2000;
constexpr uint16_t kSrT = 0x8000;

constexpr int kVectorIllegal = 4;
constexpr int kVectorLineA = 10;
constexpr int kVectorLineF = 11;

}  // namespace

Cpu::Cpu(Bus& bus) : bus_(bus), table_(0x10000) {
    for (uint32_t w = 0; w < 0x10000; ++w) table_[w] = decodeOp(static_cast<uint16_t>(w));
}

// ---- bus cycles ---------------------------------------------------------------------------

uint8_t Cpu::read8(uint32_t addr) {
    cycles_ += 4;
    return bus_.read8(addr & kAddrMask);
}

uint16_t Cpu::read16(uint32_t addr) {
    cycles_ += 4;
    return bus_.read16(addr & kAddrMask);
}

uint32_t Cpu::read32(uint32_t addr) {
    const uint32_t hi = read16(addr);
    return (hi << 16) | read16(addr + 2);
}

void Cpu::write8(uint32_t addr, uint8_t v) {
    cycles_ += 4;
    bus_.write8(addr & kAddrMask, v);
}

void Cpu::write16(uint32_t addr, uint16_t v) {
    cycles_ += 4;
    bus_.write16(addr & kAddrMask, v);
}

void Cpu::write32(uint32_t addr, uint32_t v) {
    write16(addr, static_cast<uint16_t>(v >> 16));
    write16(addr + 2, static_cast<uint16_t>(v));
}

// ---- prefetch queue -----------------------------------------------------------------------

uint16_t Cpu::nextWord() {
    const uint16_t w = irc_;
    pc_ += 2;
    irc_ = read16(pc_);
    return w;
}

void Cpu::prefetch() {
    ir_ = irc_;
    pc_ += 2;
    irc_ = read16(pc_);
}

void Cpu::fillPrefetch(uint32_t addr) {
    pc_ = addr;
    ir_ = read16(pc_);
    pc_ += 2;
    irc_ = read16(pc_);
}

// ---- status register ----------------------------------------------------------------------

void Cpu::setSr(uint16_t v) {
    v &= kSrMask;
    if (((v ^ sr_) & kSrS) != 0) {
        const uint32_t sp = a_[7];
        a_[7] = otherSp_;
        otherSp_ = sp;
    }
    sr_ = v;
}

// ---- reset, step, run ---------------------------------------------------------------------

void Cpu::reset() {
    cycles_ = 0;
    halted_ = false;
    stopped_ = false;
    setSr(0x2700);  // S = 1 (the USP is kept in otherSp_), T = 0, mask 7
    // UM table 8-14: 40(6/0). The six reads are the vector longs and the two prefetch words;
    // the other 16 clocks are internal (placed first: nothing can observe where).
    idle(16);
    a_[7] = read32(0);
    fillPrefetch(read32(4));
    instrAddr_ = pc_ - 2;
    total_ += static_cast<uint64_t>(cycles_);
}

int Cpu::step() {
    cycles_ = 0;
    if (halted_ || stopped_) {
        idle(4);  // DECISION M6: a halted or stopped CPU idles one bus-cycle time per step()
    } else {
        instrAddr_ = pc_ - 2;
        if (onInstruction) onInstruction(instrAddr_);
        execute(table_[ir_]);
    }
    total_ += static_cast<uint64_t>(cycles_);
    return cycles_;
}

int Cpu::run(int cycles) {
    // DECISION M6: whole instructions until >= cycles (overshoot < one instruction); a halted
    // or stopped CPU consumes exactly the rest of the slice.
    int done = 0;
    while (done < cycles) {
        if (halted_ || stopped_) {
            total_ += static_cast<uint64_t>(cycles - done);
            done = cycles;
            break;
        }
        done += step();
    }
    return done;
}

void Cpu::setIpl(int level) {
    ipl_ = level < 0 ? 0 : (level > 7 ? 7 : level);
}

// ---- register access ----------------------------------------------------------------------

Registers Cpu::regs() const {
    Registers r{};
    for (int i = 0; i < 8; ++i) r.d[i] = d_[i];
    for (int i = 0; i < 7; ++i) r.a[i] = a_[i];
    r.ssp = supervisor() ? a_[7] : otherSp_;
    r.usp = supervisor() ? otherSp_ : a_[7];
    r.pc = pc_ - 2;  // DECISION M3: the formal PC is the address of the word in IR
    r.sr = sr_;
    r.prefetch[0] = ir_;
    r.prefetch[1] = irc_;
    return r;
}

void Cpu::setRegs(const Registers& r) {
    for (int i = 0; i < 8; ++i) d_[i] = r.d[i];
    for (int i = 0; i < 7; ++i) a_[i] = r.a[i];
    sr_ = static_cast<uint16_t>(r.sr & kSrMask);
    a_[7] = supervisor() ? r.ssp : r.usp;
    otherSp_ = supervisor() ? r.usp : r.ssp;
    pc_ = r.pc + 2;  // DECISION M3: IR holds the word at r.pc, IRC the word at r.pc + 2
    ir_ = r.prefetch[0];
    irc_ = r.prefetch[1];
    instrAddr_ = r.pc;
}

uint32_t Cpu::exceptionCount(int vector) const {
    return vector >= 0 && vector < 256 ? exceptions_[vector] : 0;
}

// ---- exceptions ---------------------------------------------------------------------------

void Cpu::exception(int vector, uint32_t stackedPc) {
    // UM 6.2.5: copy the SR, then S = 1 and T = 0 (setSr switches A7 to the SSP).
    const uint16_t oldSr = sr_;
    setSr(static_cast<uint16_t>((sr_ | kSrS) & ~kSrT));
    ++exceptions_[vector & 0xFF];
    // UM table 8-14: 34(4/3) for illegal, line A/F, privilege, trace and TRAP: three frame
    // writes, two vector reads and two prefetch reads (28 clocks) plus 6 internal clocks.
    idle(4);
    // Short (6-byte) frame: SR at SSP, PC high at SSP + 2, PC low at SSP + 4. Bus order and
    // idle placement follow the suite's transaction log (TRAP.json "4e44 [TRAP Q] 1"):
    // n4, PC low, SR, PC high, vector high, vector low, prefetch, n2, prefetch.
    a_[7] -= 6;
    const uint32_t sp = a_[7];
    write16(sp + 4, static_cast<uint16_t>(stackedPc));
    write16(sp, oldSr);
    write16(sp + 2, static_cast<uint16_t>(stackedPc >> 16));
    const uint32_t target = read32(static_cast<uint32_t>(vector & 0xFF) * 4);
    idle(2);
    fillPrefetch(target);
}

// ---- dispatch -----------------------------------------------------------------------------

void Cpu::execute(Op op) {
    switch (op) {
    case Op::Nop: prefetch(); return;  // 4(1/0)
    case Op::Illegal: exception(kVectorIllegal, instrAddr_); return;
    case Op::LineA: exception(kVectorLineA, instrAddr_); return;
    case Op::LineF: exception(kVectorLineF, instrAddr_); return;
    }
}

}  // namespace ens::m68k
