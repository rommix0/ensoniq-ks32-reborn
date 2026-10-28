// ks32emu: runs the emulated KS-32 (its OS ROM on the 68000 with the DUART and the OTIS/ESP board).
//   ks32emu boot [--roms <dir>] [--ms <n>] [--trace <devs>] [--from <cycle>] [--count <n>] [--last <n>]
// File I/O lives here; src/ks32 stays pure. Errors print "error: <msg>" and exit 2.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../ks32play/files.h"
#include "ks32/machine.h"
#include "m68k/m68k_dis.h"

using namespace ens::ks32;

namespace {

struct Failure {
    std::string msg;
};
[[noreturn]] void fail(const std::string& msg) { throw Failure{msg}; }

struct Options {
    std::string roms = "ks-32";
    uint64_t ms = 3000;
    std::string trace;  // comma list of otis,esp,duart,unmapped,iack
    uint64_t from = 0, count = UINT64_MAX;
    uint32_t last = 0;
};

uint64_t parseNum(const std::string& opt, const std::string& v) {
    char* end = nullptr;
    const unsigned long long n = std::strtoull(v.c_str(), &end, 10);
    if (v.empty() || *end != '\0') fail("bad value '" + v + "' for " + opt);
    return n;
}

Options parseOptions(int argc, char** argv) {
    Options o;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.compare(0, 2, "--") != 0) fail("unexpected argument '" + a + "'");
        if (i + 1 >= argc) fail("option " + a + " needs a value");
        const std::string v = argv[++i];
        if (a == "--roms") o.roms = v;
        else if (a == "--ms") o.ms = parseNum(a, v);
        else if (a == "--trace") o.trace = v;
        else if (a == "--from") o.from = parseNum(a, v);
        else if (a == "--count") o.count = parseNum(a, v);
        else if (a == "--last") o.last = static_cast<uint32_t>(parseNum(a, v));
        else fail("unknown option " + a);
    }
    return o;
}

std::vector<uint8_t> load(const std::string& dir, const std::string& name) {
    std::vector<uint8_t> b;
    std::string err;
    const std::string path = dir + "/" + name;
    if (!readFile(path, b, err)) fail(err + " '" + path + "'");
    return b;
}

const char* devName(Device d) {
    switch (d) {
        case Device::Otis: return "OTIS";
        case Device::Esp: return "ESP";
        case Device::Duart: return "DUART";
        case Device::Unmapped: return "UNMAPPED";
        case Device::Iack: return "IACK";
    }
    return "?";
}

bool parseTrace(const std::string& list, std::set<Device>& out) {
    size_t p = 0;
    while (p <= list.size() && !list.empty()) {
        size_t q = list.find(',', p);
        if (q == std::string::npos) q = list.size();
        const std::string t = list.substr(p, q - p);
        if (t == "otis") out.insert(Device::Otis);
        else if (t == "esp") out.insert(Device::Esp);
        else if (t == "duart") out.insert(Device::Duart);
        else if (t == "unmapped") out.insert(Device::Unmapped);
        else if (t == "iack") out.insert(Device::Iack);
        else return false;
        p = q + 1;
    }
    return true;
}

// Printable ASCII, '.' otherwise.
void printBytes(const std::vector<uint8_t>& b) {
    const size_t n = b.size() < 64 ? b.size() : 64;
    std::printf("    hex:");
    for (size_t i = 0; i < n; ++i) std::printf(" %02x", b[i]);
    std::printf("\n    txt: ");
    for (size_t i = 0; i < n; ++i) std::putchar(b[i] >= 0x20 && b[i] < 0x7F ? b[i] : '.');
    std::printf("\n");
}

