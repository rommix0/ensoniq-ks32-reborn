#include "ks32/board.h"

#include <utility>

namespace ens::ks32 {

Board::Board(std::vector<uint16_t> bank0, std::vector<uint16_t> bank1) {
    waves_.bank[0] = std::move(bank0);
    waves_.bank[1] = std::move(bank1);
}

void Board::espHostWrite(uint8_t addr, uint8_t v) {
    esp_.hostWrite(addr, v);
    if (esp_.halted()) esp_.idleStep();
}

void Board::route(const OtisFrame& f, int16_t& mainL, int16_t& mainR, int espSlots) {
    esp_.setSerialIn(0, f.ch[1][0], f.ch[1][1]);
    esp_.setSerialIn(1, f.ch[2][0], f.ch[2][1]);
    esp_.setSerialIn(2, f.ch[3][0], f.ch[3][1]);
    if (espSlots > 0) esp_.advance(espSlots);
    esp_.serialOut(3, mainL, mainR);
}

void Board::frame(int16_t& mainL, int16_t& mainR) { route(otis_.runFrame(), mainL, mainR, kEspSlotsPerFrame); }

}  // namespace ens::ks32
