// 68000 run loop, bus cycles, prefetch queue, status register and exception entry.
#include "m68k/m68k.h"

namespace ens::m68k {

namespace {

constexpr int kVectorAddressError = 3;
constexpr int kVectorIllegal = 4;
constexpr int kVectorLineA = 10;
constexpr int kVectorLineF = 11;

// Group 2 exceptions are caused by an instruction (CHK, divide by zero, TRAPV, TRAP #n);
// while one is processed the processor still counts as processing an instruction (UM 6.3.9.1).
bool isGroup2(int vector) {
    return (vector >= 5 && vector <= 7) || (vector >= 32 && vector <= 47);
}

}  // namespace

Cpu::Cpu(Bus& bus) : bus_(bus), table_(0x10000) {
    for (uint32_t w = 0; w < 0x10000; ++w) table_[w] = decodeOp(static_cast<uint16_t>(w));
}

// ---- bus cycles ---------------------------------------------------------------------------

void Cpu::addressFault(uint32_t addr, bool read, bool program) {
    faultAddr_ = addr;
    faultPc_ = pc_ - 2;  // DECISION M11: the suite stacks the address of the word before IRC
    const uint16_t fc = static_cast<uint16_t>((supervisor() ? 4 : 0) | (program ? 2 : 1));
    faultStatus_ = static_cast<uint16_t>((read ? 0x10 : 0) | (notInstruction_ ? 0x08 : 0) | fc);
    throw AddressFault{};
}

uint8_t Cpu::read8(uint32_t addr) {
    cycles_ += 4;
    return bus_.read8(addr & kAddrMask);
}

uint16_t Cpu::read16(uint32_t addr, bool program) {
    if ((addr & 1) != 0) addressFault(addr, true, program);
    cycles_ += 4;
    return bus_.read16(addr & kAddrMask);
}

uint32_t Cpu::read32(uint32_t addr, bool program) {
    const uint32_t hi = read16(addr, program);
    return (hi << 16) | read16(addr + 2, program);
}

void Cpu::write8(uint32_t addr, uint8_t v) {
    cycles_ += 4;
    bus_.write8(addr & kAddrMask, v);
}

void Cpu::write16(uint32_t addr, uint16_t v) {
    if ((addr & 1) != 0) addressFault(addr, false, false);
    cycles_ += 4;
    bus_.write16(addr & kAddrMask, v);
}

void Cpu::write32(uint32_t addr, uint32_t v) {
    write16(addr, static_cast<uint16_t>(v >> 16));
    write16(addr + 2, static_cast<uint16_t>(v));
}

void Cpu::write32Desc(uint32_t addr, uint32_t v) {
    // DECISION M13: predecrement and read-modify-write long stores go low word first (suite
    // MOVE.l "2100 [MOVE.l D0, -(A0)]", CLR.l "42a0 [CLR.l -(A0)]"). An odd addr faults on
    // the first word, so the frame's access address is addr + 2, as the suite stacks it.
    write16(addr + 2, static_cast<uint16_t>(v));
    write16(addr, static_cast<uint16_t>(v >> 16));
}

// ---- prefetch queue -----------------------------------------------------------------------

uint16_t Cpu::nextWord() {
    const uint16_t w = irc_;
    pc_ += 2;
    irc_ = read16(pc_, true);
    return w;
}

void Cpu::prefetch() {
    ir_ = irc_;
    pc_ += 2;
    irc_ = read16(pc_, true);
}

void Cpu::fillPrefetch(uint32_t addr, int between) {
    if ((addr & 1) != 0) {
        pc_ = addr - 2;  // DECISION M11: the frame then stacks addr - 4 (suite JMP, RTS)
        addressFault(addr, true, true);
    }
    pc_ = addr;
    ir_ = read16(pc_, true);
    idle(between);
    pc_ += 2;
    irc_ = read16(pc_, true);
}

// ---- status register and register helpers -------------------------------------------------

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
    notInstruction_ = true;
    setSr(0x2700);  // S = 1 (the USP is kept in otherSp_), T = 0, mask 7
    // UM table 8-14: 40(6/0). The six reads are the vector longs and the two prefetch words;
    // the other 16 clocks are internal (placed first: nothing can observe where).
    idle(16);
    try {
        a_[7] = read32(0, true);
        fillPrefetch(read32(4, true));
    } catch (const AddressFault&) {
        halted_ = true;  // DECISION M9: an address error during reset is a double fault
    }
    notInstruction_ = false;
    instrAddr_ = pc_ - 2;
    opcode_ = ir_;
    total_ += static_cast<uint64_t>(cycles_);
}

