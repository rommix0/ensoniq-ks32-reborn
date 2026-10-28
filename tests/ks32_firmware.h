#pragma once
// Helpers for the tests that run the KS-32 OS ROM on the machine (ENS_ROM_DIR).
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "ks32/machine.h"

// The OS and wave ROM files from ENS_ROM_DIR, unchecked; false when a file is missing.
inline bool readKs32Roms(ens::ks32::RomSet& r) {
    const std::string dir = ENS_ROM_DIR;
    const auto read = [](const std::string& path, std::vector<uint8_t>& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    };
    return read(dir + "/ks32/ks32v301upper.bin", r.osUpper) && read(dir + "/ks32/ks32v301lower.bin", r.osLower) &&
           read(dir + "/sq1-u25.bin", r.u25) && read(dir + "/sq1-u26.bin", r.u26) &&
           read(dir + "/rom2.u39", r.rom2) && read(dir + "/rom3.u38", r.rom3);
}

// The machine on the ROMs from ENS_ROM_DIR; nullptr when a file is missing (the caller prints
// "  skip: ROMs not found"). Files that exist but are rejected (a wrong size) print the error
// and fail the running test, so a bad ROM set never passes as a skip.
inline std::unique_ptr<ens::ks32::Machine> loadKs32() {
    ens::ks32::RomSet r;
    if (!readKs32Roms(r)) return nullptr;
    std::string err;
    auto m = ens::ks32::Machine::create(r, err);
    if (!m) {
        std::printf("  FAIL: ROMs present but rejected: %s\n", err.c_str());
        ++ens_test::failures();
    }
    return m;
}

// Runs 29 762 frames per 1000 ms of emulated time; the audio is discarded.
inline void runMs(ens::ks32::Machine& m, uint32_t ms) {
    const uint64_t frames = static_cast<uint64_t>(ms) * ens::ks32::kWavRate / 1000;
    int16_t l, r;
    for (uint64_t f = 0; f < frames; ++f) m.runFrame(l, r);
}

// FNV-1a 64 over every BusEvent field while running ms; the machine's own hook is restored.
inline uint64_t traceHash(ens::ks32::Machine& m, uint32_t ms) {
    uint64_t h = 14695981039346656037ull;
    const auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xFF;
            h *= 1099511628211ull;
        }
    };
    auto saved = m.onBusEvent;
    m.onBusEvent = [&](const ens::ks32::BusEvent& e) {
        mix(e.cycle);
        mix(e.addr);
        mix(e.value);
        mix(e.size);
        mix(e.write);
        mix(static_cast<uint64_t>(e.dev));
    };
    runMs(m, ms);
    m.onBusEvent = saved;
    return h;
}
