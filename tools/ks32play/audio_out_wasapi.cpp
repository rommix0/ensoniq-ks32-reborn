#include "audio_out_wasapi.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <objbase.h>

namespace {

// Spelled out here so neither __uuidof nor the uuid import library is needed (MinGW and MSVC alike).
const GUID kClsidMMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidIMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidIAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const GUID kIidIAudioRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
const GUID kSubtypePcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

constexpr REFERENCE_TIME kBufferDuration = 200000;  // 20 ms in 100 ns units

enum class SampleType { Float32, Pcm16, Unsupported };

template <class T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

std::string hrText(const char* what, HRESULT hr) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
    std::string s = buf;
    if (hr == static_cast<HRESULT>(0x80070490L)) s += ": no audio output device";
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) s += ": the audio device was removed or changed";
    if (hr == AUDCLNT_E_DEVICE_IN_USE) s += ": the audio device is in exclusive use";
    return s;
}

SampleType sampleType(const WAVEFORMATEX* wf) {
    if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && wf->wBitsPerSample == 32) return SampleType::Float32;
    if (wf->wFormatTag == WAVE_FORMAT_PCM && wf->wBitsPerSample == 16) return SampleType::Pcm16;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22) {
        const auto* x = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        if (IsEqualGUID(x->SubFormat, kSubtypeFloat) && wf->wBitsPerSample == 32) return SampleType::Float32;
        if (IsEqualGUID(x->SubFormat, kSubtypePcm) && wf->wBitsPerSample == 16) return SampleType::Pcm16;
    }
    return SampleType::Unsupported;
}

// "48000 Hz, 2 channels, 32-bit float"; for formats this player cannot write, the raw fields.
std::string describe(const WAVEFORMATEX* wf) {
    const SampleType t = sampleType(wf);
    char buf[160];
    if (t != SampleType::Unsupported) {
        std::snprintf(buf, sizeof buf, "%lu Hz, %u channels, %s", static_cast<unsigned long>(wf->nSamplesPerSec),
                      static_cast<unsigned>(wf->nChannels), t == SampleType::Float32 ? "32-bit float" : "16-bit PCM");
    } else {
        std::snprintf(buf, sizeof buf, "%lu Hz, %u channels, format tag 0x%04X, %u bits",
                      static_cast<unsigned long>(wf->nSamplesPerSec), static_cast<unsigned>(wf->nChannels),
                      static_cast<unsigned>(wf->wFormatTag), static_cast<unsigned>(wf->wBitsPerSample));
    }
    return buf;
}

// Default render device's IAudioClient (COM must be initialised on this thread).
HRESULT openDefaultClient(IMMDeviceEnumerator*& en, IMMDevice*& dev, IAudioClient*& client, std::string& err) {
    HRESULT hr = CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidIMMDeviceEnumerator,
                                  reinterpret_cast<void**>(&en));
    if (FAILED(hr)) {
        err = hrText("creating the audio device enumerator", hr);
        return hr;
    }
    hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (FAILED(hr)) {
        err = hrText("finding the default audio output device", hr);
        return hr;
    }
    hr = dev->Activate(kIidIAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
    if (FAILED(hr)) err = hrText("opening the audio device", hr);
    return hr;
}

}  // namespace

bool AudioOut::describeDefault(std::string& desc, std::string& err) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        err = hrText("CoInitializeEx", init);
        return false;
    }
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    WAVEFORMATEX* wf = nullptr;
    bool ok = false;
    if (SUCCEEDED(openDefaultClient(en, dev, client, err))) {
        const HRESULT hr = client->GetMixFormat(&wf);
        if (FAILED(hr)) {
            err = hrText("reading the audio mix format", hr);
        } else {
            desc = describe(wf);
            if (sampleType(wf) == SampleType::Unsupported || wf->nChannels < 2) desc += " (not supported by play)";
            REFERENCE_TIME def = 0, minimum = 0;
            if (SUCCEEDED(client->GetDevicePeriod(&def, &minimum))) {
                char buf[64];
                std::snprintf(buf, sizeof buf, ", device period %.1f ms", static_cast<double>(def) / 10000.0);
                desc += buf;
            }
            ok = true;
        }
    }
    if (wf) CoTaskMemFree(wf);
    release(client);
    release(dev);
    release(en);
    if (SUCCEEDED(init)) CoUninitialize();
    return ok;
}

bool AudioOut::start(Render cb, uint32_t& sampleRate, uint32_t& channels, std::string& err) {
    stop();
    {
        std::lock_guard<std::mutex> lock(mu_);
        ready_ = startOk_ = false;
        error_.clear();
        stats_ = Stats{};
    }
    failed_ = false;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) {
        err = "cannot create an event";
        return false;
    }
    thread_ = std::thread(&AudioOut::run, this, std::move(cb));
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this] { return ready_; });
    if (!startOk_) {
        err = error_;
        lock.unlock();
        stop();
        return false;
    }
    sampleRate = rate_;
    channels = channels_;
    return true;
}

void AudioOut::stop() {
    if (thread_.joinable()) {
        SetEvent(static_cast<HANDLE>(stopEvent_));
        thread_.join();
    }
    if (stopEvent_) {
        CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
    }
}

std::string AudioOut::error() const {
    std::lock_guard<std::mutex> lock(mu_);
    return error_;
}