int Cpu::step() {
    cycles_ = 0;
    if (halted_ || stopped_) {
        idle(4);  // DECISION M6: a halted or stopped CPU idles one bus-cycle time per step()
    } else {
        instrAddr_ = pc_ - 2;
        opcode_ = ir_;
        notInstruction_ = false;
        if (onInstruction) onInstruction(instrAddr_);
        try {
            execute(table_[ir_]);
        } catch (const AddressFault&) {
            try {
                addressError();
            } catch (const AddressFault&) {
                halted_ = true;  // DECISION M9: an address error inside group-0 processing halts
            }
        }
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
    opcode_ = ir_;
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
    if (!isGroup2(vector)) notInstruction_ = true;  // UM 6.3.9.1: the frame's I/N bit
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
    fillPrefetch(target, 2);
    notInstruction_ = false;
}

void Cpu::addressError() {
    // UM 6.3.10 / figure 6-7: the 14-byte group-0 frame. Bits 4..0 of the status word are
    // R/W, I/N and FC2..FC0 of the faulting access.
    // DECISION M11: the UM leaves bits 15..5 undefined; the suite stores IR bits 15..5 there.
    const uint16_t status = static_cast<uint16_t>((opcode_ & 0xFFE0) | faultStatus_);
    const uint16_t oldSr = sr_;
    notInstruction_ = true;
    setSr(static_cast<uint16_t>((sr_ | kSrS) & ~kSrT));
    ++exceptions_[kVectorAddressError];
    // UM table 8-14: 50(4/7). The faulting access itself is not run. Bus order as in the
    // suite (MOVE.w "3010 [MOVE.w (A0), D0] 4754"): n4, PC low, SR, PC high, IR, address
    // low, status, address high, vector high, vector low, prefetch, n2, prefetch.
    idle(4);
    a_[7] -= 14;
    const uint32_t sp = a_[7];
    write16(sp + 12, static_cast<uint16_t>(faultPc_));
    write16(sp + 8, oldSr);
    write16(sp + 10, static_cast<uint16_t>(faultPc_ >> 16));
    write16(sp + 6, opcode_);
    write16(sp + 4, static_cast<uint16_t>(faultAddr_));
    write16(sp, status);
    write16(sp + 2, static_cast<uint16_t>(faultAddr_ >> 16));
    fillPrefetch(read32(kVectorAddressError * 4), 2);
    notInstruction_ = false;
}

// ---- dispatch -----------------------------------------------------------------------------

void Cpu::execute(Op op) {
    switch (op) {
    case Op::Nop: prefetch(); return;  // 4(1/0)
    case Op::Illegal: exception(kVectorIllegal, instrAddr_); return;
    case Op::LineA: exception(kVectorLineA, instrAddr_); return;
    case Op::LineF: exception(kVectorLineF, instrAddr_); return;
    case Op::MoveB: opMove(kByte); return;
    case Op::MoveW: opMove(kWord); return;
    case Op::MoveL: opMove(kLong); return;
    case Op::MoveaW: opMovea(kWord); return;
    case Op::MoveaL: opMovea(kLong); return;
    case Op::Moveq: opMoveq(); return;
    case Op::Lea: opLea(); return;
    case Op::Pea: opPea(); return;
    case Op::ClrB: opClr(kByte); return;
    case Op::ClrW: opClr(kWord); return;
    case Op::ClrL: opClr(kLong); return;
    case Op::Exg: opExg(); return;
    case Op::Swap: opSwap(); return;
    case Op::ExtW: opExt(kWord); return;
    case Op::ExtL: opExt(kLong); return;
    case Op::Link: opLink(); return;
    case Op::Unlk: opUnlk(); return;
    case Op::MoveToUsp: opMoveUsp(true); return;
    case Op::MoveFromUsp: opMoveUsp(false); return;
    case Op::MovemToMemW: opMovemToMem(kWord); return;
    case Op::MovemToMemL: opMovemToMem(kLong); return;
    case Op::MovemToRegW: opMovemToReg(kWord); return;
    case Op::MovemToRegL: opMovemToReg(kLong); return;
    case Op::MovepToRegW: opMovepToReg(kWord); return;
    case Op::MovepToRegL: opMovepToReg(kLong); return;
    case Op::MovepToMemW: opMovepToMem(kWord); return;
    case Op::MovepToMemL: opMovepToMem(kLong); return;
    }
}

}  // namespace ens::m68k
