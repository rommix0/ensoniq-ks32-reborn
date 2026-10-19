// mame_ref: run a trace (docs/trace-format.md) on MAME's own ES5505 + ES5510 code (vendored in
// tools/mame_ref/vendor, compiled against tools/mame_ref/shim) and write raw PCM in the same
// layout as ens_trace, so `pcm_diff` can compare MAME with our cores on identical input.
//   mame_ref <trace> <out.pcm>
// Output: 4 little-endian int16 per frame: auxL auxR mainL mainR.
// Exit status: 0 ok, 1 file/trace error, 2 bad arguments.
//
// The chips are driven the way MAME's esq5505 driver and esqpump device drive them; every place
// where the trace conventions (TR1, TR2, R15) have to be mapped onto MAME is listed in
// docs/mame-ref.md. Only the trace parser (ens_trace_parse) is shared with our tools.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "emu.h"
#include "es5506.h"
#include "es5510.h"
#include "trace/trace.h"

namespace {

// KS-32 OTIS clock (esq5505.cpp `common32`: 30.47618 MHz / 2). Only sets MAME's sample-rate
// bookkeeping; it has no effect on the generated samples.
constexpr u32 kOtisClock = 15238090;
constexpr u32 kEspClock = 10000000;

void usage() {
    std::fprintf(stderr,
                 "usage: mame_ref <trace> <out.pcm>\n"
                 "  Runs the trace on MAME's ES5505/ES5510 code and writes int16 LE auxL auxR\n"
                 "  mainL mainR per frame to <out.pcm> (same layout as ens_trace). Relative wave\n"
                 "  paths are resolved against the trace's directory. Exit 1 on an unreadable file\n"
                 "  or a trace error (reported as <trace>:<line>: message).\n");
}

int fail2(const char* msg, const char* arg) {
    std::fprintf(stderr, "mame_ref: %s%s%s\n", msg, arg ? " " : "", arg ? arg : "");
    usage();
    return 2;
}

bool readFile(const char* path, std::vector<unsigned char>& bytes, size_t limit) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    unsigned char buf[4096];
    size_t n;
    while (bytes.size() <= limit && (n = std::fread(buf, 1, sizeof buf, f)) > 0)
        bytes.insert(bytes.end(), buf, buf + n);
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

// Same checks and messages as the ens_trace runner: raw little-endian uint16 words, at most 1M.
bool readWaveImage(const std::string& path, std::vector<u16>& words, std::string& msg) {
    std::vector<unsigned char> bytes;
    const size_t limit = static_cast<size_t>(ens::kTraceMaxWaveWords) * 2;
    if (!readFile(path.c_str(), bytes, limit)) {
        msg = "cannot read wave file '" + path + "'";
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
        words[i] = static_cast<u16>(bytes[2 * i] | (bytes[2 * i + 1] << 8));
    return true;
}

// Mirrors esq5505 + esq_5505_5510_pump around the two vendored devices.
class MameRig {
public:
    // Wave images become MAME ROM regions (word i = OTIS word address i, no byte swapping).
    // An empty image leaves the region unset, so that bank is unmapped and reads 0.
    MameRig(std::vector<u16> bank0, std::vector<u16> bank1)
        : otis_(cfg_, "otis", nullptr, kOtisClock), esp_(cfg_, "esp", nullptr, kEspClock) {
        if (!bank0.empty()) region0_ = std::make_unique<memory_region>(std::move(bank0));
        if (!bank1.empty()) region1_ = std::make_unique<memory_region>(std::move(bank1));
        otis_.set_region0(region0_.get());
        otis_.set_region1(region1_.get());
        otis_.set_channels(4);  // esq5505.cpp: m_otis->set_channels(4)
        otis_.start();
        esp_.start();
        otis_.reset();
        esp_.reset();
    }

    void otisWrite(u32 reg, u32 value) { otis_.write(reg, static_cast<u16>(value)); }

    // MAME applies host writes at once (select-register transfers included), so the idle step
    // our runner performs after each write while halted (R15) has no MAME counterpart.
    void espWrite(u32 addr, u32 value) { esp_.host_w(addr, static_cast<u8>(value)); }

    // DECISION TR2 (MAME mapping): ESPHALT only sets the pump's halted flag (esq5505
    // duart_output). The `$1F` write that the trace adds when halting a running ESP is passed
    // to host_w as the firmware's write would be; run_once() is atomic, so MAME never stops
    // mid-program and the halt is effectively immediate.
    void espHalt(bool high) {
        if (high && !espHalted_) esp_.host_w(0x1F, 0xFF);
        espHalted_ = high;
    }

    // One frame: one OTIS sample, then esq_5505_5510_pump_device::sound_stream_update.
    void frame(std::vector<int16_t>& out) {
        using sample_t = sound_stream::sample_t;
        constexpr sample_t input_scale = 32768.0;
        const sound_stream& otisOut = otis_.update_one_sample();
        sample_t in[8];
        for (int i = 0; i < 8; ++i) in[i] = otisOut.get_output(i, 0);  // routes at gain 1.0

        // DECISION TR1: OTIS ch0 -> aux; ch1..ch3 -> ESP SER0..SER2; ESP SER3 -> main
        // (esqpump.cpp, hard-wired). MAME's aux path stays float all the way to the speaker; the
        // harness converts it to int16 the same way the pump converts the ESP inputs.
        out.push_back(static_cast<int16_t>(s32(in[0] * input_scale)));
        out.push_back(static_cast<int16_t>(s32(in[1] * input_scale)));

        // esqpump: `ser_w(n, s32(stream.get(2 + n, 0) * input_scale))`; s32() truncates toward
        // zero, reproduced on purpose (MAME behaviour, not the hardware's floor).
        for (int n = 0; n < 6; ++n) esp_.ser_w(n, static_cast<int16_t>(s32(in[2 + n] * input_scale)));
        if (!espHalted_) esp_.run_once();
        // ser_r(6/7) * (1/32768) to the speaker and back to int16 is exact; take the latch.
        out.push_back(esp_.ser_r(6));
        out.push_back(esp_.ser_r(7));
    }

private:
    machine_config cfg_;
    std::unique_ptr<memory_region> region0_, region1_;
    es5505_device otis_;
    es5510_device esp_;
    // The trace starts with the ESP running and HALT low (docs/trace-format.md "Output");
    // esqpump's constructor default (halted) is overridden to match.
    bool espHalted_ = false;
};

}  // namespace

int main(int argc, char** argv) {
    const char* paths[2] = {nullptr, nullptr};
    int n = 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') return fail2("unknown option", argv[i]);
        if (n == 2) return fail2("unexpected extra argument", argv[i]);
        paths[n++] = argv[i];
    }
    if (n != 2) return fail2("expected a trace file and an output file", nullptr);

