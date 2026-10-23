// 68000 effective addresses: address calculation, operand load and store by mode and size.
// Timing is never tabulated here: an operand costs the bus cycles it actually runs (4 clocks
// each, counted by the bus functions) plus the internal clocks UM table 8-1 implies, which
// are the 2 clocks of -(An) and of the index modes.
#include "m68k/m68k.h"

namespace ens::m68k {

// ---- size helpers -------------------------------------------------------------------------

uint32_t Cpu::signExtend(uint32_t v, int size) {
    if (size == kByte) return ((v & 0xFFu) ^ 0x80u) - 0x80u;
    if (size == kWord) return ((v & 0xFFFFu) ^ 0x8000u) - 0x8000u;
    return v;
}

void Cpu::setDreg(int r, int size, uint32_t v) {
    const uint32_t m = sizeMask(size);
    d_[r] = (d_[r] & ~m) | (v & m);
}

void Cpu::setLogicFlags(uint32_t v, int size) {
    const uint32_t m = sizeMask(size);
    uint16_t ccr = static_cast<uint16_t>(sr_ & kCcrX);
    if ((v & m) == 0) ccr |= kCcrZ;
    if ((v & (m ^ (m >> 1))) != 0) ccr |= kCcrN;
    setCcr(ccr);
}

// ---- address calculation ------------------------------------------------------------------

uint32_t Cpu::indexWord(uint32_t base) {
    // Brief extension word (PRM 2.1): D/A, register, W/L, 8-bit displacement; bits 10..8
    // are ignored by the 68000.
    const uint16_t ext = nextWord();
    const int r = (ext >> 12) & 7;
    uint32_t x = (ext & 0x8000) != 0 ? a_[r] : d_[r];
    if ((ext & 0x0800) == 0) x = signExtend(x, kWord);
    return base + signExtend(ext, kByte) + x;
}

Cpu::Loc Cpu::locate(uint16_t field, int size) {
    Loc l{eaIndex(field), field & 7, 0};
    // (An)+ and -(An) step a byte operand through A7 by 2, keeping the stack word-aligned.
    const uint32_t stepBy = (size == kByte && l.reg == 7) ? 2u : static_cast<uint32_t>(size);
    switch (l.mode) {
    case 0:  // Dn
    case 1:  // An
        break;
    case 2: l.addr = a_[l.reg]; break;
    case 3:
        l.addr = a_[l.reg];
        a_[l.reg] += stepBy;
        break;
    case 4:
        idle(2);
        a_[l.reg] -= stepBy;
        l.addr = a_[l.reg];
        break;
    case 5: l.addr = a_[l.reg] + signExtend(nextWord(), kWord); break;
    case 6:
        idle(2);
        l.addr = indexWord(a_[l.reg]);
        break;
    case 7: l.addr = signExtend(nextWord(), kWord); break;
    case 8: {
        const uint32_t hi = nextWord();
        l.addr = (hi << 16) | nextWord();
        break;
    }
    case 9: {
        const uint32_t base = pc_;  // the address of the extension word
        l.addr = base + signExtend(nextWord(), kWord);
        break;
    }
    case 10:
        idle(2);
        l.addr = indexWord(pc_);
        break;
    default:  // 11: #imm, the value itself (a byte immediate is the low byte of its word)
        if (size == kLong) {
            const uint32_t hi = nextWord();
            l.addr = (hi << 16) | nextWord();
        } else {
            l.addr = nextWord() & sizeMask(size);
        }
        break;
    }
    return l;
}

uint32_t Cpu::controlAddress(uint16_t field, int indexClocks) {
    const int r = field & 7;
    switch (eaIndex(field)) {
    case 2: return a_[r];
    case 5: return a_[r] + signExtend(nextWord(), kWord);
    case 6: idle(indexClocks); return indexWord(a_[r]);
    case 7: return signExtend(nextWord(), kWord);
    case 8: {
        const uint32_t hi = nextWord();
        return (hi << 16) | nextWord();
    }
    case 9: {
        const uint32_t base = pc_;
        return base + signExtend(nextWord(), kWord);
    }
    default:  // 10 (decodeOp admits no other mode here)
        idle(indexClocks);
        return indexWord(pc_);
    }
}

// ---- operand load and store ---------------------------------------------------------------

uint32_t Cpu::load(const Loc& l, int size) {
    switch (l.mode) {
    case 0: return d_[l.reg] & sizeMask(size);
    case 1: return a_[l.reg] & sizeMask(size);
    case 11: return l.addr;
    default: break;
    }
    // DECISION M12: PC-relative operands are program-space reads (PRM 2.0); the suite
    // records them as data reads, which only the address-error frame's FC can show.
    const bool program = l.mode == 9 || l.mode == 10;
    if (size == kByte) return read8(l.addr);
    if (size == kWord) return read16(l.addr, program);
    return read32(l.addr, program);
}

void Cpu::storeRmw(const Loc& l, int size, uint32_t v) {
    switch (l.mode) {
    case 0: setDreg(l.reg, size, v); return;
    case 1: a_[l.reg] = v; return;  // whole register: the caller has sign-extended
    default: break;
    }
    if (size == kByte) write8(l.addr, static_cast<uint8_t>(v));
    else if (size == kWord) write16(l.addr, static_cast<uint16_t>(v));
    else write32Desc(l.addr, v);  // M13: read-modify-write long stores go low word first
}

}  // namespace ens::m68k
