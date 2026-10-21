#include "midi_in_win.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

namespace {

std::string toUtf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    s.resize(static_cast<size_t>(n - 1));  // drop the terminator
    return s;
}

// Runs on a WinMM thread: only queue the message (no Synth calls, no allocation).
void CALLBACK midiInProc(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR) {
    if (msg != MIM_DATA) return;
    const uint32_t m = static_cast<uint32_t>(param1);
    const uint32_t status = m & 0xFF;
    if (status < 0x80 || status >= 0xF0) return;  // system messages and stray data bytes
    reinterpret_cast<MidiIn*>(instance)->push(m & 0x7F7FFF);
}

}  // namespace

std::vector<std::string> MidiIn::devices() {
    std::vector<std::string> names;
    const UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof caps) == MMSYSERR_NOERROR)
            names.push_back(toUtf8(caps.szPname));
        else
            names.push_back("(unavailable)");
    }
    return names;
}

bool MidiIn::open(int index, std::string& err) {
    close();
    HMIDIIN h = nullptr;
    MMRESULT r = midiInOpen(&h, static_cast<UINT>(index), reinterpret_cast<DWORD_PTR>(&midiInProc),
                            reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
    if (r != MMSYSERR_NOERROR) {
        err = "cannot open MIDI input " + std::to_string(index) + " (midiInOpen error " + std::to_string(r) +
              (r == MMSYSERR_ALLOCATED ? ", in use by another program)" : ")");
        return false;
    }
    r = midiInStart(h);
    if (r != MMSYSERR_NOERROR) {
        midiInClose(h);
        err = "cannot start MIDI input " + std::to_string(index) + " (midiInStart error " + std::to_string(r) + ")";
        return false;
    }
    handle_ = h;
    return true;
}

void MidiIn::close() {
    if (!handle_) return;
    HMIDIIN h = static_cast<HMIDIIN>(handle_);
    midiInStop(h);
    midiInReset(h);
    midiInClose(h);
    handle_ = nullptr;
}

void MidiIn::push(uint32_t msg) {
    std::lock_guard<std::mutex> lock(mu_);
    if (head_ - tail_ >= kSize) {
        ++dropped_;
        return;
    }
    ring_[head_ % kSize] = msg;
    ++head_;
    ++received_;
}

bool MidiIn::poll(uint32_t& msg) {
    std::lock_guard<std::mutex> lock(mu_);
    if (head_ == tail_) return false;
    msg = ring_[tail_ % kSize];
    ++tail_;
    return true;
}

uint64_t MidiIn::received() const {
    std::lock_guard<std::mutex> lock(mu_);
    return received_;
}

uint64_t MidiIn::dropped() const {
    std::lock_guard<std::mutex> lock(mu_);
    return dropped_;
}
