#include "esp/esp_isa.h"

#include <string>

namespace ens::esp {

namespace {

constexpr int kDil = 244;

constexpr OpSelect kSel[16] = {
    {false, true, false, false, true, false},   // 0
    {false, true, false, false, false, true},   // 1
    {false, true, false, false, true, true},    // 2
    {false, true, false, true, true, false},    // 3
    {false, true, false, true, true, true},     // 4
    {false, false, true, false, true, false},   // 5
    {false, false, true, true, true, false},    // 6
    {false, true, true, false, true, false},    // 7
    {false, true, true, true, true, false},     // 8
    {true, true, false, false, true, false},    // 9
    {true, true, false, false, false, true},    // A
    {true, true, false, false, true, true},     // B
    {true, true, false, true, true, false},     // C
    {true, true, false, true, true, true},      // D
    {true, true, true, false, true, false},     // E
    {true, true, true, true, true, false},      // F
};

const char* const kOpNames[16] = {"ADD", "SUB", "ADDU", "SUBU", "CMP", "AND", "OR", "XOR",
                                  "ABS", "MOV", "ASL2", "ASL8", "LS15", "DIFF", "ASR", "END"};
const char* const kRamNames[8] = {"read-delay", "write-delay", "read-A",  "write-A",
                                  "read-B",     "read-dump",   "read-io", "write-io"};

bool oneOperand(Op op) {
    return op == Op::ABS || op == Op::MOV || op == Op::ASL2 || op == Op::ASL8 ||
           op == Op::LS15 || op == Op::DIFF || op == Op::ASR;
}

std::string pad(std::string s, size_t width) {
    if (s.size() < width) s.append(width - s.size(), ' ');
    return s;
}

// Register read as a source (address 244 is the read-only DIL) or written as a destination
// (244 is the write-only MEMSIZ).
std::string srcName(uint8_t addr) { return addr == kDil ? "DIL" : regName(addr); }
std::string dstName(uint8_t addr) { return addr == kDil ? "MEMSIZ" : regName(addr); }

std::string dest(bool toReg, bool toFifo, uint8_t addr) {
    std::string s;
    if (toReg) s = dstName(addr);
    if (toFifo) s += toReg ? ",DOL" : "DOL";
    return s;
}

}  // namespace

const OpSelect& opSelect(unsigned row) { return kSel[row & 0xF]; }

Instr Instr::decode(uint64_t w) {
    Instr i;
    i.d = static_cast<uint8_t>(w >> 40);
    i.c = static_cast<uint8_t>(w >> 32);
    i.b = static_cast<uint8_t>(w >> 24);
    i.a = static_cast<uint8_t>(w >> 16);
    i.op = static_cast<Op>((w >> 12) & 0xF);
    i.opsel = static_cast<uint8_t>((w >> 8) & 0xF);
    i.skip = ((w >> 7) & 1) != 0;
    i.mac = ((w >> 6) & 1) != 0;
    i.ram = static_cast<RamOp>((w >> 3) & 7);
    return i;
}

uint64_t Instr::encode() const {
    return (uint64_t(d) << 40) | (uint64_t(c) << 32) | (uint64_t(b) << 24) | (uint64_t(a) << 16) |
           (uint64_t(static_cast<unsigned>(op) & 0xF) << 12) | (uint64_t(opsel & 0xF) << 8) |
           (uint64_t(skip ? 1 : 0) << 7) | (uint64_t(mac ? 1 : 0) << 6) |
           (uint64_t(static_cast<unsigned>(ram) & 7) << 3);
}

std::string regName(uint8_t addr) {
    static const char* const kSpr[] = {"SER0R", "SER0L", "SER1R", "SER1L", "SER2R", "SER2L",
                                       "SER3R", "SER3L", "MACL",  "MACH",  "DIL/MEMSIZ",
                                       "DLENGTH", "ABASE", "BBASE", "DBASE", "SIGREG", "CCR",
                                       "CMR",   "-1",    "MIN",   "MAX",   "ZERO"};
    if (addr < 192) return "gpr" + std::to_string(addr);
    if (addr >= 234) return kSpr[addr - 234];
    return "?" + std::to_string(addr);
}

std::string disassemble(const Instr& in, int pc) {
    const OpSelect& s = opSelect(in.opsel);
    std::string pcs = std::to_string(pc);
    if (pcs.size() < 3) pcs.insert(0, 3 - pcs.size(), '0');
    std::string line = pcs + "  " + pad(kOpNames[static_cast<unsigned>(in.op) & 0xF], 5);

    if (in.op != Op::END) {
        line += " " + srcName(in.b);
        if (!oneOperand(in.op))
            line += ", " + (s.aluSrcDil ? std::string("DIL") : srcName(in.a));
        if (in.op != Op::CMP) line += " > " + dest(s.aluToA, s.aluToFifo, in.a);
    }

    line += " | MUL " + srcName(in.d) + " x " +
            (s.mulSrcDil ? std::string("DIL") : srcName(in.c)) + " > " +
            dest(s.mulToC, s.mulToFifo, in.c);
    if (in.mac) line += " +MAC";

    line += std::string(" | RAM ") + kRamNames[static_cast<unsigned>(in.ram) & 7];
    if (in.skip) line += " SKIP";
    return line;
}

}  // namespace ens::esp
