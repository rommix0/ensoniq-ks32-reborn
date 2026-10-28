// ks32emu: runs the emulated KS-32 (its OS ROM on the 68000 with the DUART and the OTIS/ESP board).
//   ks32emu boot [--roms <dir>] [--ms <n>] [--trace <devs>] [--from <cycle>] [--count <n>] [--last <n>]
//                [--display-only] [--press <ms>:<name>[+<name>...]]...
// --press presses the named buttons (kButtons names, e.g. EDIT_SEQ_PRESET, BANK9) in order at
// <ms>, holds them 100 ms and releases them in reverse order; it may be repeated.
// The panel display is printed as "<ms> |<row>|" lines each time it changes (bytes outside
// 0x20-0x7E as \xNN), the LEDs as "<ms> LEDs ..." each time they change; --display-only leaves
// out the summary.
// File I/O lives here; src/ks32 stays pure. Errors print "error: <msg>" and exit 2.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <utility>
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
    bool displayOnly = false;
    std::vector<std::pair<uint64_t, std::vector<Button>>> presses;  // --press: ms, buttons
};

uint64_t parseNum(const std::string& opt, const std::string& v) {
    char* end = nullptr;
    const unsigned long long n = std::strtoull(v.c_str(), &end, 10);
    if (v.empty() || *end != '\0') fail("bad value '" + v + "' for " + opt);
    return n;
}

// "<ms>:<name>[+<name>...]"
std::pair<uint64_t, std::vector<Button>> parsePress(const std::string& v) {
    const size_t colon = v.find(':');
    if (colon == std::string::npos) fail("bad value '" + v + "' for --press (<ms>:<name>[+<name>...])");
    std::pair<uint64_t, std::vector<Button>> p{parseNum("--press", v.substr(0, colon)), {}};
    for (size_t q = colon; q != std::string::npos;) {
        const size_t next = v.find('+', q + 1);
        const std::string name = v.substr(q + 1, next == std::string::npos ? std::string::npos : next - q - 1);
        const ButtonName* hit = nullptr;
        for (const ButtonName& b : kButtons)
            if (name == b.name) hit = &b;
        if (!hit) fail("unknown button '" + name + "' for --press");
        p.second.push_back(hit->button);
        q = next;
    }
    return p;
}

Options parseOptions(int argc, char** argv) {
    Options o;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.compare(0, 2, "--") != 0) fail("unexpected argument '" + a + "'");
        if (a == "--display-only") {
            o.displayOnly = true;
            continue;
        }
        if (i + 1 >= argc) fail("option " + a + " needs a value");
        const std::string v = argv[++i];
        if (a == "--roms") o.roms = v;
        else if (a == "--ms") o.ms = parseNum(a, v);
        else if (a == "--trace") o.trace = v;
        else if (a == "--from") o.from = parseNum(a, v);
        else if (a == "--count") o.count = parseNum(a, v);
        else if (a == "--last") o.last = static_cast<uint32_t>(parseNum(a, v));
        else if (a == "--press") o.presses.push_back(parsePress(v));
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

// A display row with every byte outside 0x20-0x7E as \xNN.
std::string printable(const std::string& row) {
    std::string s;
    for (const char ch : row) {
        const auto c = static_cast<unsigned char>(ch);
        if (c >= 0x20 && c < 0x7F) {
            s += ch;
            continue;
        }
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\x%02X", c);
        s += buf;
    }
    return s;
}

// "LEDs 8 10*": the lit LED numbers, * = blinking; "LEDs off" when none is lit.
std::string ledText(const PanelState& p) {
    std::string s = "LEDs";
    for (int i = 0; i < kPanelLeds; ++i)
        if (p.leds[i] != Led::Off) s += " " + std::to_string(i) + (p.leds[i] == Led::Blink ? "*" : "");
    return s == "LEDs" ? "LEDs off" : s;
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
        // Only writes the DUART received: odd bytes and words (an even-byte write is ignored, K1).
        if (e.dev == Device::Duart && e.write && ((e.addr & 1) || e.size == 2))
            duartLast[(e.addr >> 1) & 0xF] = static_cast<uint8_t>(e.value);
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

    // ms -> (button, down), in option order for equal times (multimap keeps insertion order).
    std::multimap<uint64_t, std::pair<Button, bool>> events;
    for (const auto& [at, buttons] : o.presses) {
        for (const Button b : buttons) events.emplace(at, std::make_pair(b, true));
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) events.emplace(at + 100, std::make_pair(*it, false));
    }

    const uint64_t frames = o.ms * kWavRate / 1000;
    int16_t l, r;
    std::vector<std::string> rows = m->panel().rows;
    std::string leds = ledText(m->panel());
    for (uint64_t f = 0; f < frames; ++f) {
        for (const uint64_t now = f * 1000 / kWavRate; !events.empty() && events.begin()->first <= now;) {
            m->button(events.begin()->second.first, events.begin()->second.second);
            events.erase(events.begin());
        }
        m->runFrame(l, r);
        const PanelState& p = m->panel();
        const auto ms = static_cast<unsigned long long>((f + 1) * 1000 / kWavRate);
        if (p.rows != rows) {
            rows = p.rows;
            for (const std::string& row : rows) std::printf("%llu |%s|\n", ms, printable(row).c_str());
        }
        if (ledText(p) != leds) {
            leds = ledText(p);
            std::printf("%llu %s\n", ms, leds.c_str());
        }
    }
    if (o.displayOnly) return 0;

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
    const std::vector<uint8_t> tx = m->duart().takeTransmitted(0);  // channel B's bytes went to the panel
    std::printf("DUART A transmitted: %zu bytes\n", tx.size());
    if (!tx.empty()) printBytes(tx);
    std::printf("panel: %s, unknown bytes %u\n", ledText(m->panel()).c_str(), m->panel().unknown);
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
                 "[--count <n>] [--last <n>] [--display-only] [--press <ms>:<name>[+<name>...]]...");
        return boot(parseOptions(argc, argv));
    } catch (const Failure& f) {
        std::fprintf(stderr, "error: %s\n", f.msg.c_str());
        return 2;
    }
}
