// 68000 debug-trace disassembler. Validity comes from decodeOp (an Illegal, LineA or LineF word
// is data); this file only names the fields of words decodeOp has already accepted, so a mode
// an instruction does not allow never reaches the operand formatter.
//
// Number style: below 10 decimal, otherwise $hex; a negative displacement or immediate prints
// as -$hex. Addresses (absolute operands, PC-relative and branch targets) are 24-bit, the
// 68000's bus; targets always print in hex.
#include "m68k/m68k_dis.h"

#include "m68k/m68k_decode.h"

namespace ens::m68k {

namespace {

constexpr uint32_t kAddrMask = 0xFFFFFF;
constexpr const char* kHexDigits = "0123456789abcdef";
constexpr const char* kCond[16] = {"t",  "f",  "hi", "ls", "cc", "cs", "ne", "eq",
                                   "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"};
constexpr const char* kShiftKind[4] = {"as", "ls", "rox", "ro"};
constexpr const char* kBitOp[4] = {"btst", "bchg", "bclr", "bset"};

enum Size { kByte = 0, kWord = 1, kLong = 2 };

std::string hex(uint32_t v) {
    std::string s;
    do {
        s.insert(s.begin(), kHexDigits[v & 15]);
        v >>= 4;
    } while (v != 0);
    return "$" + s;
}

std::string num(uint32_t v) { return v < 10 ? std::to_string(v) : hex(v); }

// Signed: a negative value prints as -$hex whatever its size.
std::string snum(int32_t v) {
    if (v >= 0) return num(static_cast<uint32_t>(v));
    return "-" + hex(0u - static_cast<uint32_t>(v));
}

int32_t sext8(uint32_t v) { return static_cast<int32_t>(((v & 0xFFu) ^ 0x80u) - 0x80u); }
int32_t sext16(uint32_t v) { return static_cast<int32_t>(((v & 0xFFFFu) ^ 0x8000u) - 0x8000u); }

std::string dreg(int r) { return "d" + std::to_string(r); }
std::string areg(int r) { return r == 7 ? "sp" : "a" + std::to_string(r); }
std::string ind(int r) { return "(" + areg(r) + ")"; }

const char* sfx(int size) { return size == kByte ? ".b" : size == kWord ? ".w" : ".l"; }

// Reads the instruction's words in order and counts them.
struct Fetch {
    const std::function<uint16_t(uint32_t)>& read16;
    uint32_t base;
    uint32_t pos = 0;
    uint32_t here() const { return (base + pos) & kAddrMask; }
    uint16_t word() {
        const uint16_t w = read16(here());
        pos += 2;
        return w;
    }
};

// Index register of a brief extension word: bit 15 D/A, 14..12 register, 11 word/long.
std::string indexReg(uint16_t ext) {
    const int r = (ext >> 12) & 7;
    return ((ext & 0x8000) != 0 ? areg(r) : dreg(r)) + ((ext & 0x0800) != 0 ? ".l" : ".w");
}

// The effective-address operand in `field` (mode 5..3, register 2..0), consuming its
// extension words. `size` only matters for #imm.
std::string ea(Fetch& f, uint16_t field, int size) {
    const int r = field & 7;
    switch (eaIndex(field)) {
    case 0: return dreg(r);
    case 1: return areg(r);
    case 2: return ind(r);
    case 3: return ind(r) + "+";
    case 4: return "-" + ind(r);
    case 5: {
        const int32_t d = sext16(f.word());
        return "(" + snum(d) + "," + areg(r) + ")";
    }
    case 6: {
        const uint16_t ext = f.word();
        return "(" + snum(sext8(ext)) + "," + areg(r) + "," + indexReg(ext) + ")";
    }
    case 7: return "(" + num(static_cast<uint32_t>(sext16(f.word())) & kAddrMask) + ").w";
    case 8: {
        const uint32_t hi = f.word();
        const uint32_t lo = f.word();
        return "(" + num(((hi << 16) | lo) & kAddrMask) + ").l";
    }
    case 9: {
        const uint32_t at = f.here();  // PC-relative to the extension word's address
        const uint32_t target = (at + static_cast<uint32_t>(sext16(f.word()))) & kAddrMask;
        return "(" + hex(target) + ",pc)";
    }
    case 10: {
        const uint32_t at = f.here();
        const uint16_t ext = f.word();
        const uint32_t target = (at + static_cast<uint32_t>(sext8(ext))) & kAddrMask;
        return "(" + hex(target) + ",pc," + indexReg(ext) + ")";
    }
    case 11: {
        if (size == kLong) {
            const uint32_t hi = f.word();
            const uint32_t lo = f.word();
            return "#" + num((hi << 16) | lo);
        }
        const uint32_t v = f.word();
        return "#" + num(size == kByte ? (v & 0xFF) : v);
    }
    default: return "?";
    }
}

// An immediate operand word(s) of an ORI/ANDI/.../CMPI-style instruction.
std::string immediate(Fetch& f, int size) { return ea(f, 0x3C, size); }

// MOVEM register list. `mask` is as stored in the extension word; the -(An) form stores it
// reversed (bit 0 = a7), so it is flipped first. Runs of consecutive registers print as d0-d3.
std::string regList(uint16_t mask, bool reversed) {
    uint32_t m = mask;
    if (reversed) {
        m = 0;
        for (int i = 0; i < 16; ++i) {
            if ((mask >> i) & 1) m |= 1u << (15 - i);
        }
    }
    std::string out;
    for (int cls = 0; cls < 2; ++cls) {
        int i = 0;
        while (i < 8) {
            if (((m >> (cls * 8 + i)) & 1) == 0) {
                ++i;
                continue;
            }
            int j = i;
            while (j + 1 < 8 && ((m >> (cls * 8 + j + 1)) & 1) != 0) ++j;
            if (!out.empty()) out += "/";
            out += cls == 0 ? dreg(i) : areg(i);
            if (j > i) out += "-" + (cls == 0 ? dreg(j) : areg(j));
            i = j + 1;
        }
    }
    return out.empty() ? "#0" : out;
}

std::string line(const std::string& mnemonic, const std::string& args) {
    if (args.empty()) return mnemonic;
    std::string s = mnemonic;
    s.append(s.size() < 8 ? 8 - s.size() : 1, ' ');
    return s + args;
}

std::string quick(int field) { return std::to_string(field == 0 ? 8 : field); }

}  // namespace

DisLine disassemble(const std::function<uint16_t(uint32_t)>& read16, uint32_t addr) {
    Fetch f{read16, addr & kAddrMask};
    const uint16_t op = f.word();
    const Op k = decodeOp(op);
    const uint16_t field = op & 0x3F;
    const int rn = (op >> 9) & 7;  // register in bits 11..9
    const int ry = op & 7;         // register in bits 2..0
    std::string text;

    // Shorthands over the lambdas below keep each case to one line.
    const auto dstField = [&op] {  // MOVE's destination: register 11..9, mode 8..6
        return static_cast<uint16_t>(((op >> 9) & 7) | ((op >> 3) & 0x38));
    };
    // <ea> alone, <ea>,Dn / An, Dn,<ea>, #imm,<ea>
    const auto unary = [&](const char* name, int sz) {
        return line(std::string(name) + sfx(sz), ea(f, field, sz));
    };
    const auto eaToReg = [&](const char* name, int sz, const std::string& reg) {
        return line(std::string(name) + sfx(sz), ea(f, field, sz) + "," + reg);
    };
    const auto regToEa = [&](const char* name, int sz) {
        return line(std::string(name) + sfx(sz), dreg(rn) + "," + ea(f, field, sz));
    };
    const auto immToEa = [&](const char* name, int sz) {
        const std::string imm = immediate(f, sz);
        return line(std::string(name) + sfx(sz), imm + "," + ea(f, field, sz));
    };
    const auto rel = [&](Op first) { return static_cast<int>(k) - static_cast<int>(first); };

    switch (k) {
    case Op::Illegal:
    case Op::LineA:
    case Op::LineF: {
        if (op == 0x4AFC) {
            text = "illegal";
        } else {
            std::string h = hex(op);
            h.insert(1, 4 - (h.size() - 1), '0');
            text = line("dc.w", h);
        }
        break;
    }
    case Op::Nop: text = "nop"; break;
    case Op::MoveB:
    case Op::MoveW:
    case Op::MoveL: {
        const int sz = rel(Op::MoveB);
        const std::string src = ea(f, field, sz);
        text = line(std::string("move") + sfx(sz), src + "," + ea(f, dstField(), sz));
        break;
    }
    case Op::MoveaW: text = eaToReg("movea", kWord, areg(rn)); break;
    case Op::MoveaL: text = eaToReg("movea", kLong, areg(rn)); break;
    case Op::Moveq: text = line("moveq", "#" + snum(sext8(op)) + "," + dreg(rn)); break;
    case Op::Lea: text = line("lea", ea(f, field, kLong) + "," + areg(rn)); break;
    case Op::Pea: text = line("pea", ea(f, field, kLong)); break;
    case Op::ClrB:
    case Op::ClrW:
    case Op::ClrL: text = unary("clr", rel(Op::ClrB)); break;
    case Op::Exg: {
        const int mode = (op >> 3) & 0x1F;
        text = line("exg", mode == 0x08   ? dreg(rn) + "," + dreg(ry)
                           : mode == 0x09 ? areg(rn) + "," + areg(ry)
                                          : dreg(rn) + "," + areg(ry));
        break;
    }
    case Op::Swap: text = line("swap", dreg(ry)); break;
    case Op::ExtW: text = line("ext.w", dreg(ry)); break;
    case Op::ExtL: text = line("ext.l", dreg(ry)); break;
    case Op::Link: text = line("link", areg(ry) + ",#" + snum(sext16(f.word()))); break;
    case Op::Unlk: text = line("unlk", areg(ry)); break;
    case Op::MoveToUsp: text = line("move.l", areg(ry) + ",usp"); break;
    case Op::MoveFromUsp: text = line("move.l", "usp," + areg(ry)); break;
    case Op::MovemToMemW:
    case Op::MovemToMemL:
    case Op::MovemToRegW:
    case Op::MovemToRegL: {
        const bool toMem = k == Op::MovemToMemW || k == Op::MovemToMemL;
        const int sz = (k == Op::MovemToMemL || k == Op::MovemToRegL) ? kLong : kWord;
        const uint16_t mask = f.word();  // the register mask precedes the operand's words
        const std::string regs = regList(mask, toMem && eaIndex(field) == 4);
        const std::string mem = ea(f, field, sz);
        text = line(std::string("movem") + sfx(sz), toMem ? regs + "," + mem : mem + "," + regs);
        break;
    }
    case Op::MovepToRegW:
    case Op::MovepToRegL:
    case Op::MovepToMemW:
    case Op::MovepToMemL: {
        const bool toMem = k == Op::MovepToMemW || k == Op::MovepToMemL;
        const int sz = (k == Op::MovepToRegL || k == Op::MovepToMemL) ? kLong : kWord;
        const std::string mem = "(" + snum(sext16(f.word())) + "," + areg(ry) + ")";
        text = line(std::string("movep") + sfx(sz), toMem ? dreg(rn) + "," + mem : mem + "," + dreg(rn));
        break;
    }
    case Op::AddiB:
    case Op::AddiW:
    case Op::AddiL: text = immToEa("addi", rel(Op::AddiB)); break;
    case Op::SubiB:
    case Op::SubiW:
    case Op::SubiL: text = immToEa("subi", rel(Op::SubiB)); break;
    case Op::CmpiB:
    case Op::CmpiW:
    case Op::CmpiL: text = immToEa("cmpi", rel(Op::CmpiB)); break;
    case Op::AndiB:
    case Op::AndiW:
    case Op::AndiL: text = immToEa("andi", rel(Op::AndiB)); break;
    case Op::OriB:
    case Op::OriW:
    case Op::OriL: text = immToEa("ori", rel(Op::OriB)); break;
    case Op::EoriB:
    case Op::EoriW:
    case Op::EoriL: text = immToEa("eori", rel(Op::EoriB)); break;
    case Op::AddqB:
    case Op::AddqW:
    case Op::AddqL:
    case Op::SubqB:
    case Op::SubqW:
    case Op::SubqL: {
        const bool sub = k >= Op::SubqB;
        const int sz = rel(sub ? Op::SubqB : Op::AddqB);
        text = line(std::string(sub ? "subq" : "addq") + sfx(sz), "#" + quick(rn) + "," + ea(f, field, sz));
        break;
    }
    case Op::CmpB:
    case Op::CmpW:
    case Op::CmpL: text = eaToReg("cmp", rel(Op::CmpB), dreg(rn)); break;
    case Op::CmpaW: text = eaToReg("cmpa", kWord, areg(rn)); break;
    case Op::CmpaL: text = eaToReg("cmpa", kLong, areg(rn)); break;
    case Op::CmpmB:
    case Op::CmpmW:
    case Op::CmpmL:
        text = line(std::string("cmpm") + sfx(rel(Op::CmpmB)), ind(ry) + "+," + ind(rn) + "+");
        break;
    case Op::NegB:
    case Op::NegW:
    case Op::NegL: text = unary("neg", rel(Op::NegB)); break;
    case Op::NegxB:
    case Op::NegxW:
    case Op::NegxL: text = unary("negx", rel(Op::NegxB)); break;
    case Op::NotB:
    case Op::NotW:
    case Op::NotL: text = unary("not", rel(Op::NotB)); break;
    case Op::TstB:
    case Op::TstW:
    case Op::TstL: text = unary("tst", rel(Op::TstB)); break;
    case Op::Mulu: text = eaToReg("mulu", kWord, dreg(rn)); break;
    case Op::Muls: text = eaToReg("muls", kWord, dreg(rn)); break;
    case Op::Divu: text = eaToReg("divu", kWord, dreg(rn)); break;
    case Op::Divs: text = eaToReg("divs", kWord, dreg(rn)); break;
    case Op::Abcd:
    case Op::Sbcd: {
        const bool mem = (op & 0x0008) != 0;
        text = line(k == Op::Abcd ? "abcd" : "sbcd",
                    mem ? "-" + ind(ry) + ",-" + ind(rn) : dreg(ry) + "," + dreg(rn));
        break;
    }
    case Op::Nbcd: text = line("nbcd", ea(f, field, kByte)); break;
    case Op::Chk: text = eaToReg("chk", kWord, dreg(rn)); break;
    case Op::AndB:
    case Op::AndW:
    case Op::AndL: text = eaToReg("and", rel(Op::AndB), dreg(rn)); break;
    case Op::AndToMemB:
    case Op::AndToMemW:
    case Op::AndToMemL: text = regToEa("and", rel(Op::AndToMemB)); break;
    case Op::OrB:
    case Op::OrW:
    case Op::OrL: text = eaToReg("or", rel(Op::OrB), dreg(rn)); break;
    case Op::OrToMemB:
    case Op::OrToMemW:
    case Op::OrToMemL: text = regToEa("or", rel(Op::OrToMemB)); break;
    case Op::EorB:
    case Op::EorW:
    case Op::EorL: text = regToEa("eor", rel(Op::EorB)); break;
    case Op::ShiftRegB:
    case Op::ShiftRegW:
    case Op::ShiftRegL: {
        const int sz = rel(Op::ShiftRegB);
        const std::string count = (op & 0x0020) != 0 ? dreg(rn) : "#" + quick(rn);
        text = line(std::string(kShiftKind[(op >> 3) & 3]) + ((op & 0x0100) != 0 ? "l" : "r") + sfx(sz),
                    count + "," + dreg(ry));
        break;
    }
    case Op::ShiftMem:
        text = line(std::string(kShiftKind[(op >> 9) & 3]) + ((op & 0x0100) != 0 ? "l" : "r") + ".w",
                    ea(f, field, kWord));
        break;
    case Op::BtstDyn:
    case Op::BchgDyn:
    case Op::BclrDyn:
    case Op::BsetDyn: text = line(kBitOp[rel(Op::BtstDyn)], dreg(rn) + "," + ea(f, field, kByte)); break;
    case Op::BtstImm:
    case Op::BchgImm:
    case Op::BclrImm:
    case Op::BsetImm: {
        const std::string bit = "#" + num(f.word() & 0xFF);
        text = line(kBitOp[rel(Op::BtstImm)], bit + "," + ea(f, field, kByte));
        break;
    }
    case Op::Scc: text = line(std::string("s") + kCond[(op >> 8) & 15], ea(f, field, kByte)); break;
    case Op::Tas: text = line("tas", ea(f, field, kByte)); break;
    case Op::Bcc:
    case Op::Bsr: {
        const uint32_t at = f.here();  // displacements count from the word after the opcode
        const bool word = (op & 0xFF) == 0;
        const int32_t disp = word ? sext16(f.word()) : sext8(op);
        const uint32_t target = (at + static_cast<uint32_t>(disp)) & kAddrMask;
        const std::string name = k == Op::Bsr ? "bsr" : (op & 0x0F00) == 0 ? "bra" : std::string("b") + kCond[(op >> 8) & 15];
        text = line(name + (word ? ".w" : ".s"), hex(target));
        break;
    }
    case Op::Dbcc: {
        const uint32_t at = f.here();
        const uint32_t target = (at + static_cast<uint32_t>(sext16(f.word()))) & kAddrMask;
        text = line(std::string("db") + kCond[(op >> 8) & 15], dreg(ry) + "," + hex(target));
        break;
    }
    case Op::Jmp: text = line("jmp", ea(f, field, kLong)); break;
    case Op::Jsr: text = line("jsr", ea(f, field, kLong)); break;
    case Op::Rts: text = "rts"; break;
    case Op::Rtr: text = "rtr"; break;
    case Op::Rte: text = "rte"; break;
    case Op::Trap: text = line("trap", "#" + std::to_string(op & 15)); break;
    case Op::Trapv: text = "trapv"; break;
    case Op::Reset: text = "reset"; break;
    case Op::Stop: text = line("stop", "#" + num(f.word())); break;
    case Op::MoveFromSr: text = line("move.w", "sr," + ea(f, field, kWord)); break;
    case Op::MoveToCcr: text = eaToReg("move", kWord, "ccr"); break;
    case Op::MoveToSr: text = eaToReg("move", kWord, "sr"); break;
    case Op::AndiToCcr: text = line("andi.b", "#" + num(f.word() & 0xFF) + ",ccr"); break;
    case Op::OriToCcr: text = line("ori.b", "#" + num(f.word() & 0xFF) + ",ccr"); break;
    case Op::EoriToCcr: text = line("eori.b", "#" + num(f.word() & 0xFF) + ",ccr"); break;
    case Op::AndiToSr: text = line("andi.w", "#" + num(f.word()) + ",sr"); break;
    case Op::OriToSr: text = line("ori.w", "#" + num(f.word()) + ",sr"); break;
    case Op::EoriToSr: text = line("eori.w", "#" + num(f.word()) + ",sr"); break;
    default: {
        // ADD / SUB family (enumerators in the same order in both): <ea>,Dn; Dn,<ea>; ADDA; ADDX.
        const bool sub = k >= Op::SubB && k <= Op::SubxL;
        const int r = rel(sub ? Op::SubB : Op::AddB);
        const char* name = sub ? "sub" : "add";
        if (r < 3) {
            text = eaToReg(name, r, dreg(rn));
        } else if (r < 6) {
            text = regToEa(name, r - 3);
        } else if (r < 8) {
            text = eaToReg((std::string(name) + "a").c_str(), r == 6 ? kWord : kLong, areg(rn));
        } else {
            const bool mem = (op & 0x0008) != 0;
            text = line(std::string(name) + "x" + sfx(r - 8),
                        mem ? "-" + ind(ry) + ",-" + ind(rn) : dreg(ry) + "," + dreg(rn));
        }
        break;
    }
    }
    return DisLine{static_cast<int>(f.pos), text};
}

}  // namespace ens::m68k
