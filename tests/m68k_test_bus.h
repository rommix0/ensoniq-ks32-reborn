#pragma once
// Flat 16 MB RAM bus for the 68000 core tests. Every address is masked to 24 bits.
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "m68k/m68k.h"

struct RamBus : ens::m68k::Bus {
    static constexpr uint32_t kMask = 0xFFFFFF;

    std::vector<uint8_t> mem = std::vector<uint8_t>(size_t(1) << 24, 0);
    int resets = 0;                            // resetPeripherals() calls
    int ackVector = ens::m68k::kAutovector;    // what interruptAcknowledge() answers

    uint8_t read8(uint32_t addr) override { return mem[addr & kMask]; }
    uint16_t read16(uint32_t addr) override {
        return static_cast<uint16_t>((mem[addr & kMask] << 8) | mem[(addr + 1) & kMask]);
    }
    void write8(uint32_t addr, uint8_t v) override { mem[addr & kMask] = v; }
    void write16(uint32_t addr, uint16_t v) override {
        mem[addr & kMask] = static_cast<uint8_t>(v >> 8);
        mem[(addr + 1) & kMask] = static_cast<uint8_t>(v);
    }
    int interruptAcknowledge(int level) override {
        (void)level;
        return ackVector;
    }
    void resetPeripherals() override { ++resets; }

    // Store big-endian words from addr on.
    void load16(uint32_t addr, std::initializer_list<uint16_t> words) {
        for (uint16_t w : words) {
            write16(addr, w);
            addr += 2;
        }
    }
    void load32(uint32_t addr, uint32_t v) {
        write16(addr, static_cast<uint16_t>(v >> 16));
        write16(addr + 2, static_cast<uint16_t>(v));
    }
    uint16_t peek16(uint32_t addr) { return read16(addr); }
    uint32_t peek32(uint32_t addr) {
        return (static_cast<uint32_t>(read16(addr)) << 16) | read16(addr + 2);
    }
};