AudioOut::Stats AudioOut::stats() const {
    std::lock_guard<std::mutex> lock(mu_);
    return stats_;
}

void AudioOut::run(Render cb) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    WAVEFORMATEX* wf = nullptr;
    HANDLE audioEvent = nullptr;
    SampleType type = SampleType::Unsupported;
    UINT32 bufFrames = 0;
    std::string err;

    // Open and initialise the stream; any failure leaves `err` set.
    [&] {
        if (FAILED(init)) {
            err = hrText("CoInitializeEx", init);
            return;
        }
        if (FAILED(openDefaultClient(en, dev, client, err))) return;
        HRESULT hr = client->GetMixFormat(&wf);
        if (FAILED(hr)) {
            err = hrText("reading the audio mix format", hr);
            return;
        }
        type = sampleType(wf);
        if (type == SampleType::Unsupported || wf->nChannels < 2) {
            err = "unsupported audio format (" + describe(wf) +
                  "); play writes 32-bit float or 16-bit PCM with 2 or more channels";
            return;
        }
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, kBufferDuration, 0, wf,
                                nullptr);
        if (FAILED(hr)) {
            err = hrText("initialising the audio stream", hr);
            return;
        }
        audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!audioEvent) {
            err = "cannot create an event";
            return;
        }
        hr = client->SetEventHandle(audioEvent);
        if (SUCCEEDED(hr)) hr = client->GetBufferSize(&bufFrames);
        if (SUCCEEDED(hr)) hr = client->GetService(kIidIAudioRenderClient, reinterpret_cast<void**>(&render));
        if (FAILED(hr)) err = hrText("setting up the audio stream", hr);
    }();

    HANDLE mmcss = nullptr;
    const uint32_t nch = wf ? wf->nChannels : 0;
    std::vector<float> buf;
    Stats st;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    // Render `frames` frames into the device buffer.
    auto fill = [&](UINT32 frames) -> HRESULT {
        BYTE* data = nullptr;
        HRESULT hr = render->GetBuffer(frames, &data);
        if (FAILED(hr)) return hr;
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        cb(buf.data(), frames, nch);
        QueryPerformanceCounter(&t1);
        st.maxRenderMs = std::max(st.maxRenderMs, static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 /
                                                      static_cast<double>(freq.QuadPart));
        const size_t n = static_cast<size_t>(frames) * nch;
        for (size_t i = 0; i < n; ++i) {
            float v = buf[i];
            if (!(v >= -1.0f && v <= 1.0f)) {  // also catches NaN
                ++st.clipped;
                v = v > 0.0f ? 1.0f : -1.0f;
            }
            st.peak = std::max(st.peak, std::fabs(v));
            if (type == SampleType::Float32) {
                reinterpret_cast<float*>(data)[i] = v;
            } else {
                const long s = std::lrint(static_cast<double>(v) * 32768.0);
                reinterpret_cast<int16_t*>(data)[i] = static_cast<int16_t>(std::clamp(s, -32768L, 32767L));
            }
        }
        hr = render->ReleaseBuffer(frames, 0);
        if (SUCCEEDED(hr)) {
            ++st.periods;
            st.frames += frames;
        }
        return hr;
    };

    if (err.empty()) {
        DWORD taskIndex = 0;
        mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        buf.assign(static_cast<size_t>(bufFrames) * nch, 0.0f);
        HRESULT hr = fill(bufFrames);  // pre-roll a full buffer
        if (SUCCEEDED(hr)) hr = client->Start();
        if (FAILED(hr)) err = hrText("starting the audio stream", hr);
    }

    {  // hand the outcome to start()
        std::lock_guard<std::mutex> lock(mu_);
        startOk_ = err.empty();
        error_ = err;
        if (startOk_) {
            rate_ = wf->nSamplesPerSec;
            channels_ = wf->nChannels;
            bufferFrames_ = bufFrames;
            format_ = describe(wf);
        }
        ready_ = true;
    }
    cv_.notify_all();

    if (err.empty()) {
        const HANDLE waits[2] = {static_cast<HANDLE>(stopEvent_), audioEvent};
        HRESULT hr = S_OK;
        for (;;) {
            const DWORD w = WaitForMultipleObjects(2, waits, FALSE, 2000);
            if (w == WAIT_OBJECT_0) break;  // stop()
            if (w != WAIT_OBJECT_0 + 1) {
                err = "the audio device stopped requesting data";
                break;
            }
            UINT32 padding = 0;
            hr = client->GetCurrentPadding(&padding);
            if (FAILED(hr)) break;
            if (padding == 0) ++st.underruns;  // the device played everything we gave it: a gap
            const UINT32 avail = bufFrames - std::min(padding, bufFrames);
            if (avail > 0) hr = fill(avail);
            if (FAILED(hr)) break;
        }
        if (FAILED(hr) && err.empty()) err = hrText("writing audio", hr);
        client->Stop();
        std::lock_guard<std::mutex> lock(mu_);
        if (!err.empty()) {
            error_ = err;
            failed_ = true;
        }
    }
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    {
        std::lock_guard<std::mutex> lock(mu_);
        stats_ = st;
    }

    release(render);
    release(client);
    release(dev);
    release(en);
    if (wf) CoTaskMemFree(wf);
    if (audioEvent) CloseHandle(audioEvent);
    if (SUCCEEDED(init)) CoUninitialize();
}
