#pragma once
// Motorola 68000 core (PRM, UM). One instruction or one exception per step(), with the cycle
// count of the bus cycles it ran (4 clocks each) plus its internal clocks. The two-word
// prefetch queue (IR, IRC) is modelled as in the UM. Instantiable: no globals, no statics.
#include <cstdint>
#include <functional>
#include <vector>

#include "m68k/m68k_decode.h"

namespace ens::m68k {

constexpr int kAutovector = -1;  // Bus::interruptAcknowledge: use vector 24 + level
constexpr int kSpurious = -2;    // ... use vector 24

// The machine side of the CPU: byte and word accesses, interrupt acknowledge and RESET.
// Addresses passed by the core are 24-bit (A23..A1 plus the byte select); word accesses are
// always at even addresses. Long accesses are two word accesses, high word first, except
// predecrement writes, which go low word first (UM).
// DECISION M5: no bus-error return and no function codes on this interface; vector 2 is never taken.
struct Bus {
    virtual ~Bus() = default;
    virtual uint8_t read8(uint32_t addr) = 0;
    virtual uint16_t read16(uint32_t addr) = 0;
    virtual void write8(uint32_t addr, uint8_t v) = 0;
    virtual void write16(uint32_t addr, uint16_t v) = 0;
    // Interrupt acknowledge for `level` (1..7): a vector number 0..255, kAutovector or kSpurious.
    virtual int interruptAcknowledge(int level) {
        (void)level;
        return kAutovector;
    }
    virtual void resetPeripherals() {}  // the RESET instruction asserts the RESET line
};

// Programmer-visible state.
// DECISION M3: pc is the address of the next instruction; prefetch = {word at pc, word at pc+2} (suite convention).
struct Registers {
    uint32_t d[8];
    uint32_t a[7];  // A7 is usp or ssp, according to sr.S
    uint32_t usp, ssp, pc;
    uint16_t sr;
    uint16_t prefetch[2];  // IR then IRC: the words at pc and pc + 2 at an instruction boundary
};

class Cpu {
public:
    explicit Cpu(Bus& bus);

    // Reset exception (UM 6.3.1): SR = 0x2700, SSP <- long at 0, PC <- long at 4, the prefetch
    // queue filled from PC. 40 cycles (UM table 8-14). Clears halted() and stopped(); no
    // other register changes. Not counted in exceptionCount().
    void reset();
    // Exactly one instruction or one exception; returns its cycles. A halted or stopped CPU
    // idles 4 cycles (DECISION M6).
    int step();
    // DECISION M6: whole instructions until >= cycles have passed; returns cycles used.
    int run(int cycles);
    void setIpl(int level);  // 0..7, clamped (interrupts: Task 7)
    bool halted() const { return halted_; }
    bool stopped() const { return stopped_; }
    uint64_t totalCycles() const { return total_; }

    Registers regs() const;            // DECISION M3: pc/prefetch as in the suite
    void setRegs(const Registers& r);  // also refills the internal IR/IRC from r.prefetch

    // Address of the last instruction started (after reset() or setRegs(): the PC).
    uint32_t instructionAddress() const { return instrAddr_; }
    uint32_t exceptionCount(int vector) const;  // 0..255; 0 for anything else

    std::function<void(uint32_t pc)> onInstruction;  // debug hook, called before each instruction

private:
    // ---- bus cycles: each access is one 4-clock bus cycle, counted in cycles_ ------------
    uint8_t read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    uint32_t read32(uint32_t addr);  // high word first
    void write8(uint32_t addr, uint8_t v);
    void write16(uint32_t addr, uint16_t v);
    void write32(uint32_t addr, uint32_t v);  // high word first (predecrement: low first)
    void idle(int clocks) { cycles_ += clocks; }

    // ---- prefetch queue ------------------------------------------------------------------
    // pc_ is the address of the word held in irc_; the instruction in ir_ starts at pc_ - 2.
    uint16_t nextWord();               // take IRC as an extension word and refill IRC
    void prefetch();                   // IR <- IRC, refill IRC: the closing fetch of an instruction
    void fillPrefetch(uint32_t addr);  // after a jump: IR <- (addr), IRC <- (addr + 2)

    // ---- status register -----------------------------------------------------------------
    bool supervisor() const { return (sr_ & 0x2000) != 0; }
    void setSr(uint16_t v);  // swaps A7 with the inactive stack pointer when S changes

    // ---- exceptions ----------------------------------------------------------------------
    // Group 1/2 entry shared by every exception: copy SR, S = 1, T = 0, push the 6-byte
    // frame (PC, SR) on the SSP, fetch the vector and refill the prefetch queue. 34 cycles.
    void exception(int vector, uint32_t stackedPc);

    void execute(Op op);  // dispatch of the decoded IR (m68k_cpu.cpp)

    Bus& bus_;
    std::vector<Op> table_;  // decodeOp of every opcode word, filled by the constructor

    uint32_t d_[8] = {};
    uint32_t a_[8] = {};      // a_[7] is the active stack pointer
    uint32_t otherSp_ = 0;    // the inactive one: USP when S = 1, SSP when S = 0
    uint32_t pc_ = 2;         // address of the word in irc_ (pc = pc_ - 2 under M3)
    uint16_t sr_ = 0x2700;
    uint16_t ir_ = 0, irc_ = 0;
    int ipl_ = 0;
    bool halted_ = false, stopped_ = false;

    int cycles_ = 0;  // clocks of the step in progress
    uint64_t total_ = 0;
    uint32_t instrAddr_ = 0;
    uint32_t exceptions_[256] = {};
};

}  // namespace ens::m68k
