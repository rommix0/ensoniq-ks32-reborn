// ks32play: MIDI test player for the emulated KS-32 sound chips (OTIS + ESP).
//   ks32play render <in.mid> <out.wav> [--resampled <out2.wav>] [--rate <hz>] [--tail <s>] [common]
//   ks32play waves [--set internal|rom3]
//   ks32play waves --audition <set>:<n> <out.wav> [common]
//   ks32play play [--midi <index>] [--list] [--demo] [common]
// The only layer with file I/O and OS calls. Exit 0 on success, 2 on any error (one "error: ..."
// line on stderr). parseOptions() -> loadAssets() -> makeSynth(), then one cmd* function per command.
// Live play (WinMM MIDI in, WASAPI out) is Windows-only: midi_in_win.*, audio_out_wasapi.*.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "files.h"
#include "ks32/board.h"
#include "ks32/wave_roms.h"
#include "player/effects.h"
#include "player/resampler.h"
#include "player/sample_map.h"
#include "player/smf.h"
#include "player/synth.h"
#include "player/voices.h"
#include "player/wav.h"

#ifdef _WIN32
#include "audio_out_wasapi.h"
#include "midi_in_win.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mutex>
#endif

using namespace ens::player;

namespace {

constexpr double kChipRate = 30476180.0 / 1024.0;  // exact chip frame rate (Hz)

// A command-line error: reported as "error: <msg>" (then the usage text if `usage`), exit code 2.
struct Failure {
    std::string msg;
    bool usage = false;
};

[[noreturn]] void fail(const std::string& msg) { throw Failure{msg, false}; }
[[noreturn]] void failUsage(const std::string& msg) { throw Failure{msg, true}; }

struct Options {
    std::vector<std::string> positional;
    std::string roms = "ks-32";
    std::string data = "docs/rom-investigation/data";
    int effect = 1;
    int bus = 1;
    int waveSet = 0, wave = 0;
    Adsr adsr;
    std::string resampled;
    int rate = 48000;
    double tail = 4.0;
    std::string setName;    // waves --set
    bool audition = false;  // waves --audition
    int auditionSet = 0, auditionWave = 0;
    int midi = 0;          // play --midi
    bool midiSet = false;
    bool list = false;     // play --list
    bool demo = false;     // play --demo
};

bool parseInt(const std::string& s, int lo, int hi, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (*end != '\0' || v < lo || v > hi) return false;
    out = static_cast<int>(v);
    return true;
}

bool parseDouble(const std::string& s, double lo, double hi, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (*end != '\0' || !(v >= lo && v <= hi)) return false;
    out = v;
    return true;
}

// "S:N" with S 0 (internal) or 1 (rom3).
bool parseWaveId(const std::string& s, int& set, int& num) {
    const size_t c = s.find(':');
    if (c == std::string::npos) return false;
    return parseInt(s.substr(0, c), 0, 1, set) && parseInt(s.substr(c + 1), 0, 999, num);
}

// "a,d,s,r": attack ms, decay ms, sustain percent, release ms.
bool parseAdsr(const std::string& s, Adsr& out) {
    int f[4];
    size_t pos = 0;
    for (int k = 0; k < 4; ++k) {
        const size_t c = s.find(',', pos);
        if ((k < 3) == (c == std::string::npos)) return false;
        const std::string part = s.substr(pos, c == std::string::npos ? std::string::npos : c - pos);
        if (!parseInt(part, 0, k == 2 ? 100 : 60000, f[k])) return false;
        pos = c + 1;
    }
    out = Adsr{f[0], f[1], f[2], f[3]};
    return true;
}

// Everything after the command word. Flags may appear anywhere; the rest are positional.
Options parseOptions(int argc, char** argv, int first) {
    Options o;
    for (int i = first; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.size() < 3 || a.compare(0, 2, "--") != 0) {
            o.positional.push_back(a);
            continue;
        }
        if (a == "--list" || a == "--demo") {  // the only options without a value
            if (a == "--list") o.list = true;
            else o.demo = true;
            continue;
        }
        if (i + 1 >= argc) fail("option " + a + " needs a value");
        const std::string v = argv[++i];
        bool ok = true;
        if (a == "--roms") o.roms = v;
        else if (a == "--data") o.data = v;
        else if (a == "--effect") ok = parseInt(v, 0, 10, o.effect);
        else if (a == "--bus") ok = parseInt(v, 0, 2, o.bus);
        else if (a == "--wave") ok = parseWaveId(v, o.waveSet, o.wave);
        else if (a == "--adsr") ok = parseAdsr(v, o.adsr);
        else if (a == "--resampled") o.resampled = v;
        else if (a == "--rate") ok = parseInt(v, 8000, 384000, o.rate);
        else if (a == "--tail") ok = parseDouble(v, 0.0, 600.0, o.tail);
        else if (a == "--midi") {
            ok = parseInt(v, 0, 9999, o.midi);
            o.midiSet = true;
        }
        else if (a == "--set") {
            ok = (v == "internal" || v == "rom3");
            o.setName = v;
        } else if (a == "--audition") {
            ok = parseWaveId(v, o.auditionSet, o.auditionWave);
            o.audition = true;
        } else {
            fail("unknown option " + a);
        }
        if (!ok) fail("bad value '" + v + "' for " + a);
    }
    return o;
}