int boot(const Options& o) {
    std::set<Device> traced;
    if (!parseTrace(o.trace, traced)) fail("bad value '" + o.trace + "' for --trace (otis,esp,duart,unmapped,iack)");

    RomSet roms;
    roms.osUpper = load(o.roms + "/ks32", "ks32v301upper.bin");
    roms.osLower = load(o.roms + "/ks32", "ks32v301lower.bin");
    roms.u25 = load(o.roms, "sq1-u25.bin");
    roms.u26 = load(o.roms, "sq1-u26.bin");
    roms.rom2 = load(o.roms, "rom2.u39");
    roms.rom3 = load(o.roms, "rom3.u38");
    std::string err;
    auto m = Machine::create(roms, err);
    if (!m) fail(err);

    std::set<uint32_t> unmapped;
    std::map<int, uint8_t> duartLast;  // register -> last value written
    uint64_t printed = 0;
    m->onBusEvent = [&](const BusEvent& e) {
        if (e.dev == Device::Unmapped) unmapped.insert(e.addr);
        if (e.dev == Device::Duart && e.write) duartLast[(e.addr >> 1) & 0xF] = static_cast<uint8_t>(e.value);
        if (!traced.count(e.dev) || e.cycle < o.from || printed >= o.count) return;
        ++printed;
        std::printf("%llu %s %c%u %06x %0*x\n", static_cast<unsigned long long>(e.cycle), devName(e.dev),
                    e.write ? 'W' : 'R', e.size, e.addr, e.size * 2, e.value);
    };

    std::deque<uint32_t> ring;
    if (o.last)
        m->cpu().onInstruction = [&](uint32_t pc) {
            ring.push_back(pc);
            if (ring.size() > o.last) ring.pop_front();
        };

    const uint64_t frames = o.ms * kWavRate / 1000;
    int16_t l, r;
    for (uint64_t f = 0; f < frames; ++f) m->runFrame(l, r);

    std::printf("ran %llu frames (%llu ms), %llu CPU cycles%s\n", static_cast<unsigned long long>(frames),
                static_cast<unsigned long long>(o.ms), static_cast<unsigned long long>(m->cycles()),
                m->cpu().halted() ? ", CPU HALTED" : "");
    std::printf("exceptions:");
    bool any = false;
    for (int v = 0; v < 256; ++v)
        if (const uint32_t n = m->cpu().exceptionCount(v)) {
            std::printf(" v%d=%u", v, n);
            any = true;
        }
    std::printf("%s\n", any ? "" : " none");
    // Distinct unmapped addresses; runs closer than 3 bytes merge into first-last(count); first 16 runs.
    std::printf("unmapped addresses (%zu distinct):", unmapped.size());
    int runs = 0, shown = 0;
    for (auto it = unmapped.begin(); it != unmapped.end();) {
        const uint32_t first = *it;
        uint32_t last = first;
        size_t n = 0;
        for (; it != unmapped.end() && *it - last <= 2; ++it, ++n) last = *it;
        if (++runs <= 16) {
            std::printf(" %06x-%06x(%zu)", first, last, n);
            ++shown;
        }
    }
    if (runs > shown) std::printf(" ... +%d more runs", runs - shown);
    std::printf("%s\n", unmapped.empty() ? " none" : "");
    std::printf("DUART last write per register:");
    for (const auto& [reg, v] : duartLast) std::printf(" r%d=%02x", reg, v);
    std::printf("%s\n", duartLast.empty() ? " none" : "");
    std::printf("OTIS active voices: %d\n", m->board().otis().activeVoices());
    std::printf("ESP halted: %s\n", m->board().esp().halted() ? "yes" : "no");
    for (int ch = 0; ch < 2; ++ch) {
        const std::vector<uint8_t> tx = m->duart().takeTransmitted(ch);
        std::printf("DUART %c transmitted: %zu bytes\n", 'A' + ch, tx.size());
        if (!tx.empty()) printBytes(tx);
    }
    std::printf("DUART unsupported writes: %u\n", m->duart().unsupportedWrites());

    if (o.last) {
        std::printf("last %zu instructions:\n", ring.size());
        Machine& mc = *m;
        const auto rd = [&mc](uint32_t a) { return mc.peek16(a); };
        for (uint32_t pc : ring) std::printf("  %06x  %s\n", pc, ens::m68k::disassemble(rd, pc).text.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) != "boot")
            fail("usage: ks32emu boot [--roms <dir>] [--ms <n>] [--trace <devs>] [--from <cycle>] "
                 "[--count <n>] [--last <n>]");
        return boot(parseOptions(argc, argv));
    } catch (const Failure& f) {
        std::fprintf(stderr, "error: %s\n", f.msg.c_str());
        return 2;
    }
}
