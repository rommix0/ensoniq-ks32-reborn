#pragma once
// Trace format: a text script that feeds identical input to our OTIS/ESP cores and to the MAME
// reference harness (docs/trace-format.md). Parsing (text -> commands) and PCM comparison are
// pure and free of chip classes, so the MAME harness can reuse them (trace_parse.cpp, library
// ens_trace_parse). The runner that drives our chips lives in trace.cpp (library ens_trace).
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ens {

struct TraceError {
    int line = 0;  // 1-based; 0 when the error is not tied to a trace line
    std::string msg;
};

enum class TraceOp { Wave, Dram, OtisWrite, EspWrite, EspHalt, EspIdle, Frames };

// One parsed command. Field use by op:
//   Wave: a = bank (0/1), path = file path (already joined with baseDir)
//   Dram: a = size in words        OtisWrite: a = register 0-15, b = value
//   EspWrite: a = host address, b = value        EspHalt: a = pin level (0/1)
//   EspIdle: a = idle steps        Frames: a = frame count
struct TraceCmd {
    TraceOp op = TraceOp::Frames;
    int line = 0;
    uint32_t a = 0, b = 0;
    std::string path;
};

// Limits shared by every consumer of the format.
constexpr uint32_t kTraceMaxWaveWords = 1u << 20;   // one OTIS bank
constexpr uint32_t kTraceMaxDramWords = 1u << 24;   // 24-bit ESP word addresses
constexpr uint32_t kTraceMaxIdleSteps = 1u << 24;   // per espidle command
constexpr uint64_t kTraceMaxTotalFrames = 1u << 25; // per trace (256 MiB of output)

// Parses the whole text. Relative wave paths are joined with baseDir (empty = as given). Returns
// false with err.line/err.msg on the first error; `out` then holds the commands parsed so far.
// Never throws on malformed input.
bool parseTrace(const std::string& text, const std::string& baseDir, std::vector<TraceCmd>& out,
                TraceError& err);

// Result of comparing two interleaved PCM buffers (samples, not bytes).
struct PcmCompare {
    size_t framesA = 0, framesB = 0;  // whole frames in each buffer
    bool identical = false;           // same length and every sample equal
    bool lengthDiffers = false;
    int64_t firstDiffFrame = -1;      // -1 when no compared sample differs
    std::vector<int> maxAbs;          // per channel, over the compared samples
    std::vector<double> rms;          // per channel, over the compared samples
};
PcmCompare pcmCompare(const std::vector<int16_t>& a, const std::vector<int16_t>& b, int channels);

// Drives one Otis + one Esp from a parsed trace (routing TR1, see docs/trace-format.md).
class TraceRunner {
public:
    TraceRunner();
    ~TraceRunner();
    TraceRunner(const TraceRunner&) = delete;
    TraceRunner& operator=(const TraceRunner&) = delete;

    // Parses `text` and loads the wave images it names. false + err on any error (the error
    // line is the trace line of the offending command, also for unreadable wave files).
    bool load(const std::string& text, const std::string& baseDir, TraceError& err);
    // Resets both chips and executes all commands in order. `out` is replaced with 4 int16
    // per frame: auxL auxR mainL mainR.
    void run(std::vector<int16_t>& outInterleaved4);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ens
