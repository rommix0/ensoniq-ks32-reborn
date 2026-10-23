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

// Size field ss in bits 7..6 (0 byte, 1 word, 2 long; 3 is another instruction): one of
// three consecutive Op enumerators B, W, L.
Op sized(Op byteOp, uint16_t op) {
    return static_cast<Op>(static_cast<uint16_t>(byteOp) + ((op >> 6) & 3));
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
    const uint16_t ea = op & 0x3F;
    const int bitOp = (op >> 6) & 3;  // BTST, BCHG, BCLR, BSET
    const auto bitAt = [bitOp](Op btst) {
        return static_cast<Op>(static_cast<uint16_t>(btst) + bitOp);
    };
    if ((op & 0x0100) != 0) {  // dynamic: bit number in Dn; BTST also tests an immediate byte
        return eaIn(ea, bitOp == 0 ? kEaData : kEaDataAlterable) ? bitAt(Op::BtstDyn) : Op::Illegal;
    }
    if ((op & 0xFF00) == 0x0800) {  // static: BTST has no immediate operand (PRM)
        const uint16_t allowed = bitOp == 0 ? static_cast<uint16_t>(kEaData & ~(1u << 11)) : kEaDataAlterable;
        return eaIn(ea, allowed) ? bitAt(Op::BtstImm) : Op::Illegal;
    }
    // ORI, ANDI, SUBI, ADDI, EORI, CMPI (the 68000's CMPI has no PC-relative destination).
    // The immediate forms to CCR and SR have an immediate "destination": Task 6.
    if (bitOp == 3 || !eaIn(ea, kEaDataAlterable)) return Op::Illegal;
    switch (op & 0xFF00) {
    case 0x0000: return sized(Op::OriB, op);
    case 0x0200: return sized(Op::AndiB, op);
    case 0x0A00: return sized(Op::EoriB, op);
    case 0x0600: return sized(Op::AddiB, op);
    case 0x0400: return sized(Op::SubiB, op);
    case 0x0C00: return sized(Op::CmpiB, op);
    default: return Op::Illegal;
    }
}

// ADD/SUB family (lines 9 and D). `base` is AddB or SubB; the enumerators keep the same order.
Op decodeAddSub(uint16_t op, Op base) {
    const uint16_t ea = op & 0x3F;
    const int opmode = (op >> 6) & 7;
    const auto at = [base](Op addOp) {
        return static_cast<Op>(static_cast<uint16_t>(base) +
                               (static_cast<uint16_t>(addOp) - static_cast<uint16_t>(Op::AddB)));
    };
    switch (opmode) {
    case 3: return eaIn(ea, kEaAll) ? at(Op::AddaW) : Op::Illegal;
    case 7: return eaIn(ea, kEaAll) ? at(Op::AddaL) : Op::Illegal;
    case 0:  // byte: no An source
        return eaIn(ea, kEaData) ? at(Op::AddB) : Op::Illegal;
    case 1:
    case 2: return eaIn(ea, kEaAll) ? sized(at(Op::AddB), op) : Op::Illegal;
    default:  // 4..6: Dn,<ea>; register modes 0/1 are ADDX/SUBX
        if (((ea >> 3) & 7) <= 1) return sized(at(Op::AddxB), op);
        return eaIn(ea, kEaMemoryAlterable) ? sized(at(Op::AddToMemB), op) : Op::Illegal;
    }
}

// ADDQ/SUBQ (line 5, size 0..2) and Scc (size 3; DBcc, mode 1, is Task 6).
Op decodeLine5(uint16_t op) {
    const uint16_t ea = op & 0x3F;
    if (((op >> 6) & 3) == 3) return eaIn(ea, kEaDataAlterable) ? Op::Scc : Op::Illegal;
    if (!eaIn(ea, kEaDataAlterable | (1u << 1))) return Op::Illegal;
    if (eaIndex(ea) == 1 && ((op >> 6) & 3) == 0) return Op::Illegal;  // no byte to An
    return sized((op & 0x0100) != 0 ? Op::SubqB : Op::AddqB, op);
}

Op decodeLine8(uint16_t op) {
    const uint16_t ea = op & 0x3F;
    if ((op & 0x01C0) == 0x00C0) return eaIn(ea, kEaData) ? Op::Divu : Op::Illegal;
    if ((op & 0x01C0) == 0x01C0) return eaIn(ea, kEaData) ? Op::Divs : Op::Illegal;
    if ((op & 0x01F0) == 0x0100) return Op::Sbcd;
    if ((op & 0x0100) == 0) {  // OR <ea>,Dn (size 3 is DIVU, above)
        return eaIn(ea, kEaData) ? sized(Op::OrB, op) : Op::Illegal;
    }
    return eaIn(ea, kEaMemoryAlterable) ? sized(Op::OrToMemB, op) : Op::Illegal;  // mode 0/1: SBCD, PACK
}