// ---- assets -----------------------------------------------------------------------------------

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    const char last = dir.back();
    return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

std::vector<uint8_t> readOrFail(const std::string& path, const char* what) {
    std::vector<uint8_t> bytes;
    std::string err;
    if (!readFile(path, bytes, err)) fail(err + " " + what + " '" + path + "'");
    return bytes;
}

std::string readText(const std::string& path) {
    const std::vector<uint8_t> b = readOrFail(path, "data file");
    return std::string(b.begin(), b.end());
}

// What a Synth is built from. Task 10's `play` creates one per run.
struct Assets {
    std::vector<uint16_t> bank0, bank1;
    SampleMap map;
    EffectLibrary fx;
    std::vector<uint16_t> fcTable;
};

SampleMap loadSampleMap(const Options& o) {
    const std::string path = joinPath(o.data, "sample_map.json");
    SampleMap map;
    std::string err;
    if (!parseSampleMap(readText(path), map, err)) fail("'" + path + "': " + err);
    return map;
}

Assets loadAssets(const Options& o) {
    Assets a;
    const std::vector<uint8_t> u25 = readOrFail(joinPath(o.roms, "sq1-u25.bin"), "ROM");
    const std::vector<uint8_t> u26 = readOrFail(joinPath(o.roms, "sq1-u26.bin"), "ROM");
    const std::vector<uint8_t> r2 = readOrFail(joinPath(o.roms, "rom2.u39"), "ROM");
    const std::vector<uint8_t> r3 = readOrFail(joinPath(o.roms, "rom3.u38"), "ROM");
    std::string err;
    if (!ens::ks32::buildWaveBanks(u25, u26, r2, r3, a.bank0, a.bank1, err)) fail(err);

    a.map = loadSampleMap(o);
    const std::string progs = joinPath(joinPath(o.data, "esp"), "esp_programs.json");
    const std::string defs = joinPath(joinPath(o.data, "esp"), "effect_defaults.json");
    if (!parseEffects(readText(progs), readText(defs), a.fx, err)) fail("'" + progs + "': " + err);
    a.fcTable = makeFcTable();  // the firmware table was verified identical to the formula (Task 2)
    return a;
}

std::unique_ptr<Synth> makeSynth(const Assets& a, const Options& o) {
    SynthConfig cfg;
    cfg.voice.bus = o.bus;
    cfg.voice.adsr = o.adsr;
    cfg.effect = o.effect;
    cfg.waveSet = o.waveSet;
    cfg.wave = o.wave;
    return std::make_unique<Synth>(a.bank0, a.bank1, a.map, a.fx, a.fcTable, cfg);
}

// ---- offline rendering ------------------------------------------------------------------------

uint64_t secondsToFrames(double s) { return static_cast<uint64_t>(std::llround(s * kChipRate)); }