    std::vector<unsigned char> textBytes;
    if (!readFile(paths[0], textBytes, SIZE_MAX)) {
        std::fprintf(stderr, "mame_ref: cannot read %s\n", paths[0]);
        return 1;
    }
    const std::string text(textBytes.begin(), textBytes.end());
    const std::string tracePath = paths[0];
    const size_t slash = tracePath.find_last_of("/\\");
    const std::string baseDir = slash == std::string::npos ? std::string() : tracePath.substr(0, slash);

    std::vector<ens::TraceCmd> cmds;
    ens::TraceError err;
    if (!ens::parseTrace(text, baseDir, cmds, err)) {
        std::fprintf(stderr, "mame_ref: %s:%d: %s\n", paths[0], err.line, err.msg.c_str());
        return 1;
    }
    std::vector<u16> banks[2];
    uint64_t totalFrames = 0;
    for (const ens::TraceCmd& c : cmds) {
        if (c.op == ens::TraceOp::Wave) {
            std::string msg;
            if (!readWaveImage(c.path, banks[c.a & 1], msg)) {
                std::fprintf(stderr, "mame_ref: %s:%d: %s\n", paths[0], c.line, msg.c_str());
                return 1;
            }
        } else if (c.op == ens::TraceOp::Frames) {
            totalFrames += c.a;
        }
    }

    MameRig rig(std::move(banks[0]), std::move(banks[1]));
    std::vector<int16_t> pcm;
    pcm.reserve(static_cast<size_t>(totalFrames) * 4);
    for (const ens::TraceCmd& c : cmds) {
        switch (c.op) {
            case ens::TraceOp::Wave: break;  // loaded above
            // MAME's ESP DRAM is fixed at 1M words (es5510.h DRAM_SIZE); the size is not modelled.
            case ens::TraceOp::Dram: break;
            case ens::TraceOp::OtisWrite: rig.otisWrite(c.a, c.b); break;
            case ens::TraceOp::EspWrite: rig.espWrite(c.a, c.b); break;
            case ens::TraceOp::EspHalt: rig.espHalt(c.a != 0); break;
            // MAME has no halted-state idle cycles (RAM clear is instant, no refresh model).
            case ens::TraceOp::EspIdle: break;
            case ens::TraceOp::Frames:
                for (uint32_t i = 0; i < c.a; ++i) rig.frame(pcm);
                break;
        }
    }

    std::vector<unsigned char> bytes(pcm.size() * 2);
    for (size_t i = 0; i < pcm.size(); ++i) {
        const uint16_t u = static_cast<uint16_t>(pcm[i]);
        bytes[2 * i] = static_cast<unsigned char>(u & 0xFF);
        bytes[2 * i + 1] = static_cast<unsigned char>(u >> 8);
    }
    std::FILE* o = std::fopen(paths[1], "wb");
    if (!o) {
        std::fprintf(stderr, "mame_ref: cannot create %s\n", paths[1]);
        return 1;
    }
    const bool ok = (bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), o) == bytes.size());
    const bool closed = std::fclose(o) == 0;
    if (!ok || !closed) {
        std::fprintf(stderr, "mame_ref: error writing %s\n", paths[1]);
        return 1;
    }
    return 0;
}
