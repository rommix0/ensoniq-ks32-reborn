// ks32emu: runs the emulated KS-32 (its OS ROM on the 68000 with the DUART and the OTIS/ESP board).
//   ks32emu boot [--roms <dir>] [--ms <n>] [--trace <devs>] [--from <cycle>] [--count <n>] [--last <n>]
//                [--display-only] [--press <ms>:<name>[+<name>...]]... [--analog <name>=<raw>]...
//   ks32emu render <in.mid> <out.wav> [--roms <dir>] [--tail <s>] [--press ...]... [--analog ...]...
//   ks32emu play [--midi <index>] [--list] [--roms <dir>] [--analog ...]...
// --press presses the named buttons (kButtons names, e.g. EDIT_SEQ_PRESET, BANK9) in order at
// <ms>, holds them 100 ms and releases them in reverse order; it may be repeated. In boot <ms>
// counts from power-on, in render from the end of the boot (the start of the WAV).
// render boots for kBootMs (not written), sends each MIDI file event to MIDI in at its frame and
// renders until the last event plus --tail (default 2 s); it writes a stereo WAV at kWavRate and
// prints the FNV-1a 64 hash of the PCM. play does the same live: WinMM MIDI in, WASAPI out,
// resampled to the device rate; it prints the display as it changes, and a typed line of button
// names joined by + (e.g. bank9+screen1) presses them as --press does; q quits (Windows only).
// MIDI in takes channel messages only: system messages (0xF0 and up) are not sent.
// --analog sets an analog input (PITCH_WHEEL, MOD_WHEEL, CV_PEDAL, VOLUME, DATA_ENTRY, PRESSURE,
// BATTERY) to a raw 10-bit conversion, 0..1023 (decimal), from power-on; it may be repeated.
// The panel display is printed as "<ms> |<row>|" lines each time it changes (bytes outside
// 0x20-0x7E as \xNN), the LEDs as "<ms> LEDs ..." each time they change; --display-only leaves
// out the summary.
// File I/O lives here; src/ks32 stays pure. Errors print "error: <msg>" and exit 2.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../ks32play/files.h"
#include "ks32/machine.h"
#include "m68k/m68k_dis.h"
#include "player/resampler.h"
#include "player/smf.h"
#include "player/wav.h"

#ifdef _WIN32
#include "../ks32play/audio_out_wasapi.h"
#include "../ks32play/midi_in_win.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mutex>
#include <thread>
#endif

using namespace ens::ks32;