// Applies every event at its frame and renders `tailFrames` after the last one. Events must be
// sorted by frame. Stereo interleaved int16 at the chip rate. Appends the synth log to `log`.
std::vector<int16_t> renderEvents(Synth& synth, const std::vector<MidiEvent>& events, uint64_t tailFrames,
                                  std::vector<std::string>& log) {
    const uint64_t last = events.empty() ? 0 : events.back().frame;
    const uint64_t total = last + tailFrames;
    std::vector<int16_t> pcm(static_cast<size_t>(total) * 2);
    uint64_t cur = 0;
    auto renderTo = [&](uint64_t target) {
        if (target > cur) {
            synth.render(pcm.data() + cur * 2, static_cast<int>(target - cur));
            cur = target;
        }
    };
    for (const MidiEvent& e : events) {
        renderTo(e.frame);
        synth.midi(e.status, e.d1, e.d2);
    }
    renderTo(total);
    for (std::string& s : synth.takeLog()) log.push_back(std::move(s));
    return pcm;
}

// Chip-rate pcm -> `rate` Hz, aligned with the input (filter delay removed, tail flushed with zeros).
std::vector<int16_t> resample(const std::vector<int16_t>& pcm, int rate) {
    Resampler rs(kChipRate, rate, 2);
    const size_t frames = pcm.size() / 2;
    std::vector<float> out;
    rs.process(pcm.data(), frames, out);
    const size_t pad = static_cast<size_t>(std::ceil(rs.latencyFrames() * kChipRate / rate)) + 64;
    const std::vector<int16_t> zeros(pad * 2, 0);
    rs.process(zeros.data(), pad, out);

    const size_t skip = static_cast<size_t>(std::llround(rs.latencyFrames()));
    const size_t want = static_cast<size_t>(std::llround(static_cast<double>(frames) * rate / kChipRate));
    const size_t have = out.size() / 2 > skip ? out.size() / 2 - skip : 0;
    const size_t n = std::min(want, have);
    std::vector<int16_t> res(n * 2);
    for (size_t i = 0; i < n * 2; ++i) {
        const double v = std::nearbyint(static_cast<double>(out[skip * 2 + i]) * 32768.0);
        res[i] = static_cast<int16_t>(std::clamp(v, -32768.0, 32767.0));
    }
    return res;
}

void writeWav(const std::string& path, const std::vector<int16_t>& pcm, uint32_t rate) {
    std::string err;
    if (!writeFile(path, makeWav16(pcm, 2, rate), err)) fail(err + " '" + path + "'");
}

// The one-line summary of the chip-rate render (frames, duration, peak, clips), then the Synth log.
void printSummary(const std::vector<int16_t>& pcm, const std::vector<std::string>& log) {
    int peak = 0;
    size_t clips = 0;
    for (int16_t s : pcm) {
        peak = std::max(peak, std::abs(static_cast<int>(s)));
        if (s == 32767 || s == -32768) ++clips;
    }
    const size_t frames = pcm.size() / 2;
    std::printf("frames %zu  duration %.3f s  peak %d  clips %zu\n", frames, frames / kChipRate, peak, clips);
    for (const std::string& l : log) std::printf("  %s\n", l.c_str());
}

// Shared tail of `render` and `waves --audition`: build the synth, render, write the WAV(s).
int renderAndWrite(const Options& o, const std::vector<MidiEvent>& events, uint64_t tailFrames,
                   const std::string& outWav, std::vector<std::string> log = {}) {
    const Assets assets = loadAssets(o);
    const std::unique_ptr<Synth> synth = makeSynth(assets, o);
    const std::vector<int16_t> pcm = renderEvents(*synth, events, tailFrames, log);
    writeWav(outWav, pcm, ens::ks32::kWavRate);
    if (!o.resampled.empty()) writeWav(o.resampled, resample(pcm, o.rate), static_cast<uint32_t>(o.rate));
    printSummary(pcm, log);
    return 0;
}

// ---- commands ---------------------------------------------------------------------------------

