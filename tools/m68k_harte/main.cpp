// m68k_harte: run Tom Harte's 68000 per-instruction tests (converted to .m68t) against ens_m68k.
//   m68k_harte <dir> [--only NAME[,NAME]] [--max-fail N]
//   m68k_harte --bench
// One line per file: "NAME: pass/total (smp a, aef b, dev c)" where pass includes the three
// allowed classes (DECISION M1: self-modifying prefetch, address-error frame, manual deviation
// by name). The first --max-fail (default 3) failing tests of a file are listed with their
// first 3 differences; a manual-deviation test that passes is reported as a stale entry.
// Exit codes: 0 all tests pass (or the data is missing: "skip"), 1 any failure, 2 usage or
// corrupt data.
// --bench runs a fixed synthetic instruction mix from RAM for 300 M cycles and prints the speed
// in M cycles/s (exit 1 if the mix took an exception, which would make the figure meaningless).
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "m68k/harte_format.h"
#include "m68k/m68k.h"

namespace fs = std::filesystem;
using namespace ens::m68k;

namespace {

int usage() {
    std::fprintf(stderr, "usage: m68k_harte <dir> [--only NAME[,NAME]] [--max-fail N]\n"
                         "       m68k_harte --bench\n");
    return 2;
}

bool readFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) return false;
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

void printFirstLines(const std::string& diff, int maxLines) {
    size_t pos = 0;
    for (int i = 0; i < maxLines && pos < diff.size(); ++i) {
        size_t nl = diff.find('\n', pos);
        if (nl == std::string::npos) nl = diff.size();
        std::printf("    %s\n", diff.substr(pos, nl - pos).c_str());
        pos = nl + 1;
    }
}

// Flat 64 KB RAM, mirrored over the 24-bit space.
struct BenchBus : Bus {
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x10000, 0);
    uint8_t read8(uint32_t a) override { return mem[a & 0xFFFF]; }
    uint16_t read16(uint32_t a) override {
        return static_cast<uint16_t>((mem[a & 0xFFFF] << 8) | mem[(a + 1) & 0xFFFF]);
    }
    void write8(uint32_t a, uint8_t v) override { mem[a & 0xFFFF] = v; }
    void write16(uint32_t a, uint16_t v) override {
        mem[a & 0xFFFF] = static_cast<uint8_t>(v >> 8);
        mem[(a + 1) & 0xFFFF] = static_cast<uint8_t>(v);
    }
};

// The mix: MOVE.l and MOVE.w across register, (An), (An)+, -(An), (d16,An), (d8,An,Xn), abs.l
// and #imm modes, ADD, CMP, Bcc, DBF, JSR/RTS, LSL and MULU, in a DBF loop of 1000 passes
// inside an endless outer loop; the data keep changing (d0 grows by d4 each pass, MULU takes
// the loop counter), so MULU's data-dependent time is typical, not its worst case. Reset
// vectors: SSP 0x8000, PC 0x1000.
constexpr uint32_t kBenchCode = 0x1000;
constexpr uint16_t kBenchProgram[] = {
    0x41F8, 0x4000,          // 1000 lea     ($4000).w,a0
    0x43F8, 0x5000,          // 1004 lea     ($5000).w,a1
    0x7000,                  // 1008 moveq   #0,d0
    0x7C00,                  // 100a moveq   #0,d6
    0x3E3C, 0x03E7,          // 100c outer: move.w #999,d7
    0x2080,                  // 1010 loop: move.l d0,(a0)
    0x2218,                  // 1012 move.l  (a0)+,d1
    0x2101,                  // 1014 move.l  d1,-(a0)
    0x3410,                  // 1016 move.w  (a0),d2
    0x3342, 0x0004,          // 1018 move.w  d2,(4,a1)
    0x3629, 0x0004,          // 101c move.w  (4,a1),d3
    0x283C, 0x1234, 0x5678,  // 1020 move.l  #$12345678,d4
    0x3A31, 0x6002,          // 1026 move.w  (2,a1,d6.w),d5
    0x33C2, 0x0000, 0x5008,  // 102a move.w  d2,($5008).l
    0xD084,                  // 1030 add.l   d4,d0
    0xD647,                  // 1032 add.w   d7,d3
    0xB642,                  // 1034 cmp.w   d2,d3
    0x6702,                  // 1036 beq.s   $103a
    0x5240,                  // 1038 addq.w  #1,d0
    0x4EB8, 0x1048,          // 103a jsr     ($1048).w
    0xE78C,                  // 103e lsl.l   #3,d4
    0xCAC7,                  // 1040 mulu.w  d7,d5
    0x51CF, 0xFFCC,          // 1042 dbf     d7,loop
    0x60C4,                  // 1046 bra.s   outer
    0xB280,                  // 1048 sub: cmp.l d0,d1
    0x4E75,                  // 104a rts
};

