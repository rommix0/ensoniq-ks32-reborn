#include "m68k/m68k_decode.h"

namespace ens::m68k {

Op decodeOp(uint16_t opcode) {
    switch (opcode >> 12) {
    case 0xA: return Op::LineA;  // PRM/UM 6.3.6: unimplemented, own vectors
    case 0xF: return Op::LineF;
    default: break;
    }
    if (opcode == 0x4E71) return Op::Nop;
    // Everything else, including ILLEGAL (4AFC), takes the illegal-instruction exception.
    return Op::Illegal;
}

}  // namespace ens::m68k
