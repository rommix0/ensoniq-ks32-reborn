#pragma once
// The KS-32 as a machine: the 68000, the MC68681 DUART and the OTIS/ESP board joined by the
// address map (K1), run slot by slot against the OTIS voice scan (K4). Pure emulation:
// no file I/O (the caller reads the ROM files). Integer-only; state is bit-identical across builds.
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "duart/duart.h"
#include "ks32/board.h"
#include "ks32/panel.h"
#include "m68k/m68k.h"

namespace ens::ks32 {

struct RomSet {  // file images, unmodified
    std::vector<uint8_t> osUpper, osLower;  // 0x20000 and 0x10000 bytes
    std::vector<uint8_t> u25, u26, rom2, rom3;
};

enum class Device : uint8_t { Otis, Esp, Duart, Unmapped, Iack };

struct BusEvent {
    uint64_t cycle;  // cpu.totalCycles() when the access is made
    uint32_t addr;   // 24-bit address; Iack: the level
    uint16_t value;  // value read or written; Iack: the vector answered
    uint8_t size;    // 1 or 2
    bool write;
    Device dev;
};

constexpr uint32_t kCpuHz = 15'238'090, kDuartHz = 4'000'000;  // DECISION K3
// DECISION K3, K16: the DUART's 16x serial clocks on IP2..IP5 (16 MHz / 32): 31 250 baud.
constexpr uint32_t kSerialClockHz = 500'000;
constexpr int kSlotClocks = 16;  // DECISION K3, K4
// The emulated time the OS v3.01 takes from power-on to its steady state, the play screen: about
// 2121 ms (the last display byte sent, with the K13 resting analog values;
// docs/ks32-hardware-interface.md), plus 50 %, rounded up.
constexpr uint32_t kBootMs = 3200;
// DECISION K7: the front panel is on DUART channel B (0 = A, 1 = B).
constexpr int kPanelChannel = 1;
// DECISION K2: the DUART answers the acknowledge with its IVR; both levels are provisional
// (the boot cannot tell them apart) and are settled in Task 7.
constexpr int kOtisIrqLevel = 1;
constexpr int kDuartIrqLevel = 3;

// The analog inputs, named after their Analog Test readings (service manual PDF p20-21): PW=,
// Mod=, CV=, Vol=, Knob=, Bat= and Pressure=. Each is a raw 10-bit OTIS POT conversion, 0..0x3FF
// (docs/ks32-hardware-interface.md, "Analog inputs").
enum class Analog : uint8_t { PitchWheel, ModWheel, CvPedal, Volume, DataEntry, Pressure, Battery };
// DECISION K13: the resting raw values, by Analog, and the readings they give: pitch wheel centred
// (PW=64; the firmware takes the centre at power-on), mod wheel 0, CV pedal unplugged (127), volume
// at maximum (127), data entry at mid (Knob=127 of 0-255), pressure 0, battery nominal (Bat=164,
// mid-band of the manual's 125-205).
inline constexpr uint16_t kAnalogRest[] = {0x200, 0x3FF, 0x000, 0x3FF, 0x16E, 0x000, 0x298};

class Machine : public m68k::Bus {
public:
    // nullptr on a wrong size, with err naming the file ("OS upper", "OS lower", "u25", ...).
    static std::unique_ptr<Machine> create(const RomSet& roms, std::string& err);
    Machine(const Machine&) = delete;  // cpu_ holds a Bus& to *this
    Machine& operator=(const Machine&) = delete;
    void runFrame(int16_t& mainL, int16_t& mainR);  // until OTIS latches the next frame
    uint64_t cycles() const { return cpu_.totalCycles(); }  // CPU cycles since power-on
    uint16_t peek16(uint32_t addr) const;  // ROM and RAM only, devices read 0, no side effects
    std::function<void(const BusEvent&)> onBusEvent;  // the bus trace; off when empty
    m68k::Cpu& cpu() { return cpu_; }
    Board& board() { return board_; }
    duart::Duart& duart() { return duart_; }
    const PanelState& panel() const { return panel_.state(); }
    void button(Button b, bool down) { panel_.button(b, down); }  // reaches RxDB from the next slot
    void setAnalog(Analog a, uint16_t raw);  // raw 0..0x3FF (masked); PAR follows at once (K8)

    // m68k::Bus
    uint8_t read8(uint32_t addr) override;
    uint16_t read16(uint32_t addr) override;
    void write8(uint32_t addr, uint8_t v) override;
    void write16(uint32_t addr, uint16_t v) override;
    int interruptAcknowledge(int level) override;
    void resetPeripherals() override;

private:
    Machine(std::vector<uint16_t> bank0, std::vector<uint16_t> bank1);
    uint8_t romByte(uint32_t off) const;
    uint8_t memByte(uint32_t a, bool& mapped) const;  // ROM and RAM
    void trace(uint32_t addr, uint16_t value, uint8_t size, bool write, Device dev);
    void updateLines();  // IPL and the ESP's HALT pin
    void slot(int i, int n);

    Board board_;
    duart::Duart duart_{kDuartHz};
    Panel panel_;
    m68k::Cpu cpu_{*this};
    std::vector<uint8_t> osUpper_, osLower_;
    std::vector<uint8_t> ram_ = std::vector<uint8_t>(0x10000, 0);  // 64 KB (K1)
    bool overlay_ = true;  // reads of 0-7 come from ROM until the first write there
    // The POT multiplexer's inputs by select (DUART OP3..OP0, K8). 0x0F is the converter
    // reference ("Reference=" on Analog Test screen 3); 7-14 are never selected.
    std::array<uint16_t, 16> mux_{};
    int carry_ = 0;        // CPU cycles run past the end of the last slot
    uint32_t duartAcc_ = 0;
};

}  // namespace ens::ks32
