#pragma once
// KS-32 board: the two OTIS wave banks, one Otis, one Esp and the 64K-word ESP delay DRAM,
// wired with the KS-32 routing. Integer-only; no firmware state is set up here (that is the
// host sequencer's and the voice manager's job).
#include <cstdint>
#include <vector>

#include "esp/esp.h"
#include "otis/otis.h"

namespace ens::ks32 {

constexpr int kEspSlotsPerFrame = 89;   // DECISION P1: findings.md section B, 89 slots per frame
constexpr uint32_t kDramWords = 65536;  // findings.md section D (MEMSIZ = 0x0000FF)
constexpr uint32_t kWavRate = 29762;    // 30476180 / 1024 = 29761.9 Hz, rounded for WAV headers

class Board {
public:
    // Banks may be shorter than 1M words: reads past the end return 0.
    Board(std::vector<uint16_t> bank0, std::vector<uint16_t> bank1);
    Board(const Board&) = delete;  // Otis and Esp hold references to the members below
    Board& operator=(const Board&) = delete;

    Otis& otis() { return otis_; }
    Esp& esp() { return esp_; }
    uint16_t dram(uint32_t word) const { return dram_.ram[word & (kDramWords - 1)]; }

    // Host write to the ESP, then one Esp::idleStep() while halted so select-register
    // transfers land (same convention as DECISION TR2 in trace.cpp).
    void espHostWrite(uint8_t addr, uint8_t v);

    // One sample frame. DECISION TR1 routing (findings.md section C): OTIS CH1..CH3 -> ESP
    // SER0..SER2; esp.advance(kEspSlotsPerFrame); ESP SER3 -> main. OTIS CH0 (aux) is dropped.
    void frame(int16_t& mainL, int16_t& mainR);

private:
    struct Waves : WaveMemory {
        std::vector<uint16_t> bank[2];
        uint16_t read(int b, uint32_t wordAddr) override {
            const std::vector<uint16_t>& m = bank[b & 1];
            return wordAddr < m.size() ? m[wordAddr] : 0;
        }
    };
    // Word addresses are masked to the power-of-two size; I/O space reads 0 and ignores writes.
    struct Dram : EspBus {
        std::vector<uint16_t> ram = std::vector<uint16_t>(kDramWords, 0);
        uint16_t read(bool io, uint32_t a) override { return io ? 0 : ram[a & (kDramWords - 1)]; }
        void write(bool io, uint32_t a, uint16_t v) override {
            if (!io) ram[a & (kDramWords - 1)] = v;
        }
    };

    Waves waves_;
    Dram dram_;
    Otis otis_{waves_};
    Esp esp_{dram_};
};

}  // namespace ens::ks32