int cmdRender(const Options& o) {
    if (o.positional.size() != 2) failUsage("render needs <in.mid> <out.wav>");
    const std::string& in = o.positional[0];
    const std::vector<uint8_t> file = readOrFail(in, "MIDI file");
    std::vector<MidiEvent> events;
    std::string err;
    if (!parseSmf(file, events, err)) fail("'" + in + "': " + err);
    // A hostile or broken SMF can put events days apart; the whole render is held in memory.
    const uint64_t cap = secondsToFrames(4 * 3600.0);
    const uint64_t last = events.empty() ? 0 : events.back().frame;
    const uint64_t tail = secondsToFrames(o.tail);
    if (last > cap || last + tail > cap) {
        char len[32];
        std::snprintf(len, sizeof len, "%.0f", static_cast<double>(last + tail) / kChipRate);
        fail("'" + in + "': render length " + len + " s is over the 4 h limit");
    }
    return renderAndWrite(o, events, tail, o.positional[1]);
}

// Root key of a zone: pitch word = -256 * root key (findings: root key = -w / 256).
int rootKeyOf(const Zone& z) {
    return std::clamp(static_cast<int>(std::lround(-z.pitchWord / 256.0)), 0, 127);
}

const char* loopMode(const Zone& z) {
    switch (z.crLow & 0x18) {  // CR low byte 0x03 one-shot, 0x0B forward loop, 0x1B bidirectional
    case 0x00: return "one-shot";
    case 0x08: return "loop";
    case 0x18: return "bidir";
    default: return "?";
    }
}

int listWaves(const Options& o) {
    const SampleMap map = loadSampleMap(o);
    int count[2] = {0, 0};
    for (const Wave& w : map.waves) {
        if ((o.setName == "internal" && w.set != 0) || (o.setName == "rom3" && w.set != 1)) continue;
        ++count[w.set & 1];
        std::printf("%d:%d  %s  %zu zone%s", w.set, w.number, w.name.empty() ? "-" : w.name.c_str(),
                    w.zones.size(), w.zones.size() == 1 ? "" : "s");
        int lo = 0;
        for (const Zone& z : w.zones) {
            std::printf("  [%d-%d root %d %s]", lo, z.topKey, rootKeyOf(z), loopMode(z));
            lo = z.topKey + 1;
        }
        if (!w.note.empty()) std::printf("  note: %s", w.note.c_str());
        std::printf("\n");
    }
    if (o.setName != "rom3") std::printf("internal: %d waves\n", count[0]);
    if (o.setName != "internal") std::printf("rom3: %d waves\n", count[1]);
    return 0;
}

// Each zone's rounded root key for 1 s, with 0.5 s gaps, after a 0.25 s lead-in. The ESP boot and
// the effect load (with its fade-in) take about 2100 chip frames, 0.07 s.
int audition(const Options& o) {
    if (o.positional.size() != 1) failUsage("--audition needs <out.wav>");
    Options opt = o;
    opt.waveSet = o.auditionSet;
    opt.wave = o.auditionWave;
    const SampleMap map = loadSampleMap(opt);
    const Wave* w = map.find(opt.waveSet, opt.wave);
    if (!w) fail("wave " + std::to_string(opt.waveSet) + ":" + std::to_string(opt.wave) + " has no zones");

    std::vector<MidiEvent> events;
    std::vector<std::string> notes;
    uint64_t t = secondsToFrames(0.25);
    for (const Zone& z : w->zones) {
        const uint8_t key = static_cast<uint8_t>(rootKeyOf(z));
        if (&zoneForKey(*w, key) != &z)  // the root lies outside the zone's own key range
            notes.push_back("zone up to key " + std::to_string(z.topKey) + ": root key " +
                            std::to_string(key) + " is played by another zone");
        events.push_back({t, 0x90, key, 100});
        events.push_back({t + secondsToFrames(1.0), 0x80, key, 0});
        t += secondsToFrames(1.5);
    }
    return renderAndWrite(opt, events, secondsToFrames(std::min(o.tail, 1.0)),
                          opt.positional[0], notes);
}

int cmdWaves(const Options& o) {
    if (o.audition) return audition(o);
    if (!o.positional.empty()) failUsage("waves takes no positional arguments (did you mean --audition?)");
    return listWaves(o);
}

// ---- live play (Windows) ------------------------------------------------------------------------

#ifdef _WIN32