namespace {

struct Failure {
    std::string msg;
};
[[noreturn]] void fail(const std::string& msg) { throw Failure{msg}; }

struct Options {
    std::vector<std::string> positional;  // render: <in.mid> <out.wav>
    std::string roms = "ks-32";
    uint64_t ms = 3000;
    std::string trace;  // comma list of otis,esp,duart,unmapped,iack
    uint64_t from = 0, count = UINT64_MAX;
    uint32_t last = 0;
    bool displayOnly = false;
    std::vector<std::pair<uint64_t, std::vector<Button>>> presses;  // --press: ms, buttons
    std::vector<std::pair<Analog, uint16_t>> analogs;                // --analog: input, raw
    double tail = 2.0;                                               // render --tail, seconds
    int midi = 0;                                                    // play --midi
    bool midiSet = false, list = false;                              // play: --midi given, --list
};

constexpr std::pair<Analog, const char*> kAnalogNames[] = {
    {Analog::PitchWheel, "PITCH_WHEEL"}, {Analog::ModWheel, "MOD_WHEEL"}, {Analog::CvPedal, "CV_PEDAL"},
    {Analog::Volume, "VOLUME"}, {Analog::DataEntry, "DATA_ENTRY"}, {Analog::Pressure, "PRESSURE"},
    {Analog::Battery, "BATTERY"},
};

uint64_t parseNum(const std::string& opt, const std::string& v) {
    char* end = nullptr;
    const unsigned long long n = std::strtoull(v.c_str(), &end, 10);
    if (v.empty() || *end != '\0') fail("bad value '" + v + "' for " + opt);
    return n;
}

// "<name>[+<name>...]", names as in kButtons (case-insensitive).
std::vector<Button> parseButtons(const std::string& v) {
    std::vector<Button> out;
    for (size_t q = 0;;) {
        const size_t next = v.find('+', q);
        std::string name = v.substr(q, next == std::string::npos ? std::string::npos : next - q);
        for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const ButtonName* hit = nullptr;
        for (const ButtonName& b : kButtons)
            if (name == b.name) hit = &b;
        if (!hit) fail("unknown button '" + name + "'");
        out.push_back(hit->button);
        if (next == std::string::npos) return out;
        q = next + 1;
    }
}

// "<ms>:<name>[+<name>...]"
std::pair<uint64_t, std::vector<Button>> parsePress(const std::string& v) {
    const size_t colon = v.find(':');
    if (colon == std::string::npos) fail("bad value '" + v + "' for --press (<ms>:<name>[+<name>...])");
    return {parseNum("--press", v.substr(0, colon)), parseButtons(v.substr(colon + 1))};
}

double parseSeconds(const std::string& opt, const std::string& v) {
    char* end = nullptr;
    const double s = std::strtod(v.c_str(), &end);
    if (v.empty() || *end != '\0' || !(s >= 0 && s <= 3600)) fail("bad value '" + v + "' for " + opt);
    return s;
}

// "<name>=<raw>"
std::pair<Analog, uint16_t> parseAnalog(const std::string& v) {
    const size_t eq = v.find('=');
    if (eq == std::string::npos) fail("bad value '" + v + "' for --analog (<name>=<raw>)");
    const uint64_t raw = parseNum("--analog", v.substr(eq + 1));
    if (raw > 0x3FF) fail("raw value " + v.substr(eq + 1) + " for --analog is over 1023");
    for (const auto& [a, name] : kAnalogNames)
        if (v.compare(0, eq, name) == 0) return {a, static_cast<uint16_t>(raw)};
    fail("unknown analog input '" + v.substr(0, eq) + "' for --analog");
}

Options parseOptions(int argc, char** argv) {
    Options o;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.compare(0, 2, "--") != 0) {
            o.positional.push_back(a);
            continue;
        }
        if (a == "--display-only" || a == "--list") {
            (a == "--list" ? o.list : o.displayOnly) = true;
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
        else if (a == "--analog") o.analogs.push_back(parseAnalog(v));
        else if (a == "--tail") o.tail = parseSeconds(a, v);
        else if (a == "--midi") {
            o.midi = static_cast<int>(parseNum(a, v));
            o.midiSet = true;
        } else fail("unknown option " + a);
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

// The ROMs from o.roms, with the --analog settings applied from power-on.
std::unique_ptr<Machine> makeMachine(const Options& o) {
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
    for (const auto& [a, raw] : o.analogs) m->setAnalog(a, raw);
    return m;
}

// Button events by ms: (button, down), in insertion order for equal times (multimap keeps it).
using ButtonEvents = std::multimap<uint64_t, std::pair<Button, bool>>;

// Presses `buttons` in order at `ms` and releases them in reverse order 100 ms later.
void schedule(ButtonEvents& events, uint64_t ms, const std::vector<Button>& buttons) {
    for (const Button b : buttons) events.emplace(ms, std::make_pair(b, true));
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) events.emplace(ms + 100, std::make_pair(*it, false));
}

// Applies every event due at `now` ms.
void applyDue(Machine& m, ButtonEvents& events, uint64_t now) {
    for (; !events.empty() && events.begin()->first <= now; events.erase(events.begin()))
        m.button(events.begin()->second.first, events.begin()->second.second);
}

// One channel message to MIDI in: program change (0xC0) and channel pressure (0xD0) have one data
// byte, the others two.
void sendMidi(Machine& m, uint8_t status, uint8_t d1, uint8_t d2) {
    m.midiIn(status);
    m.midiIn(d1);
    if ((status & 0xE0) != 0xC0) m.midiIn(d2);
}

void runBoot(Machine& m) {
    int16_t l, r;
    for (uint64_t f = 0; f < static_cast<uint64_t>(kBootMs) * kWavRate / 1000; ++f) m.runFrame(l, r);
    (void)m.takeMidiOut();  // what the boot sends on MIDI out is not reported
}

int boot(const Options& o) {
    std::set<Device> traced;
    if (!parseTrace(o.trace, traced)) fail("bad value '" + o.trace + "' for --trace (otis,esp,duart,unmapped,iack)");
    if (!o.positional.empty()) fail("unexpected argument '" + o.positional[0] + "'");
    auto m = makeMachine(o);

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

    ButtonEvents events;
    for (const auto& [at, buttons] : o.presses) schedule(events, at, buttons);

    const uint64_t frames = o.ms * kWavRate / 1000;
    int16_t l, r;
    std::vector<std::string> rows = m->panel().rows;
    std::string leds = ledText(m->panel());
    for (uint64_t f = 0; f < frames; ++f) {
        applyDue(*m, events, f * 1000 / kWavRate);
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

// FNV-1a 64 over the samples, each as two little-endian bytes (as ks32_firmware_render_is_deterministic).
uint64_t pcmHash(const std::vector<int16_t>& pcm) {
    uint64_t h = 14695981039346656037ull;
    for (const int16_t s : pcm)
        for (int i = 0; i < 2; ++i) {
            h ^= static_cast<uint8_t>(static_cast<uint16_t>(s) >> (8 * i));
            h *= 1099511628211ull;
        }
    return h;
}

int render(const Options& o) {
    if (o.positional.size() != 2) fail("render needs <in.mid> <out.wav>");
    std::vector<uint8_t> file;
    std::string err;
    if (!readFile(o.positional[0], file, err)) fail(err + " '" + o.positional[0] + "'");
    std::vector<ens::player::MidiEvent> midi;  // frames at kWavRate, the machine's frame rate
    if (!ens::player::parseSmf(file, midi, err)) fail(err + " '" + o.positional[0] + "'");

    auto m = makeMachine(o);
    runBoot(*m);
    ButtonEvents events;
    for (const auto& [at, buttons] : o.presses) schedule(events, at, buttons);
    const uint64_t frames =
        (midi.empty() ? 0 : midi.back().frame) + static_cast<uint64_t>(std::llround(o.tail * kWavRate));
    std::vector<int16_t> pcm;
    pcm.reserve(static_cast<size_t>(frames) * 2);
    size_t next = 0;
    int peak = 0;
    int16_t l, r;
    for (uint64_t f = 0; f < frames; ++f) {
        applyDue(*m, events, f * 1000 / kWavRate);
        for (; next < midi.size() && midi[next].frame <= f; ++next)
            sendMidi(*m, midi[next].status, midi[next].d1, midi[next].d2);
        m->runFrame(l, r);
        pcm.push_back(l);
        pcm.push_back(r);
        peak = std::max({peak, std::abs(static_cast<int>(l)), std::abs(static_cast<int>(r))});
    }
    if (!writeFile(o.positional[1], ens::player::makeWav16(pcm, 2, kWavRate), err))
        fail(err + " '" + o.positional[1] + "'");
    std::printf("%zu MIDI events, %llu frames (%.3f s at %u Hz), peak %d, MIDI out %zu bytes\n", midi.size(),
                static_cast<unsigned long long>(frames), static_cast<double>(frames) / kWavRate, kWavRate, peak,
                m->takeMidiOut().size());
    std::printf("pcm hash %016llx\n", static_cast<unsigned long long>(pcmHash(pcm)));
    return 0;
}

#ifdef _WIN32

constexpr double kChipRate = 30476180.0 / 1024.0;  // the machine's exact frame rate (Hz)

// Everything the WASAPI render thread touches. The machine lives on that thread: render() takes
// the typed presses, sends the queued MIDI, runs frames in blocks of 64 through the resampler
// until the device's request is covered, then copies the display for the console thread.
struct LiveEngine {
    Machine& m;
    MidiIn* midi = nullptr;  // nullptr: no MIDI input
    std::unique_ptr<ens::player::Resampler> rs;  // created once the device rate is known, then `ready`
    std::atomic<bool> ready{false};
    std::vector<float> fifo;  // resampled L/R not yet sent to the device
    uint64_t frame = 0;       // frames run since the end of the boot
    ButtonEvents events;
    int16_t block[64 * 2] = {};

    std::mutex mu;  // guards the members below, shared with the console thread
    std::vector<std::vector<Button>> typed;  // presses typed on the console, not yet scheduled
    PanelState panel;                         // the display as of the last refill
    uint64_t midiOut = 0;                     // bytes the firmware sent on MIDI out (not forwarded)

    explicit LiveEngine(Machine& machine) : m(machine) {}

    void render(float* out, uint32_t frames, uint32_t channels) {
        std::fill(out, out + static_cast<size_t>(frames) * channels, 0.0f);
        if (!ready.load(std::memory_order_acquire)) return;
        {
            std::lock_guard<std::mutex> lock(mu);
            for (const std::vector<Button>& b : typed) schedule(events, frame * 1000 / kWavRate, b);
            typed.clear();
        }
        uint32_t msg = 0;
        if (midi)
            while (midi->poll(msg))
                sendMidi(m, static_cast<uint8_t>(msg), static_cast<uint8_t>(msg >> 8), static_cast<uint8_t>(msg >> 16));
        while (fifo.size() / 2 < frames) {
            for (int i = 0; i < 64; ++i, ++frame) {
                applyDue(m, events, frame * 1000 / kWavRate);
                m.runFrame(block[2 * i], block[2 * i + 1]);
            }
            rs->process(block, 64, fifo);
        }
        for (uint32_t f = 0; f < frames; ++f) {
            out[static_cast<size_t>(f) * channels] = fifo[2 * static_cast<size_t>(f)];
            out[static_cast<size_t>(f) * channels + 1] = fifo[2 * static_cast<size_t>(f) + 1];
        }
        fifo.erase(fifo.begin(), fifo.begin() + 2 * static_cast<std::ptrdiff_t>(frames));  // keeps capacity
        const size_t sent = m.takeMidiOut().size();
        std::lock_guard<std::mutex> lock(mu);
        panel = m.panel();
        midiOut += sent;
    }
};

std::atomic<bool> g_interrupted{false};

BOOL WINAPI onConsoleCtrl(DWORD type) {
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) return FALSE;
    g_interrupted = true;  // quit cleanly from the main loop
    return TRUE;
}

// Lines typed on stdin, read by a detached thread with ReadFile (not the CRT, whose stream lock
// could stall exit; ks32play does the same). End of input is not a quit.
class StdinLines {
public:
    StdinLines() {
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        if (!h || h == INVALID_HANDLE_VALUE) return;
        std::shared_ptr<Shared> sh = sh_;
        std::thread([sh, h] {  // detached: process exit ends it if it is still blocked
            char c = 0;
            DWORD n = 0;
            std::string line;
            while (ReadFile(h, &c, 1, &n, nullptr) && n == 1) {
                if (c == '\r') continue;
                if (c != '\n') {
                    line += c;
                    continue;
                }
                std::lock_guard<std::mutex> lock(sh->mu);
                sh->lines.push_back(std::move(line));
                line.clear();
            }
        }).detach();
    }
    std::vector<std::string> take() {
        std::lock_guard<std::mutex> lock(sh_->mu);
        std::vector<std::string> out;
        out.swap(sh_->lines);
        return out;
    }

private:
    struct Shared {
        std::mutex mu;
        std::vector<std::string> lines;
    };
    std::shared_ptr<Shared> sh_ = std::make_shared<Shared>();
};

int listDevices() {
    const std::vector<std::string> names = MidiIn::devices();
    if (names.empty()) std::printf("no MIDI input devices\n");
    else std::printf("MIDI inputs:\n");
    for (size_t i = 0; i < names.size(); ++i) std::printf("  %zu: %s\n", i, names[i].c_str());
    std::string desc, err;
    if (AudioOut::describeDefault(desc, err)) std::printf("audio output (default device): %s\n", desc.c_str());
    else std::printf("audio output: %s\n", err.c_str());
    return 0;
}

void printPanel(const PanelState& p) {
    for (const std::string& row : p.rows) std::printf("|%s|\n", printable(row).c_str());
    std::printf("%s\n", ledText(p).c_str());
    std::fflush(stdout);
}

int play(const Options& o) {
    if (!o.positional.empty()) fail("play takes no positional arguments");
    if (!o.presses.empty()) fail("play takes typed presses, not --press");
    if (o.list) return listDevices();

    // Pick the MIDI input before booting, so a bad index fails fast. Without devices it runs
    // with the typed buttons only.
    const std::vector<std::string> names = MidiIn::devices();
    if (o.midiSet && o.midi >= static_cast<int>(names.size()))
        fail("MIDI input " + std::to_string(o.midi) + " not found (" + std::to_string(names.size()) + " devices)");

    auto m = makeMachine(o);
    std::printf("booting (%u ms emulated)...\n", kBootMs);
    std::fflush(stdout);
    runBoot(*m);
    LiveEngine engine(*m);
    engine.panel = m->panel();
    MidiIn midi;

    AudioOut audio;
    uint32_t rate = 0, channels = 0;
    std::string err;
    if (!audio.start([&engine](float* out, uint32_t frames, uint32_t ch) { engine.render(out, frames, ch); }, rate,
                     channels, err))
        fail(err);
    // The callback outputs silence until `ready`; from here on only the audio thread runs the machine.
    engine.rs = std::make_unique<ens::player::Resampler>(kChipRate, static_cast<double>(rate), 2);
    engine.fifo.reserve((static_cast<size_t>(audio.bufferFrames()) + 4096) * 2);  // refills never reallocate
    if (!names.empty()) {
        if (!midi.open(o.midi, err)) {
            audio.stop();
            fail(err);
        }
        engine.midi = &midi;
    }
    engine.ready.store(true, std::memory_order_release);

    std::printf("audio: default device, %s, buffer %u frames (%.1f ms)\n", audio.formatName().c_str(),
                audio.bufferFrames(), audio.bufferFrames() * 1000.0 / rate);
    if (names.empty()) std::printf("midi: none (no MIDI input devices)\n");
    else std::printf("midi: %d: %s\n", o.midi, names[static_cast<size_t>(o.midi)].c_str());
    std::printf("Type button names joined by + (e.g. bank9+screen1) and Enter to press them; q to quit.\n");
    PanelState shown = engine.panel;
    printPanel(shown);

    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
    StdinLines input;
    bool quit = false;
    while (!quit && !audio.failed() && !g_interrupted.load()) {
        Sleep(50);
        for (std::string line : input.take()) {
            if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);  // a UTF-8 BOM (PowerShell pipes)
            line.erase(std::remove_if(line.begin(), line.end(), [](char c) { return c == ' ' || c == '\t'; }),
                       line.end());
            if (line == "q" || line == "Q") {
                quit = true;
                break;
            }
            if (line.empty()) continue;
            try {
                std::vector<Button> b = parseButtons(line);
                std::lock_guard<std::mutex> lock(engine.mu);
                engine.typed.push_back(std::move(b));
            } catch (const Failure& f) {
                std::printf("%s\n", f.msg.c_str());
            }
        }
        PanelState now;
        {
            std::lock_guard<std::mutex> lock(engine.mu);
            now = engine.panel;
        }
        if (now.rows != shown.rows || now.leds != shown.leds) printPanel(shown = now);
    }

    audio.stop();
    midi.close();
    SetConsoleCtrlHandler(onConsoleCtrl, FALSE);
    const AudioOut::Stats st = audio.stats();
    std::printf("audio: %llu frames (%.2f s), %llu underruns, peak %.3f, %llu clipped, slowest refill %.2f ms; "
                "midi in %llu messages, %llu dropped; midi out %llu bytes\n",
                static_cast<unsigned long long>(st.frames), static_cast<double>(st.frames) / rate,
                static_cast<unsigned long long>(st.underruns), static_cast<double>(st.peak),
                static_cast<unsigned long long>(st.clipped), st.maxRenderMs,
                static_cast<unsigned long long>(midi.received()), static_cast<unsigned long long>(midi.dropped()),
                static_cast<unsigned long long>(engine.midiOut));
    if (audio.failed()) fail(audio.error());
    return 0;
}

#else

int play(const Options&) { fail("play needs Windows (WinMM and WASAPI)"); }

#endif

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::string cmd = argc < 2 ? "" : argv[1];
        if (cmd != "boot" && cmd != "render" && cmd != "play")
            fail("usage:\n  ks32emu boot [--roms <dir>] [--ms <n>] [--trace <devs>] [--from <cycle>] "
                 "[--count <n>] [--last <n>] [--display-only] [--press <ms>:<name>[+<name>...]]... "
                 "[--analog <name>=<raw>]...\n"
                 "  ks32emu render <in.mid> <out.wav> [--roms <dir>] [--tail <s>] [--press ...]... "
                 "[--analog ...]...\n"
                 "  ks32emu play [--midi <index>] [--list] [--roms <dir>] [--analog ...]...");
        const Options o = parseOptions(argc, argv);
        if (cmd == "render") return render(o);
        if (cmd == "play") return play(o);
        return boot(o);
    } catch (const Failure& f) {
        std::fprintf(stderr, "error: %s\n", f.msg.c_str());
        return 2;
    }
}
