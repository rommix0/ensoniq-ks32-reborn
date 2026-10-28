// KS-32 firmware smoke run on the 68000 core alone (Plan 5 Task 9). The firmware is run, never
// studied (design section 5): the test prints only the number of distinct opcode words executed
// and the last instruction address, and asserts that the CPU neither halted nor took a bus
// error, address error, illegal instruction, privilege violation or line A/F exception.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

#include "check.h"
#include "m68k/m68k.h"

using ens::m68k::Cpu;

namespace {

bool readBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// The CPU's view without the KS-32 devices (their mapping is Plan 6's): the OS EPROMs merged at
// 0xC00000-0xC3FFFF (upper = even bytes, 128 KB; lower = odd bytes, 64 KB, so only
// 0xC00000-0xC1FFFF has odd bytes; the odd bytes above read 0, like every unmapped address), the
// reset vectors at 0-7 served from 0xC00000-0xC00007, RAM at 0x000008-0x00FFFF and at
// 0xFF0000-0xFFFFFF, reads of 0 and ignored writes everywhere else.
struct SmokeBus : ens::m68k::Bus {
    static constexpr uint32_t kRomBase = 0xC00000, kRomSize = 0x40000;
    std::vector<uint8_t> rom = std::vector<uint8_t>(kRomSize, 0);
    std::vector<uint8_t> lowRam = std::vector<uint8_t>(0x10000, 0);
    std::vector<uint8_t> highRam = std::vector<uint8_t>(0x10000, 0);

    uint8_t* ram(uint32_t a) {
        if (a >= 8 && a < 0x10000) return &lowRam[a];
        if (a >= 0xFF0000 && a <= 0xFFFFFF) return &highRam[a - 0xFF0000];
        return nullptr;
    }
    uint8_t read8(uint32_t a) override {
        if (a < 8) return rom[a];
        if (a >= kRomBase && a < kRomBase + kRomSize) return rom[a - kRomBase];
        const uint8_t* p = ram(a);
        return p ? *p : 0;
    }
    uint16_t read16(uint32_t a) override {
        return static_cast<uint16_t>((read8(a) << 8) | read8(a + 1));
    }
    void write8(uint32_t a, uint8_t v) override {
        if (uint8_t* p = ram(a)) *p = v;
    }
    void write16(uint32_t a, uint16_t v) override {
        write8(a, static_cast<uint8_t>(v >> 8));
        write8(a + 1, static_cast<uint8_t>(v));
    }
};

}  // namespace

TEST(m68k_ks32_firmware_smoke) {
    const std::string dir = std::string(ENS_ROM_DIR) + "/ks32";
    std::vector<uint8_t> upper, lower;
    if (!readBytes(dir + "/ks32v301upper.bin", upper) || !readBytes(dir + "/ks32v301lower.bin", lower)) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    CHECK_EQ(upper.size(), 0x20000u);
    CHECK_EQ(lower.size(), 0x10000u);
    if (upper.size() != 0x20000 || lower.size() != 0x10000) return;
    SmokeBus bus;
    for (size_t i = 0; i < upper.size(); ++i) bus.rom[2 * i] = upper[i];
    for (size_t i = 0; i < lower.size(); ++i) bus.rom[2 * i + 1] = lower[i];

    Cpu cpu(bus);
    std::vector<bool> seen(0x10000, false);
    cpu.onInstruction = [&bus, &seen](uint32_t pc) { seen[bus.read16(pc)] = true; };
    cpu.reset();
    for (int slice = 0; slice < 50000; ++slice) cpu.run(1000);  // 50 M cycles

    int distinct = 0;
    for (bool s : seen) distinct += s ? 1 : 0;
    std::printf("  distinct opcode words %d, last instruction at 0x%06X, halted %s\n", distinct,
                static_cast<unsigned>(cpu.instructionAddress()), cpu.halted() ? "yes" : "no");
    CHECK(cpu.totalCycles() >= 50000000u);
    CHECK(!cpu.halted());
    for (int v : {2, 3, 4, 8, 10, 11}) {
        if (cpu.exceptionCount(v) != 0) std::printf("  vector %d taken %u times\n", v, cpu.exceptionCount(v));
        CHECK_EQ(cpu.exceptionCount(v), 0u);
    }
}