// Synth log lines from the audio thread, printed by the main thread.
class LogQueue {
public:
    void push(std::vector<std::string>& lines) {
        std::lock_guard<std::mutex> lock(mu_);
        for (std::string& l : lines) lines_.push_back(std::move(l));
    }
    std::vector<std::string> take() {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<std::string> out;
        out.swap(lines_);
        return out;
    }

private:
    std::mutex mu_;
    std::vector<std::string> lines_;
};

// --demo: C E G C' G E ... at 8 notes/s for 4 s after a 0.1 s lead-in (past the ESP boot and the
// first effect load), each note held 0.1 s; the run ends 1 s after the last note off.
std::vector<MidiEvent> demoEvents() {
    static const uint8_t kNotes[] = {60, 64, 67, 72, 67, 64};
    std::vector<MidiEvent> ev;
    for (int k = 0; k < 32; ++k) {
        const uint64_t t = secondsToFrames(0.1 + k * 0.125);
        const uint8_t key = kNotes[k % 6];
        ev.push_back({t, 0x90, key, 100});
        ev.push_back({t + secondsToFrames(0.1), 0x80, key, 0});
    }
    return ev;
}

// Everything the audio thread touches. render() runs on the WASAPI thread: it applies the queued
// MIDI (or the demo events due), renders chip frames in blocks of up to 64 through the resampler
// until the device's request is covered, and copies the synth's L/R to the first two channels.
struct LiveEngine {
    Synth& synth;
    LogQueue& log;
    MidiIn* midi = nullptr;                        // live mode
    const std::vector<MidiEvent>* demo = nullptr;  // demo mode

    std::unique_ptr<Resampler> rs;  // created once the device rate is known, then `ready` is set
    std::atomic<bool> ready{false};
    std::atomic<bool> finished{false};  // demo: the sequence and its tail are rendered
    std::vector<float> fifo;            // resampled L/R not yet sent to the device
    uint64_t chipFrame = 0;
    uint64_t demoEnd = 0;
    size_t demoNext = 0;
    int16_t block[64 * 2] = {};

    LiveEngine(Synth& s, LogQueue& l) : synth(s), log(l) {}

    void render(float* out, uint32_t frames, uint32_t channels) {
        std::fill(out, out + static_cast<size_t>(frames) * channels, 0.0f);
        if (!ready.load(std::memory_order_acquire)) return;
        uint32_t m = 0;
        if (midi)
            while (midi->poll(m))
                synth.midi(static_cast<uint8_t>(m), static_cast<uint8_t>(m >> 8), static_cast<uint8_t>(m >> 16));
        while (fifo.size() / 2 < frames) {
            int n = 64;
            if (demo) {
                for (; demoNext < demo->size() && (*demo)[demoNext].frame <= chipFrame; ++demoNext) {
                    const MidiEvent& e = (*demo)[demoNext];
                    synth.midi(e.status, e.d1, e.d2);
                }
                if (demoNext < demo->size())
                    n = static_cast<int>(std::min<uint64_t>(64, (*demo)[demoNext].frame - chipFrame));
            }
            synth.render(block, n);
            rs->process(block, static_cast<size_t>(n), fifo);
            chipFrame += static_cast<uint64_t>(n);
        }
        for (uint32_t f = 0; f < frames; ++f) {
            out[static_cast<size_t>(f) * channels] = fifo[2 * static_cast<size_t>(f)];
            out[static_cast<size_t>(f) * channels + 1] = fifo[2 * static_cast<size_t>(f) + 1];
        }
        fifo.erase(fifo.begin(), fifo.begin() + 2 * static_cast<std::ptrdiff_t>(frames));  // keeps capacity
        if (demo && chipFrame >= demoEnd) finished.store(true);
        std::vector<std::string> lines = synth.takeLog();
        if (!lines.empty()) log.push(lines);
    }
};

std::atomic<bool> g_interrupted{false};

BOOL WINAPI onConsoleCtrl(DWORD type) {
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) return FALSE;
    g_interrupted = true;  // quit cleanly from the main loop
    return TRUE;
}

