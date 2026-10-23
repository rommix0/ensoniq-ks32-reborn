// 68000 program control and system instructions: Bcc, BRA, BSR, DBcc, JMP, JSR, RTS, RTR,
// RTE, TRAP, TRAPV, RESET, STOP, MOVE to/from SR, MOVE to CCR and ANDI, ORI, EORI to CCR and
// SR. Cycle counts come from the bus cycles each one runs plus the internal clocks of UM
// tables 8-9, 8-10 and 8-12; the place of each internal-clock run and the bus order follow the
// suite's transaction logs. A jump refills the queue at its target with fillPrefetch, so an
// odd target is an address error on the first fetch there (DECISION M11).
#include "m68k/m68k.h"

namespace ens::m68k {

namespace {

constexpr int kVectorTrapv = 7;
constexpr int kVectorTrap0 = 32;  // TRAP #n: vector 32 + n
constexpr int kAnd = 0, kOr = 1;  // opSrImm kinds (2 is EOR)

uint16_t logicOf(int kind, uint16_t a, uint16_t b) {
    return static_cast<uint16_t>(kind == kAnd ? (a & b) : kind == kOr ? (a | b) : (a ^ b));
}

}  // namespace

// ---- branches -----------------------------------------------------------------------------

void Cpu::opBcc() {
    // UM table 8-9: taken 10(2/0); not taken 8(1/0) (byte) or 12(2/0) (word). The
    // displacement is relative to the address of the word after the opcode (pc_); the word
    // displacement is taken from IRC without a refill when the branch is taken.
    const uint32_t d8 = opcode_ & 0xFFu;
    if (condition((opcode_ >> 8) & 15)) {
        idle(2);
        fillPrefetch(pc_ + (d8 == 0 ? signExtend(irc_, kWord) : signExtend(d8, kByte)));
        return;
    }
    idle(4);
    if (d8 == 0) (void)nextWord();  // skip the displacement word
    prefetch();
}

void Cpu::opBsr() {
    // UM table 8-9: 18(2/2). n2, push the return address (high word first), fill the queue
    // at the target (suite BSR "617c [BSR Q] 2"); an odd target faults after the push.
    const uint32_t d8 = opcode_ & 0xFFu;
    const uint32_t target = pc_ + (d8 == 0 ? signExtend(irc_, kWord) : signExtend(d8, kByte));
    const uint32_t ret = d8 == 0 ? pc_ + 2 : pc_;
    idle(2);
    a_[7] -= 4;
    write32(a_[7], ret);
    fillPrefetch(target);
}

void Cpu::opDbcc() {
    // UM table 8-9: cc true 12(2/0) (as Bcc.w not taken); cc false 10(2/0) while Dn.w - 1
    // != -1, 14(3/0) when the count expires. Dn.w is decremented before the branch, so an
    // odd target faults with the new count (suite DBcc "53c8 [DBcc D0, #] 28").
    if (condition((opcode_ >> 8) & 15)) {
        idle(4);
        (void)nextWord();
        prefetch();
        return;
    }
    const int r = opcode_ & 7;
    const uint32_t count = (d_[r] - 1u) & 0xFFFFu;
    setDreg(r, kWord, count);
    idle(2);
    const uint32_t next = pc_ + 2;  // the instruction after the displacement word
    const uint32_t target = pc_ + signExtend(irc_, kWord);
    if (count != 0xFFFFu) {
        fillPrefetch(target);
        return;
    }
    // DECISION M21: on expiry the first fetch at the branch target still runs (the third
    // read of 14(3/0); an odd target faults as in the looping case), its word is dropped,
    // and the queue is refilled after the displacement word.
    fetchJumpIr(target);
    fillPrefetch(next);
}

// ---- jumps and returns --------------------------------------------------------------------

uint32_t Cpu::jumpAddress(uint16_t field) {
    // UM table 8-10 (JMP): (An) 8, (d16,An) 10, (d8,An,Xn) 14, (xxx).W 10, (xxx).L 12,
    // (d16,PC) 10, (d8,PC,Xn) 14. Besides the two fetches at the target: 2 internal clocks for
    // the 16-bit displacement and (xxx).W, 6 for the index modes, one read for (xxx).L's low
    // word (suite JMP "4ee8 [JMP (d16, A0)] 10", "4efb [JMP (d8, PC, Xn)] 4",
    // "4ef9 [JMP (xxx).l] 43").
    const int r = field & 7;
    switch (eaIndex(field)) {
    case 2: return a_[r];
    case 5: idle(2); return a_[r] + signExtend(irc_, kWord);
    case 6: idle(6); return indexValue(a_[r], irc_);
    case 7: idle(2); return signExtend(irc_, kWord);
    case 8: return (static_cast<uint32_t>(irc_) << 16) | read16(pc_ + 2, true);
    case 9: idle(2); return pc_ + signExtend(irc_, kWord);
    default: idle(6); return indexValue(pc_, irc_);  // 10: (d8,PC,Xn), decodeOp admits no other
    }
}

void Cpu::opJmp() { fillPrefetch(jumpAddress(opcode_ & 0x3F)); }

void Cpu::opJsr() {
    // UM table 8-10 (JSR): the JMP time plus the push, 8 more. The first fetch at the target
    // runs before the push (an odd target faults with nothing pushed), the second after it
    // (suite JSR "4e90 [JSR (A0)] 18", "4ea8 [JSR (d16, A0)] 78").
    const uint16_t field = opcode_ & 0x3F;
    const int mode = eaIndex(field);
    const uint32_t ret = pc_ + (mode == 2 ? 0u : mode == 8 ? 4u : 2u);  // after the extension words
    fetchJumpIr(jumpAddress(field));
    a_[7] -= 4;
    write32(a_[7], ret);
    pc_ += 2;
    irc_ = read16(pc_, true);
}

void Cpu::opRts() {
    // UM table 8-12: 16(4/0). Pop the PC (high word first), then fill the queue there.
    const uint32_t target = read32(a_[7]);
    a_[7] += 4;
    fillPrefetch(target);
}

void Cpu::opReturnFrame(bool rte) {
    // UM table 8-12: RTE (privileged) 20(5/0), RTR 20(2/0).
    // DECISION M23: RTR runs 5 reads as RTE does (the table's 2 cannot pop its frame). Both
    // are three stack reads, PC high (SP + 2), SR (SP), PC low (SP + 4), and the two fetches
    // at the return address (suite RTE "4e73 [RTE] 1", RTR "4e77 [RTR] 1").
    // RTE loads the whole SR (switching to the USP when S becomes 0, before the fetches, so
    // an odd return address stacks the new SR), RTR only the CCR.
    if (rte && !supervisor()) {
        privilegeViolation();
        return;
    }
    const uint32_t sp = a_[7];
    const uint32_t hi = read16(sp + 2);
    const uint16_t sr = read16(sp);
    const uint32_t lo = read16(sp + 4);
    a_[7] += 6;
    if (rte) setSr(sr);
    else setCcr(sr);
    fillPrefetch((hi << 16) | lo);
}

// ---- traps, RESET, STOP -------------------------------------------------------------------

void Cpu::opTrap() {
    // UM table 8-14: 34(4/3). The stacked PC is the next instruction's address (UM 6.3.5).
    exception(kVectorTrap0 + (opcode_ & 15), pc_);
}

void Cpu::opTrapv() {
    // UM table 8-12: 4(1/0) without a trap; table 8-14: 34(5/3) with one, the closing fetch
    // followed by the exception without its 4 leading internal clocks (suite TRAPV
    // "4e76 [TRAPV] 3"). The stacked PC is the next instruction's address.
    prefetch();
    if ((sr_ & kCcrV) != 0) exception(kVectorTrapv, pc_ - 2, 0);
}

void Cpu::opReset() {
    // DECISION M8: UM table 8-12: 132(1/0), and the PRM: RESET asserts the RESET line for
    // 124 clocks and leaves the processor state other than the PC unchanged. Bus order as in
    // the suite (RESET "4e70 [RESET] 1": n4, n124, the closing fetch); the peripherals are
    // reset as the 124 clocks begin.
    if (!supervisor()) {
        privilegeViolation();
        return;
    }
    idle(4);
    bus_.resetPeripherals();
    idle(124);
    prefetch();
}

void Cpu::opStop() {
    // PRM STOP: the immediate word to SR, the PC to the next instruction, then no more
    // fetching until a trace, interrupt (DECISION M7) or reset exception. UM table 8-12: 4(0/0),
    // so the queue is not refilled.
    // DECISION M22: the IR/IRC words stay as they were (the STOP opcode and its immediate)
    // and regs().pc is the next instruction's address; the exception that ends the STOP
    // stacks that address and refills the queue from its vector.
    if (!supervisor()) {
        privilegeViolation();
        return;
    }
    setSr(irc_);
    idle(4);
    pc_ += 4;
    stopped_ = true;
}

// ---- status register access ---------------------------------------------------------------

void Cpu::opMoveFromSr() {
    // UM table 8-12: Dn 6(1/0); memory 8(1/1)+, which reads the destination first: read,
    // closing fetch, write (suite MOVEfromSR "40d0 [MOVEfromSR (A0)] 162"; M13). Not
    // privileged on the 68000.
    const Loc l = locate(opcode_ & 0x3F, kWord);
    if (l.mode == 0) {
        setDreg(l.reg, kWord, sr_);
        prefetch();
        idle(2);
        return;
    }
    (void)load(l, kWord);
    prefetch();
    storeRmw(l, kWord, sr_);
}

void Cpu::opMoveToSr(bool ccrOnly) {
    // UM table 8-12: MOVE to SR 12(2/0)+ (privileged), MOVE to CCR 12(1/0)+ (word source,
    // low byte used). After the operand: 4 internal clocks, then the queue is refetched from
    // the next instruction, 2 reads (suite MOVEtoSR "46d0 [MOVEtoSR (A0)] 20", MOVEtoCCR
    // "44d0 [MOVEtoCCR (A0)] 30"). DECISION M23: MOVE to CCR has those 2 reads too, not the
    // table's 1. A PC-relative source is a program read (DECISION M12).
    if (!ccrOnly && !supervisor()) {
        privilegeViolation();
        return;
    }
    const uint16_t v = static_cast<uint16_t>(readEa(opcode_ & 0x3F, kWord));
    if (ccrOnly) setCcr(v);
    else setSr(v);
    idle(4);
    fillPrefetch(pc_);
}

void Cpu::opSrImm(int kind, bool toSr) {
    // UM table 8-12: 20(3/0) for ANDI, ORI, EORI to CCR and (privileged) to SR: the
    // immediate, 8 internal clocks, then the queue refetched from the next instruction
    // (suite ANDItoSR "027c [ANDItoSR #] 1"). To CCR only the low 5 bits of the byte act.
    if (toSr && !supervisor()) {
        privilegeViolation();
        return;
    }
    const uint16_t imm = nextWord();
    idle(8);
    if (toSr) setSr(logicOf(kind, sr_, imm));
    else setCcr(logicOf(kind, static_cast<uint16_t>(sr_ & 0x1F), imm));
    fillPrefetch(pc_);
}

}  // namespace ens::m68k