Op decodeLineB(uint16_t op) {
    const uint16_t ea = op & 0x3F;
    const int opmode = (op >> 6) & 7;
    switch (opmode) {
    case 3: return eaIn(ea, kEaAll) ? Op::CmpaW : Op::Illegal;
    case 7: return eaIn(ea, kEaAll) ? Op::CmpaL : Op::Illegal;
    case 0: return eaIn(ea, kEaData) ? Op::CmpB : Op::Illegal;
    case 1:
    case 2: return eaIn(ea, kEaAll) ? sized(Op::CmpB, op) : Op::Illegal;
    default:  // 4..6: CMPM for mode 1; EOR Dn,<ea> otherwise
        if (((ea >> 3) & 7) == 1) return sized(Op::CmpmB, op);
        return eaIn(ea, kEaDataAlterable) ? sized(Op::EorB, op) : Op::Illegal;
    }
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
    if ((op & 0xF1C0) == 0x4180) return eaIn(ea, kEaData) ? Op::Chk : Op::Illegal;
    if ((op & 0xFFC0) == 0x4AC0) return eaIn(ea, kEaDataAlterable) ? Op::Tas : Op::Illegal;  // incl. 4AFC
    if ((op & 0xFF00) == 0x4000 || (op & 0xFF00) == 0x4400 || (op & 0xFF00) == 0x4600 ||
        (op & 0xFF00) == 0x4A00) {
        // NEGX, NEG, NOT, TST; size 3 is MOVE from SR / to CCR / to SR (Task 6).
        if (((op >> 6) & 3) == 3 || !eaIn(ea, kEaDataAlterable)) return Op::Illegal;
        const Op base = (op & 0xFF00) == 0x4000   ? Op::NegxB
                        : (op & 0xFF00) == 0x4400 ? Op::NegB
                        : (op & 0xFF00) == 0x4600 ? Op::NotB
                                                  : Op::TstB;
        return sized(base, op);
    }
    if ((op & 0xFFC0) == 0x4800) return eaIn(ea, kEaDataAlterable) ? Op::Nbcd : Op::Illegal;
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
    const uint16_t ea = op & 0x3F;
    if ((op & 0x01C0) == 0x00C0) return eaIn(ea, kEaData) ? Op::Mulu : Op::Illegal;
    if ((op & 0x01C0) == 0x01C0) return eaIn(ea, kEaData) ? Op::Muls : Op::Illegal;
    if ((op & 0x01F0) == 0x0100) return Op::Abcd;
    switch (op & 0xF1F8) {
    case 0xC140:  // EXG Dx,Dy
    case 0xC148:  // EXG Ax,Ay
    case 0xC188:  // EXG Dx,Ay
        return Op::Exg;
    default: break;
    }
    if ((op & 0x0100) == 0) {  // AND <ea>,Dn
        return eaIn(ea, kEaData) ? sized(Op::AndB, op) : Op::Illegal;
    }
    return eaIn(ea, kEaMemoryAlterable) ? sized(Op::AndToMemB, op) : Op::Illegal;
}

// Line E: shifts and rotates. Register forms have size 0..2 in bits 7..6; size 3 is the
// memory form, which needs a memory alterable operand and has bit 11 clear.
Op decodeLineE(uint16_t op) {
    if (((op >> 6) & 3) != 3) return sized(Op::ShiftRegB, op);
    if ((op & 0x0800) != 0) return Op::Illegal;  // bit field instructions: 68020
    return eaIn(op & 0x3F, kEaMemoryAlterable) ? Op::ShiftMem : Op::Illegal;
}

}  // namespace

Op decodeOp(uint16_t opcode) {
    switch (opcode >> 12) {
    case 0x0: return decodeLine0(opcode);
    case 0x1:
    case 0x2:
    case 0x3: return decodeMove(opcode);
    case 0x4: return decodeLine4(opcode);
    case 0x5: return decodeLine5(opcode);
    case 0x7: return (opcode & 0x0100) == 0 ? Op::Moveq : Op::Illegal;
    case 0x8: return decodeLine8(opcode);
    case 0x9: return decodeAddSub(opcode, Op::SubB);
    case 0xA: return Op::LineA;  // PRM/UM 6.3.6: unimplemented, own vectors
    case 0xB: return decodeLineB(opcode);
    case 0xC: return decodeLineC(opcode);
    case 0xD: return decodeAddSub(opcode, Op::AddB);
    case 0xE: return decodeLineE(opcode);
    case 0xF: return Op::LineF;
    default: return Op::Illegal;
    }
}

}  // namespace ens::m68k
