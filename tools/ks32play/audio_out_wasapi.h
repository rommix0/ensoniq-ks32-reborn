#pragma once
// WASAPI shared-mode output on the default render device, event-driven, in the device's mix format
// with a ~20 ms buffer. One render thread (boosted to the MMCSS "Pro Audio" class) owns every COM
// object: it initialises COM, opens the device, then calls the Render callback each period for the
// free part of the buffer. The callback writes float frames (+-1.0 full scale, `channels` per
// frame); AudioOut clamps them to [-1, 1] and converts to 32-bit float or 16-bit PCM.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AudioOut {
public:
    using Render = std::function<void(float* interleaved, uint32_t frames, uint32_t channels)>;

    struct Stats {
        uint64_t periods = 0;    // buffer refills (one per device event)
        uint64_t frames = 0;     // device frames written
        uint64_t underruns = 0;  // refills that found the device buffer empty (audible gap)
        uint64_t clipped = 0;    // samples outside [-1, 1] before clamping
        float peak = 0.0f;       // largest absolute sample written (1.0 = full scale)
        double maxRenderMs = 0;  // slowest Render callback
    };

    AudioOut() = default;
    ~AudioOut() { stop(); }
    AudioOut(const AudioOut&) = delete;
    AudioOut& operator=(const AudioOut&) = delete;

    // The default render device's mix format, e.g. "48000 Hz, 2 channels, 32-bit float". For --list.
    static bool describeDefault(std::string& desc, std::string& err);

    // Opens the default device, starts the thread and returns once the stream runs (or failed). The
    // callback has already filled the first buffer by then, from the render thread.
    bool start(Render cb, uint32_t& sampleRate, uint32_t& channels, std::string& err);
    void stop();

    bool failed() const { return failed_.load(); }  // the stream stopped on its own (device lost)
    std::string error() const;
    std::string formatName() const { return format_; }      // set by start()
    uint32_t bufferFrames() const { return bufferFrames_; }  // device buffer size, set by start()
    Stats stats() const;  // read after stop()

private:
    void run(Render cb);

    std::thread thread_;
    void* stopEvent_ = nullptr;  // HANDLE
    std::atomic<bool> failed_{false};

    mutable std::mutex mu_;  // guards the start handshake, error_ and stats_
    std::condition_variable cv_;
    std::string error_;
    bool ready_ = false;
    bool startOk_ = false;
    std::string format_;
    uint32_t rate_ = 0, channels_ = 0, bufferFrames_ = 0;
    Stats stats_;
};
