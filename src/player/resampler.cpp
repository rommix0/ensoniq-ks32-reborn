#include "player/resampler.h"

#include <algorithm>
#include <cmath>

namespace ens::player {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kBeta = 9.0;
constexpr int kBaseHalf = 32;

// Modified Bessel function of the first kind, order 0 (power series).
double besselI0(double x) {
    const double q = x * x / 4.0;
    double term = 1.0, sum = 1.0;
    for (int k = 1; k < 200; ++k) {
        term *= q / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < sum * 1e-18) break;
    }
    return sum;
}

}  // namespace

Resampler::Resampler(double inRate, double outRate, int channels)
    : channels_(channels < 1 ? 1 : channels),
      half_(kBaseHalf),
      taps_(0),
      step_(inRate / outRate),
      latency_(0.0),
      base_(0) {
    if (inRate > outRate) half_ = static_cast<int>(std::ceil(kBaseHalf * inRate / outRate));
    taps_ = 2 * half_;
    latency_ = static_cast<double>(half_) * outRate / inRate;

    // Cutoff in cycles per input sample.
    const double fc = 0.92 * std::min(inRate, outRate) / 2.0 / inRate;
    const double i0Beta = besselI0(kBeta);
    const double halfD = static_cast<double>(half_);

    table_.resize(static_cast<size_t>(kPhases + 1) * static_cast<size_t>(taps_));
    for (int p = 0; p <= kPhases; ++p) {
        const double frac = static_cast<double>(p) / kPhases;
        float* row = &table_[static_cast<size_t>(p) * static_cast<size_t>(taps_)];
        std::vector<double> h(static_cast<size_t>(taps_));
        double sum = 0.0;
        for (int j = 0; j < taps_; ++j) {
            const double d = static_cast<double>(j - (half_ - 1)) - frac;  // distance from the point
            const double r = d / halfD;
            double v = 0.0;
            if (r > -1.0 && r < 1.0) {
                const double sinc = std::fabs(d) < 1e-12 ? 2.0 * fc : std::sin(2.0 * kPi * fc * d) / (kPi * d);
                v = sinc * besselI0(kBeta * std::sqrt(1.0 - r * r)) / i0Beta;
            }
            h[static_cast<size_t>(j)] = v;
            sum += v;
        }
        for (int j = 0; j < taps_; ++j) row[j] = static_cast<float>(h[static_cast<size_t>(j)] / sum);
    }

    // Zero history for the samples before the first input (taps reach 2*half_ - 1 frames back).
    base_ = -2 * static_cast<int64_t>(half_);
    buf_.assign(static_cast<size_t>(2 * half_) * static_cast<size_t>(channels_), 0.0f);
    // The most the history holds when fed one frame at a time, so that never reallocates (the
    // Plan 7 engine on the audio thread).
    buf_.reserve(static_cast<size_t>(2 * half_ + 1) * static_cast<size_t>(channels_));
}

void Resampler::process(const int16_t* in, size_t inFrames, std::vector<float>& out) {
    const size_t nch = static_cast<size_t>(channels_);
    for (size_t i = 0; i < inFrames * nch; ++i) buf_.push_back(static_cast<float>(in[i]) / 32768.0f);
    inCount_ += inFrames;

    const size_t taps = static_cast<size_t>(taps_);
    for (;;) {
        // Output n is the signal at input time n*step - half_; its newest tap is floor(n*step).
        const double t = static_cast<double>(outCount_) * step_;
        const int64_t pos = static_cast<int64_t>(std::floor(t));
        if (pos < 0 || static_cast<uint64_t>(pos) >= inCount_) break;
        const double frac = t - static_cast<double>(pos);
        double pf = frac * kPhases;
        int p = static_cast<int>(pf);
        if (p > kPhases - 1) p = kPhases - 1;
        const double w = pf - static_cast<double>(p);

        const int64_t first = pos - 2 * static_cast<int64_t>(half_) + 1;  // oldest tap
        const float* x = &buf_[static_cast<size_t>(first - base_) * nch];
        const float* c0 = &table_[static_cast<size_t>(p) * taps];
        const float* c1 = c0 + taps;
        for (size_t ch = 0; ch < nch; ++ch) {
            double a0 = 0.0, a1 = 0.0;
            for (size_t j = 0; j < taps; ++j) {
                const double s = static_cast<double>(x[j * nch + ch]);
                a0 += s * static_cast<double>(c0[j]);
                a1 += s * static_cast<double>(c1[j]);
            }
            out.push_back(static_cast<float>(a0 + w * (a1 - a0)));
        }
        ++outCount_;
    }

    // Drop history that no later output can reach.
    const int64_t nextFirst =
        static_cast<int64_t>(std::floor(static_cast<double>(outCount_) * step_)) - 2 * static_cast<int64_t>(half_) + 1;
    if (nextFirst > base_) {
        const size_t drop = std::min(static_cast<size_t>(nextFirst - base_) * nch, buf_.size());
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(drop));
        base_ += static_cast<int64_t>(drop / nch);
    }
}

}  // namespace ens::player
