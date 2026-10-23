#include "m68k/m68k_decode.h"

namespace ens::m68k {

namespace {

// MOVE, MOVEA (lines 1..3). Size field: 1 byte, 3 word, 2 long.
Op decodeMove(uint16_t op) {
    const uint16_t src = op & 0x3F;
    const uint16_t dst = static_cast<uint16_t>(((op >> 9) & 7) | ((op >> 3) & 0x38));  // mode:reg
    const int line = op >> 12;
    if (!eaIn(src, kEaAll)) return Op::Illegal;
    if (eaIndex(dst) == 1) {  // MOVEA: word and long only
        if (line == 1) return Op::Illegal;
        return line == 3 ? Op::MoveaW : Op::MoveaL;
    }
    if (!eaIn(dst, kEaDataAlterable)) return Op::Illegal;
    if (line == 1) return eaIndex(src) == 1 ? Op::Illegal : Op::MoveB;  // no byte An source
    return line == 3 ? Op::MoveW : Op::MoveL;
}

Op decodeLine0(uint16_t op) {
    if ((op & 0xF138) == 0x0108) {  // MOVEP (BTST..BSET Dn,An would be an invalid mode)
        switch ((op >> 6) & 3) {
        case 0: return Op::MovepToRegW;
        case 1: return Op::MovepToRegL;
        case 2: return Op::MovepToMemW;
        default: return Op::MovepToMemL;
        }
    }
    return Op::Illegal;
}

Op decodeLine4(uint16_t op) {
    const uint16_t ea = op & 0x3F;
    if (op == 0x4E71) return Op::Nop;
    if ((op & 0xFF00) == 0x4200) {  // CLR (size 3 is not an instruction on the 68000)
        if (!eaIn(ea, kEaDataAlterable)) return Op::Illegal;
        switch ((op >> 6) & 3) {
        case 0: return Op::ClrB;
        case 1: return Op::ClrW;
        case 2: return Op::ClrL;
        default: return Op::Illegal;
        }
    }
    if ((op & 0xF1C0) == 0x41C0) return eaIn(ea, kEaControl) ? Op::Lea : Op::Illegal;
    if ((op & 0xFFF8) == 0x4840) return Op::Swap;
    if ((op & 0xFFC0) == 0x4840) return eaIn(ea, kEaControl) ? Op::Pea : Op::Illegal;
    if ((op & 0xFFF8) == 0x4880) return Op::ExtW;
    if ((op & 0xFFF8) == 0x48C0) return Op::ExtL;
    if ((op & 0xFB80) == 0x4880) {  // MOVEM
        const bool toReg = (op & 0x0400) != 0;
        const bool isLong = (op & 0x0040) != 0;
        if (toReg) {
            if (!eaIn(ea, kEaControl | (1u << 3))) return Op::Illegal;  // control or (An)+
            return isLong ? Op::MovemToRegL : Op::MovemToRegW;
        }
        if (!eaIn(ea, kEaControlAlterable | (1u << 4))) return Op::Illegal;  // or -(An)
        return isLong ? Op::MovemToMemL : Op::MovemToMemW;
    }
    switch (op & 0xFFF8) {
    case 0x4E50: return Op::Link;
    case 0x4E58: return Op::Unlk;
    case 0x4E60: return Op::MoveToUsp;
    case 0x4E68: return Op::MoveFromUsp;
    default: break;
    }
    // Everything else, including ILLEGAL (4AFC), takes the illegal-instruction exception.
    return Op::Illegal;
}

Op decodeLineC(uint16_t op) {
    switch (op & 0xF1F8) {
    case 0xC140:  // EXG Dx,Dy
    case 0xC148:  // EXG Ax,Ay
    case 0xC188:  // EXG Dx,Ay
        return Op::Exg;
    default: return Op::Illegal;
    }
}

}  // namespace

Op decodeOp(uint16_t opcode) {
    switch (opcode >> 12) {
    case 0x0: return decodeLine0(opcode);
    case 0x1:
    case 0x2:
    case 0x3: return decodeMove(opcode);
    case 0x4: return decodeLine4(opcode);
    case 0x7: return (opcode & 0x0100) == 0 ? Op::Moveq : Op::Illegal;
    case 0xA: return Op::LineA;  // PRM/UM 6.3.6: unimplemented, own vectors
    case 0xC: return decodeLineC(opcode);
    case 0xF: return Op::LineF;
    default: return Op::Illegal;
    }
}

}  // namespace ens::m68k
