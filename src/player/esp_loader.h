#pragma once
// Firmware stand-in for the ES5510 ESP: boots the chip and loads KS-32 effect programs purely
// through the host interface (register writes, the HALT pin and waits), spread over frames the
// way the KS-32 OS does it (docs/rom-investigation/data/esp_init_sequence.json).
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "ks32/board.h"
#include "player/effects.h"

namespace ens::player {

class EspSequencer {
public:
    void queueBoot();                                     // DECISION P14: boot_init, RAM clear, unhalt
    void queueLoad(const EspProgram& p, int fadeFrames);  // program_load (fade out, load, fade in)
    // Once per frame, before Board::frame: executes queued ops until one blocks.
    void run(ks32::Board& b);
    bool idle() const { return q_.empty(); }
    std::vector<std::string> takeLog();  // "effect 3 loaded (id 02)" etc.

private:
    enum class Kind { Write, HaltPin, WaitAccess, WaitClear, WaitFrames, Log };
    struct Op {
        Kind kind;
        uint8_t addr = 0;  // Write: host register
        uint32_t v = 0;    // Write: value; HaltPin: level; WaitFrames: frames left
        std::string msg;   // Log
    };

    void push(Kind k, uint8_t addr = 0, uint32_t v = 0) { q_.push_back(Op{k, addr, v, {}}); }
    void write(uint8_t addr, uint8_t v) { push(Kind::Write, addr, v); }
    void gprData(uint32_t v24);
    void instrData(uint64_t w48);
    void gprWrite(uint8_t addr, uint32_t v24);  // $00-$02, $A0, WaitAccess
    void instrWrite(uint8_t addr, uint64_t w48);  // $03-$08, $C0, WaitAccess
    void gprInstrWrite(uint8_t addr, uint32_t v24, uint64_t w48);  // $00-$08, $E0, WaitAccess
    void ramp(uint32_t to, int frames);  // GPR 0x7F from level_ to `to`

    std::deque<Op> q_;
    std::vector<std::string> log_;
    uint32_t level_ = 0;  // last GPR 0x7F value queued: the firmware's RAM copy, never read back
};

}  // namespace ens::player
