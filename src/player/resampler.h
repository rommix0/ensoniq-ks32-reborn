#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ens::player {

// Polyphase windowed-sinc sample-rate converter: interleaved int16 at inRate -> interleaved float
// (+-1.0 = full scale) at outRate. Kaiser window (beta 9), cutoff 0.92 * min(inRate, outRate) / 2,
// 512 phases interpolated linearly. The kernel is 64 taps (32 input samples either side of the
// interpolation point); when downsampling it is widened in proportion so the cutoff stays sharp.
// Output is the input delayed by latencyFrames(); input history is kept across process() calls, so
// chunking the input does not change the output. Any ratio is accepted.
class Resampler {
public:
    Resampler(double inRate, double outRate, int channels);

    // Consume inFrames interleaved frames and append every output frame that is now complete.
    void process(const int16_t* in, size_t inFrames, std::vector<float>& out);

    // Filter delay, in output frames.
    double latencyFrames() const { return latency_; }

private:
    static constexpr int kPhases = 512;

    int channels_;
    int half_;          // input samples either side of the interpolation point
    int taps_;          // 2 * half_
    double step_;       // input frames per output frame
    double latency_;
    std::vector<float> table_;   // (kPhases + 1) phases x taps_
    std::vector<float> buf_;     // interleaved input history, first frame has absolute index base_
    int64_t base_;
    uint64_t inCount_ = 0;       // input frames received
    uint64_t outCount_ = 0;      // output frames produced
};

}  // namespace ens::player