int bench() {
    BenchBus bus;
    const uint16_t vectors[] = {0x0000, 0x8000, 0x0000, static_cast<uint16_t>(kBenchCode)};
    for (uint32_t i = 0; i < 4; ++i) bus.write16(2 * i, vectors[i]);
    uint32_t a = kBenchCode;
    for (uint16_t w : kBenchProgram) {
        bus.write16(a, w);
        a += 2;
    }
    Cpu cpu(bus);
    cpu.reset();
    constexpr uint64_t kCycles = 300000000;
    const auto t0 = std::chrono::steady_clock::now();
    while (cpu.totalCycles() < kCycles) cpu.run(1000);
    const auto t1 = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(t1 - t0).count();
    uint32_t exceptions = 0;
    for (int v = 2; v < 256; ++v) exceptions += cpu.exceptionCount(v);
    const Registers r = cpu.regs();
    std::printf("bench: %llu cycles in %.3f s: %.1f M cycles/s (d0 %08X, pc %06X)\n",
                static_cast<unsigned long long>(cpu.totalCycles()), seconds,
                seconds > 0 ? static_cast<double>(cpu.totalCycles()) / seconds / 1e6 : 0.0,
                static_cast<unsigned>(r.d[0]), static_cast<unsigned>(r.pc));
    if (exceptions != 0 || cpu.halted()) {
        std::printf("bench: the mix took %u exceptions%s\n", static_cast<unsigned>(exceptions),
                    cpu.halted() ? " and halted" : "");
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--bench") == 0) return bench();
    std::string dir;
    std::vector<std::string> only;
    int maxFail = 3;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--only" && i + 1 < argc) {
            const std::string list = argv[++i];
            size_t pos = 0;
            while (pos <= list.size()) {
                size_t comma = list.find(',', pos);
                if (comma == std::string::npos) comma = list.size();
                if (comma > pos) only.push_back(list.substr(pos, comma - pos));
                pos = comma + 1;
            }
        } else if (a == "--max-fail" && i + 1 < argc) {
            maxFail = std::atoi(argv[++i]);
        } else if (!a.empty() && a[0] != '-' && dir.empty()) {
            dir = a;
        } else {
            return usage();
        }
    }
    if (dir.empty()) return usage();

    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() == ".m68t") files.push_back(e.path());
        }
    }
    if (files.empty()) {
        std::printf("skip: test data not found (%s)\n", dir.c_str());
        return 0;
    }
    std::sort(files.begin(), files.end());
    if (!only.empty()) {
        std::vector<fs::path> keep;
        for (const std::string& name : only) {
            bool found = false;
            for (const fs::path& f : files) {
                if (f.stem().string() == name) {
                    keep.push_back(f);
                    found = true;
                }
            }
            if (!found) {
                std::fprintf(stderr, "m68k_harte: no test file named %s in %s\n", name.c_str(), dir.c_str());
                return 2;
            }
        }
        files = keep;
    }

    HarteRunner runner;
    uint64_t totalTests = 0, totalPass = 0, totalSmp = 0, totalAef = 0, totalDev = 0;
    for (const fs::path& f : files) {
        std::vector<uint8_t> bytes;
        std::vector<HarteTest> tests;
        std::string err;
        if (!readFile(f, bytes) || !parseHarte(bytes, tests, err)) {
            std::fprintf(stderr, "m68k_harte: %s: %s\n", f.string().c_str(),
                         err.empty() ? "cannot read file" : err.c_str());
            return 2;
        }
        uint64_t pass = 0, smp = 0, aef = 0, dev = 0;
        int listed = 0;
        std::string diff;
        for (const HarteTest& t : tests) {
            const HarteResult r = runner.run(t, diff);
            // DECISION M1: a test passes when it is exact or falls in one of the three allowed
            // classes. A listed manual deviation that no longer fails is a stale table entry; one
            // that fails outside the fields its entry covers is an ordinary FAIL (listed below).
            if (r != HarteResult::ManualDeviation && r != HarteResult::Fail) {
                if (const char* id = manualDeviation(t.name)) {
                    std::printf("  WARNING stale manual deviation (%s): %s no longer fails\n", id,
                                t.name.c_str());
                }
            }
            if (r == HarteResult::ManualDeviation) {
                ++pass;
                ++dev;
            } else if (r == HarteResult::Pass) {
                ++pass;
            } else if (r == HarteResult::SelfModifyingPrefetch) {
                ++pass;
                ++smp;
            } else if (r == HarteResult::AddressErrorFrame) {
                ++pass;
                ++aef;
            } else if (listed < maxFail) {
                ++listed;
                std::printf("  FAIL %s\n", t.name.c_str());
                printFirstLines(diff, 3);
            }
        }
        std::printf("%s: %llu/%llu (smp %llu, aef %llu, dev %llu)\n", f.stem().string().c_str(),
                    static_cast<unsigned long long>(pass), static_cast<unsigned long long>(tests.size()),
                    static_cast<unsigned long long>(smp), static_cast<unsigned long long>(aef),
                    static_cast<unsigned long long>(dev));
        totalTests += tests.size();
        totalPass += pass;
        totalSmp += smp;
        totalAef += aef;
        totalDev += dev;
    }
    std::printf("total: %llu/%llu (smp %llu, aef %llu, dev %llu)\n",
                static_cast<unsigned long long>(totalPass), static_cast<unsigned long long>(totalTests),
                static_cast<unsigned long long>(totalSmp), static_cast<unsigned long long>(totalAef),
                static_cast<unsigned long long>(totalDev));
    return totalPass == totalTests ? 0 : 1;
}