// Waits for Enter. On a console it reads key events; otherwise (stdin redirected) a background
// thread reads raw bytes with ReadFile (not the CRT, whose stream lock could stall exit) and flags
// the first newline. End of input is not a quit, so `play --demo < nul` runs to its end.
class EnterWatcher {
public:
    EnterWatcher() {
        in_ = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        const bool valid = in_ && in_ != INVALID_HANDLE_VALUE;
        console_ = valid && GetConsoleMode(in_, &mode);
        if (valid && !console_) {
            std::shared_ptr<std::atomic<bool>> seen = seen_;
            HANDLE h = in_;
            std::thread([seen, h] {  // detached: process exit ends it if it is still blocked
                char c = 0;
                DWORD n = 0;
                while (ReadFile(h, &c, 1, &n, nullptr) && n == 1)
                    if (c == '\n' || c == '\r') {
                        seen->store(true);
                        return;
                    }
            }).detach();
        }
    }
    // True once Enter was pressed; otherwise waits up to `ms` milliseconds.
    bool pressed(DWORD ms) {
        if (!console_) {
            Sleep(ms);
            return seen_->load();
        }
        if (WaitForSingleObject(in_, ms) != WAIT_OBJECT_0) return false;
        INPUT_RECORD rec[16];
        DWORD n = 0;
        if (!ReadConsoleInputW(in_, rec, 16, &n)) return false;
        for (DWORD i = 0; i < n; ++i)
            if (rec[i].EventType == KEY_EVENT && rec[i].Event.KeyEvent.bKeyDown &&
                rec[i].Event.KeyEvent.wVirtualKeyCode == VK_RETURN)
                return true;
        return false;
    }

private:
    HANDLE in_ = nullptr;
    bool console_ = false;
    std::shared_ptr<std::atomic<bool>> seen_ = std::make_shared<std::atomic<bool>>(false);
};

int listDevices() {
    const std::vector<std::string> names = MidiIn::devices();
    if (names.empty()) {
        std::printf("no MIDI input devices\n");
    } else {
        std::printf("MIDI inputs:\n");
        for (size_t i = 0; i < names.size(); ++i) std::printf("  %zu: %s\n", i, names[i].c_str());
    }
    std::string desc, err;
    if (AudioOut::describeDefault(desc, err))
        std::printf("audio output (default device): %s\n", desc.c_str());
    else
        std::printf("audio output: %s\n", err.c_str());
    return 0;
}

void printLog(LogQueue& log) {
    for (const std::string& l : log.take()) std::printf("  %s\n", l.c_str());
    std::fflush(stdout);
}

