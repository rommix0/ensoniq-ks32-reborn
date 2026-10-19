#include "trace/trace.h"

#include <cstdio>

#include "esp/esp.h"
#include "otis/otis.h"

namespace ens {

namespace {

// Wave ROM image for both OTIS banks. Reads past the end of an image return 0.
struct Waves : WaveMemory {
    std::vector<uint16_t> bank[2];
    uint16_t read(int b, uint32_t wordAddr) override {
        const std::vector<uint16_t>& m = bank[b & 1];
        return wordAddr < m.size() ? m[wordAddr] : 0;
    }
};

// ESP external RAM. Word addresses are masked to the (power-of-two) size; I/O space reads 0
// and ignores writes.
struct Dram : EspBus {
    std::vector<uint16_t> ram;
    uint32_t mask = 0;
    void resize(uint32_t words) {
        ram.assign(words, 0);
        mask = words - 1;
    }
    uint16_t read(bool io, uint32_t a) override { return io ? 0 : ram[a & mask]; }
    void write(bool io, uint32_t a, uint16_t v) override {
        if (!io) ram[a & mask] = v;
    }
};

constexpr uint32_t kDefaultDramWords = 65536;  // KS-32 MEMSIZ = 0x0000FF (findings.md section D)

// Reads a raw little-endian uint16 image. false + msg on any problem.
bool readWaveImage(const std::string& path, std::vector<uint16_t>& words, std::string& msg) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        msg = "cannot open wave file '" + path + "'";
        return false;
    }
    std::vector<unsigned char> bytes;
    unsigned char buf[4096];
    size_t n;
    const size_t limit = static_cast<size_t>(kTraceMaxWaveWords) * 2;
    while (bytes.size() <= limit && (n = std::fread(buf, 1, sizeof buf, f)) > 0)
        bytes.insert(bytes.end(), buf, buf + n);
    const bool readError = std::ferror(f) != 0;
    std::fclose(f);
    if (readError) {
        msg = "error reading wave file '" + path + "'";
        return false;
    }
    if (bytes.size() > limit) {
        msg = "wave file '" + path + "' is larger than 1M words";
        return false;
    }
    if (bytes.size() % 2 != 0) {
        msg = "wave file '" + path + "' has an odd byte count";
        return false;
    }
    words.resize(bytes.size() / 2);
    for (size_t i = 0; i < words.size(); ++i)
        words[i] = static_cast<uint16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8));
    return true;
}

}  // namespace

struct TraceRunner::Impl {
    Waves waves;
    Dram dram;
    Otis otis{waves};
    Esp esp{dram};
    std::vector<TraceCmd> cmds;
    uint32_t dramWords = kDefaultDramWords;
    uint64_t totalFrames = 0;

    // DECISION TR2: `esphalt 1` while the ESP still runs also writes host $1F (halt enable), as the
    // firmware does, so the ESP stops at once instead of at the next END; later `esp` host writes
    // are followed by one idleStep() while halted so select-register transfers land (R15).
    void espHalt(bool high) {
        esp.setHaltPin(high);
        if (high && !esp.halted()) esp.hostWrite(0x1F, 0xFF);
    }
    void espWrite(uint8_t addr, uint8_t v) {
        esp.hostWrite(addr, v);
        if (esp.halted()) esp.idleStep();
    }

    void frame(std::vector<int16_t>& out) {
        const OtisFrame f = otis.runFrame();
        // DECISION TR1: OTIS ch0 -> aux out; ch1..ch3 -> ESP SER0..SER2; ESP SER3 -> main out
        // (KS-32 firmware, findings.md section C).
        esp.setSerialIn(0, f.ch[1][0], f.ch[1][1]);
        esp.setSerialIn(1, f.ch[2][0], f.ch[2][1]);
        esp.setSerialIn(2, f.ch[3][0], f.ch[3][1]);
        esp.runSample();
        int16_t ml = 0, mr = 0;
        esp.serialOut(3, ml, mr);
        out.push_back(f.ch[0][0]);
        out.push_back(f.ch[0][1]);
        out.push_back(ml);
        out.push_back(mr);
    }
};

TraceRunner::TraceRunner() : impl_(new Impl) {}
TraceRunner::~TraceRunner() = default;

bool TraceRunner::load(const std::string& text, const std::string& baseDir, TraceError& err) {
    Impl& s = *impl_;
    s.cmds.clear();
    s.waves.bank[0].clear();
    s.waves.bank[1].clear();
    s.dramWords = kDefaultDramWords;
    s.totalFrames = 0;
    if (!parseTrace(text, baseDir, s.cmds, err)) return false;
    for (const TraceCmd& c : s.cmds) {
        if (c.op == TraceOp::Wave) {
            std::string msg;
            if (!readWaveImage(c.path, s.waves.bank[c.a & 1], msg)) {
                err.line = c.line;
                err.msg = msg;
                return false;
            }
        } else if (c.op == TraceOp::Dram) {
            s.dramWords = c.a;
        } else if (c.op == TraceOp::Frames) {
            s.totalFrames += c.a;
        }
    }
    return true;
}

void TraceRunner::run(std::vector<int16_t>& out) {
    Impl& s = *impl_;
    out.clear();
    out.reserve(static_cast<size_t>(s.totalFrames) * 4);
    s.dram.resize(s.dramWords);
    s.esp.setHaltPin(false);
    s.esp.reset();
    s.otis.reset();
    for (const TraceCmd& c : s.cmds) {
        switch (c.op) {
            case TraceOp::Wave:
            case TraceOp::Dram: break;  // applied by load()
            case TraceOp::OtisWrite: s.otis.write(static_cast<uint8_t>(c.a), static_cast<uint16_t>(c.b)); break;
            case TraceOp::EspWrite: s.espWrite(static_cast<uint8_t>(c.a), static_cast<uint8_t>(c.b)); break;
            case TraceOp::EspHalt: s.espHalt(c.a != 0); break;
            case TraceOp::EspIdle:
                for (uint32_t i = 0; i < c.a; ++i) s.esp.idleStep();
                break;
            case TraceOp::Frames:
                for (uint32_t i = 0; i < c.a; ++i) s.frame(out);
                break;
        }
    }
}

}  // namespace ens
