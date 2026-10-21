#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "check.h"
#include "player/resampler.h"

using ens::player::Resampler;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kChipRate = 30476180.0 / 1024.0;  // 29761.8..., the brief's 29761.9 Hz

// Peak amplitude (dB re 1.0) of the component at `freq`: Hann-windowed Goertzel so a strong
// neighbouring tone does not leak into the bin being measured.
double goertzelDb(const std::vector<float>& x, double rate, double freq) {
    const size_t n = x.size();
    const double w0 = 2.0 * kPi * freq / rate;
    const double coeff = 2.0 * std::cos(w0);
    double s1 = 0.0, s2 = 0.0, wsum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n - 1));
        const double s = static_cast<double>(x[i]) * w + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
        wsum += w;
    }
    const double re = s1 - s2 * std::cos(w0);
    const double im = s2 * std::sin(w0);
    const double amp = 2.0 * std::sqrt(re * re + im * im) / wsum;
    return 20.0 * std::log10(amp + 1e-30);
}

// 1 s of mono int16 at the chip rate containing the given sines (amplitude each).
std::vector<int16_t> makeInput(const std::vector<double>& freqs, double amp) {
    const size_t n = static_cast<size_t>(std::ceil(kChipRate));  // 29762 frames, ~1 s
    std::vector<int16_t> in(n);
    for (size_t i = 0; i < n; ++i) {
        double v = 0.0;
        for (double f : freqs) v += amp * std::sin(2.0 * kPi * f * static_cast<double>(i) / kChipRate);
        in[i] = static_cast<int16_t>(std::lround(v));
    }
    return in;
}

// Resample and drop the start-up region (latency + 64 outputs).
std::vector<float> run(const std::vector<int16_t>& in, double outRate) {
    Resampler r(kChipRate, outRate, 1);
    std::vector<float> out;
    r.process(in.data(), in.size(), out);
    const size_t skip = static_cast<size_t>(std::ceil(r.latencyFrames())) + 64;
    return std::vector<float>(out.begin() + static_cast<std::ptrdiff_t>(skip), out.end());
}

}  // namespace

TEST(resampler_48000_passband) {
    const std::vector<float> y = run(makeInput({1000.0}, 16384.0), 48000.0);
    // RMS over a whole number of 1 kHz periods (48 output samples each).
    const size_t n = y.size() / 48 * 48;
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += static_cast<double>(y[i]) * static_cast<double>(y[i]);
    const double rmsDb = 20.0 * std::log10(std::sqrt(acc / static_cast<double>(n)));
    const double wantDb = 20.0 * std::log10(0.5 / std::sqrt(2.0));
    std::printf("    48k passband 1 kHz: %.5f dB error\n", rmsDb - wantDb);
    CHECK(std::fabs(rmsDb - wantDb) < 0.05);
}

TEST(resampler_48000_image_rejection) {
    const std::vector<float> y = run(makeInput({10000.0}, 16384.0), 48000.0);
    const double tone = goertzelDb(y, 48000.0, 10000.0);
    const double image = goertzelDb(y, 48000.0, kChipRate - 10000.0);
    std::printf("    48k image rejection: tone %.3f dB, image %.2f dB below\n", tone, tone - image);
    CHECK(tone - image >= 70.0);
}

TEST(resampler_44100_passband) {
    const std::vector<float> y1 = run(makeInput({1000.0}, 16384.0), 44100.0);
    const std::vector<float> y12 = run(makeInput({12000.0}, 16384.0), 44100.0);
    const double want = 20.0 * std::log10(0.5);
    const double e1 = goertzelDb(y1, 44100.0, 1000.0) - want;
    const double e12 = goertzelDb(y12, 44100.0, 12000.0) - want;
    std::printf("    44.1k passband: 1 kHz %.5f dB, 12 kHz %.5f dB\n", e1, e12);
    CHECK(std::fabs(e1) < 0.1);
    CHECK(std::fabs(e12) < 0.1);
}

TEST(resampler_frame_count) {
    const std::vector<int16_t> in = makeInput({1000.0}, 8000.0);
    const size_t total = 29762;
    CHECK(in.size() >= total);
    Resampler r(kChipRate, 48000.0, 1);
    std::vector<float> out;
    const size_t chunks[] = {1, 7, 1000};
    size_t pos = 0;
    for (size_t c : chunks) {
        r.process(in.data() + pos, c, out);
        pos += c;
    }
    r.process(in.data() + pos, total - pos, out);
    std::printf("    frame count: %zu output frames\n", out.size());
    CHECK(out.size() >= 47998 && out.size() <= 48002);

    // Chunked processing must equal one-shot processing exactly.
    Resampler r2(kChipRate, 48000.0, 1);
    std::vector<float> whole;
    r2.process(in.data(), total, whole);
    CHECK_EQ(out.size(), whole.size());
    bool same = out.size() == whole.size();
    for (size_t i = 0; same && i < out.size(); ++i) same = out[i] == whole[i];
    CHECK(same);
}