int cmdPlay(const Options& o) {
    if (!o.positional.empty()) failUsage("play takes no positional arguments");
    if (o.list) return listDevices();

    // Pick the MIDI input before loading anything, so a bad index fails fast.
    std::string midiName;
    if (!o.demo) {
        const std::vector<std::string> names = MidiIn::devices();
        if (o.midiSet && o.midi >= static_cast<int>(names.size()))
            fail("MIDI input " + std::to_string(o.midi) + " not found (" + std::to_string(names.size()) +
                 " devices)");
        if (names.empty()) fail("no MIDI input devices (use --demo to test audio)");
        midiName = std::to_string(o.midi) + ": " + names[static_cast<size_t>(o.midi)];
    }

    const Assets assets = loadAssets(o);
    const std::unique_ptr<Synth> synth = makeSynth(assets, o);
    const std::vector<std::string> first = synth->takeLog();  // the constructor's lines
    LogQueue log;
    LiveEngine engine(*synth, log);
    const std::vector<MidiEvent> demo = demoEvents();
    MidiIn midi;
    if (o.demo) {
        engine.demo = &demo;
        engine.demoEnd = demo.back().frame + secondsToFrames(1.0);
    } else {
        engine.midi = &midi;
    }

    AudioOut audio;
    uint32_t rate = 0, channels = 0;
    std::string err;
    if (!audio.start([&engine](float* out, uint32_t frames, uint32_t ch) { engine.render(out, frames, ch); }, rate,
                     channels, err))
        fail(err);
    // The callback outputs silence until `ready`; from here on only the audio thread uses the engine.
    engine.rs = std::make_unique<Resampler>(kChipRate, static_cast<double>(rate), 2);
    engine.fifo.reserve((static_cast<size_t>(audio.bufferFrames()) + 4096) * 2);  // refills never reallocate
    engine.ready.store(true, std::memory_order_release);

    if (!o.demo && !midi.open(o.midi, err)) {
        audio.stop();
        fail(err);
    }

    const Wave* w = assets.map.find(o.waveSet, o.wave);
    std::printf("wave %d:%d (%s)  effect %d  bus %d\n", o.waveSet, o.wave,
                w && !w->name.empty() ? w->name.c_str() : "-", o.effect, o.bus);
    std::printf("audio: default device, %s, buffer %u frames (%.1f ms)\n", audio.formatName().c_str(),
                audio.bufferFrames(), audio.bufferFrames() * 1000.0 / rate);
    if (o.demo)
        std::printf("midi: none (--demo: 4 s arpeggio, then it stops by itself)\n");
    else
        std::printf("midi: %s\n", midiName.c_str());
    std::printf("Press Enter to quit.\n");
    for (const std::string& l : first) std::printf("  %s\n", l.c_str());
    std::fflush(stdout);

    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
    EnterWatcher enter;
    while (!engine.finished.load() && !audio.failed() && !g_interrupted.load() && !enter.pressed(50)) printLog(log);

    audio.stop();
    midi.close();
    SetConsoleCtrlHandler(onConsoleCtrl, FALSE);
    printLog(log);

    // The run's health in one line: underruns are audible gaps, clipped samples hit full scale.
    const AudioOut::Stats st = audio.stats();
    char peakDb[32] = "-inf";
    if (st.peak > 0.0f) std::snprintf(peakDb, sizeof peakDb, "%.1f", 20.0 * std::log10(static_cast<double>(st.peak)));
    std::printf("audio: %llu periods, %llu frames (%.2f s), %llu underruns, peak %.3f (%s dBFS), %llu clipped, "
                "slowest refill %.2f ms",
                static_cast<unsigned long long>(st.periods), static_cast<unsigned long long>(st.frames),
                static_cast<double>(st.frames) / rate, static_cast<unsigned long long>(st.underruns),
                static_cast<double>(st.peak), peakDb, static_cast<unsigned long long>(st.clipped), st.maxRenderMs);
    if (o.demo)
        std::printf("; demo %zu events\n", engine.demoNext);
    else
        std::printf("; midi %llu messages, %llu dropped\n", static_cast<unsigned long long>(midi.received()),
                    static_cast<unsigned long long>(midi.dropped()));
    if (audio.failed()) fail(audio.error());
    return 0;
}

#else

int cmdPlay(const Options&) { fail("play needs Windows (WinMM and WASAPI)"); }

#endif

void usage(std::FILE* to) {
    std::fprintf(to,
                 "usage:\n"
                 "  ks32play render <in.mid> <out.wav> [--resampled <out2.wav>] [--rate <hz>] [--tail <s>] [common]\n"
                 "  ks32play waves [--set internal|rom3]\n"
                 "  ks32play waves --audition <set>:<n> <out.wav> [common]\n"
                 "  ks32play play [--midi <index>] [--list] [--demo] [common]\n"
                 "common: --roms <dir>  --data <dir>  --effect <0-10>  --bus <0-2>  --wave <set>:<n>\n"
                 "        --adsr <attackMs,decayMs,sustainPct,releaseMs>\n"
                 "see docs/player.md\n");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            usage(stderr);
            return 2;
        }
        const std::string cmd = argv[1];
        if (cmd == "--help" || cmd == "-h" || cmd == "help") {
            usage(stdout);
            return 0;
        }
        if (cmd != "render" && cmd != "waves" && cmd != "play") failUsage("unknown command '" + cmd + "'");
        const Options o = parseOptions(argc, argv, 2);
        if (cmd == "render") return cmdRender(o);
        if (cmd == "waves") return cmdWaves(o);
        return cmdPlay(o);
    } catch (const Failure& f) {
        std::fprintf(stderr, "error: %s\n", f.msg.c_str());
        if (f.usage) usage(stderr);
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
}
