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
// Addresses passed by the core are 24-bit (A23..A1 plus the byte select). Word accesses are
// always at even addresses: an odd word or long access is an address error (vector 3) and
// never reaches the bus. Long accesses are two word accesses. Reads go high word first,
// except the -(An) operands of ADDX.l/SUBX.l; writes go high word first, except predecrement
// and read-modify-write stores, which go low word first (DECISION M13: the suite's bus
// order; the UM is silent).
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
    // Shared constants of the instruction implementations (m68k_cpu.cpp, m68k_ea.cpp, m68k_ops_*.cpp).
    static constexpr uint32_t kAddrMask = 0xFFFFFF;  // 24-bit address bus (A23..A0)
    static constexpr uint16_t kSrMask = 0xA71F;      // T, S, I2..I0, X N Z V C (PRM 1.3.2)
    static constexpr uint16_t kSrT = 0x8000, kSrS = 0x2000;
    static constexpr uint16_t kCcrX = 0x10, kCcrN = 0x08, kCcrZ = 0x04, kCcrV = 0x02, kCcrC = 0x01;
    static constexpr int kByte = 1, kWord = 2, kLong = 4;  // operand sizes in bytes

    // Thrown by a word or long access at an odd address, after fault*_ is recorded; caught by
    // step() and reset(), which run the address-error exception (or halt, DECISION M9).
    struct AddressFault {};

    // ---- bus cycles: each access is one 4-clock bus cycle, counted in cycles_ ------------
    // `program` marks a program-space access (the instruction stream and PC-relative
    // operands, PRM 2.0); only the address-error frame's function code shows it (M5).
    // A faulting access takes no clocks and never reaches the bus.
    uint8_t read8(uint32_t addr);
    uint16_t read16(uint32_t addr, bool program = false);
    uint32_t read32(uint32_t addr, bool program = false);  // high word first
    void write8(uint32_t addr, uint8_t v);
    void write16(uint32_t addr, uint16_t v);
    void write32(uint32_t addr, uint32_t v);      // high word first
    void write32Desc(uint32_t addr, uint32_t v);  // low word (addr + 2) first (M13)
    void idle(int clocks) { cycles_ += clocks; }
    [[noreturn]] void addressFault(uint32_t addr, bool read, bool program);

    // ---- prefetch queue ------------------------------------------------------------------
    // pc_ is the address of the word held in irc_; the instruction in ir_ starts at pc_ - 2.
    uint16_t nextWord();  // take IRC as an extension word and refill IRC
    void prefetch();      // IR <- IRC, refill IRC: the closing fetch of an instruction
    // After a jump: IR <- (addr), `between` internal clocks, IRC <- (addr + 2). An odd addr
    // is an address error on the first fetch (pc_ = addr - 2 then, so the frame's PC is
    // addr - 4 as in the suite, DECISION M11).
    void fillPrefetch(uint32_t addr, int between = 0);

    // ---- status register -----------------------------------------------------------------
    bool supervisor() const { return (sr_ & kSrS) != 0; }
    void setSr(uint16_t v);  // swaps A7 with the inactive stack pointer when S changes
    void setCcr(uint16_t ccr) { sr_ = static_cast<uint16_t>((sr_ & 0xFF00) | (ccr & 0x1F)); }
    // N and Z from the low `size` bytes of v, V = C = 0, X unchanged (MOVE, CLR, logic ops).
    void setLogicFlags(uint32_t v, int size);

    // ---- registers by size ---------------------------------------------------------------
    static uint32_t sizeMask(int size) {
        return size == kByte ? 0xFFu : size == kWord ? 0xFFFFu : 0xFFFFFFFFu;
    }
    static uint32_t signExtend(uint32_t v, int size);  // the low `size` bytes to 32 bits
    void setDreg(int r, int size, uint32_t v);         // only the low `size` bytes change

    // ---- effective addresses (m68k_ea.cpp) -----------------------------------------------
    // A located operand: eaIndex() of its field, its register and, for memory modes, its
    // address (for #imm the immediate value itself).
    struct Loc {
        int mode;
        int reg;
        uint32_t addr;
    };
    // Address calculation of an operand of `size` (UM table 8-1): extension words, (An)+ and
    // -(An) register updates (by 2 for a byte through A7), 2 internal clocks for -(An) and for
    // the index modes; #imm fetches the immediate.
    Loc locate(uint16_t field, int size);
    uint32_t load(const Loc& l, int size);              // read the operand (memory: high word first)
    // Write a read-modify-write result back to an operand just loaded (Dn: low `size`
    // bytes, An: whole register; memory long: low word first, M13). Not for plain stores,
    // whose longs go high word first.
    void storeRmw(const Loc& l, int size, uint32_t v);
    uint32_t readEa(uint16_t field, int size) { return load(locate(field, size), size); }
    // Address of a control operand (LEA, PEA, JMP, JSR, MOVEM): extension words only, and
    // `indexClocks` internal clocks for the index modes. No register is changed.
    uint32_t controlAddress(uint16_t field, int indexClocks);
    uint32_t indexWord(uint32_t base);  // (d8,base,Xn) from the brief extension word in IRC

    // ---- exceptions ----------------------------------------------------------------------
    // Group 1/2 entry shared by every exception: copy SR, S = 1, T = 0, push the 6-byte
    // frame (PC, SR) on the SSP, fetch the vector and refill the prefetch queue. 34 cycles.
    void exception(int vector, uint32_t stackedPc);
    void privilegeViolation() { exception(8, instrAddr_); }  // vector 8, the instruction's address
    // Group 0: the 14-byte address-error frame for the fault in fault*_. 50 cycles. A fault
    // inside it is a double fault and halts (DECISION M9).
    void addressError();

    void execute(Op op);  // dispatch of the decoded IR (m68k_cpu.cpp)

    // ---- data movement (m68k_ops_data.cpp) -----------------------------------------------
    void opMove(int size);
    void opMovea(int size);
    void opMoveq();
    void opLea();
    void opPea();
    void opClr(int size);
    void opExg();
    void opSwap();
    void opExt(int size);
    void opLink();
    void opUnlk();
    void opMoveUsp(bool toUsp);
    void opMovemToMem(int size);
    void opMovemToReg(int size);
    void opMovepToReg(int size);
    void opMovepToMem(int size);

    // ---- arithmetic (m68k_ops_arith.cpp) -------------------------------------------------
    // `isSub` selects SUB/SUBA/SUBI/SUBQ/SUBX over the ADD forms.
    void opAddSub(int size, bool isSub);       // <ea>,Dn
    void opAddSubToMem(int size, bool isSub);  // Dn,<ea>
    void opAddaSuba(int size, bool isSub);
    void opAddSubImm(int size, bool isSub);    // ADDI, SUBI
    void opAddSubQuick(int size, bool isSub);  // ADDQ, SUBQ
    void opAddxSubx(int size, bool isSub);
    void opCmp(int size);
    void opCmpa(int size);
    void opCmpi(int size);
    void opCmpm(int size);
    void opNeg(int size, bool negx);
    void opTst(int size);
    void opMul(bool isSigned);
    void opDiv(bool isSigned);
    void opAbcdSbcd(bool isSub);
    void opNbcd();
    void opChk();
    // Shared tails: the flags of a compare (X unchanged), and the -(Ay),-(Ax) operand pair
    // of ADDX/SUBX/ABCD/SBCD (one 2-clock predecrement, longs read low word first).
    void setCmpFlags(uint32_t a, uint32_t b, int size);
    uint32_t readPredec(int r, int size, uint32_t& addr);

    // ---- logic, shifts and bit operations (m68k_ops_logic.cpp) ---------------------------
    // `kind` of the logic handlers is 0 AND, 1 OR, 2 EOR.
    void opLogic(int size, int kind);          // AND, OR <ea>,Dn
    void opLogicToMem(int size, int kind);     // AND, OR Dn,<memory>; EOR Dn,<data alterable>
    void opLogicImm(int size, int kind);       // ANDI, ORI, EORI
    void opNot(int size);
    void opShiftReg(int size);                 // ASx, LSx, ROx, ROXx Dx,Dy / #n,Dy
    void opShiftMem();                         // the same, word-sized memory operand by 1
    void opBit(int kind, bool immediate);      // kind 0 BTST, 1 BCHG, 2 BCLR, 3 BSET
    void opScc();
    void opTas();
    bool condition(int cc) const;              // the 16 condition codes of Scc, DBcc and Bcc

    Bus& bus_;
    std::vector<Op> table_;  // decodeOp of every opcode word, filled by the constructor

    uint32_t d_[8] = {};
    uint32_t a_[8] = {};      // a_[7] is the active stack pointer
    uint32_t otherSp_ = 0;    // the inactive one: USP when S = 1, SSP when S = 0
    uint32_t pc_ = 2;         // address of the word in irc_ (pc = pc_ - 2 under M3)
    uint16_t sr_ = 0x2700;
    uint16_t ir_ = 0, irc_ = 0;
    uint16_t opcode_ = 0;  // ir_ when the instruction started (ir_ moves on at its closing fetch)
    int ipl_ = 0;
    bool halted_ = false, stopped_ = false;

    int cycles_ = 0;  // clocks of the step in progress
    uint64_t total_ = 0;
    uint32_t instrAddr_ = 0;
    uint32_t exceptions_[256] = {};

    // Address-error state: what the faulting access was, and what the processor was doing.
    uint32_t faultAddr_ = 0;    // full 32-bit access address
    uint32_t faultPc_ = 0;      // the PC the frame stacks (DECISION M11)
    uint16_t faultStatus_ = 0;  // frame status word bits 4..0: R/W, I/N, FC2..FC0
    bool notInstruction_ = false;  // in group-0/1 exception processing: I/N = 1 (UM 6.3.9.1)
};

}  // namespace ens::m68k
