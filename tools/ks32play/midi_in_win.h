#pragma once
// WinMM MIDI input for `ks32play play`. The driver callback (MIM_DATA) only stores each complete
// channel message in a fixed-size ring guarded by a mutex; system messages (status 0xF0 and up:
// clock, active sensing, sysex) are dropped there. The audio thread reads them with poll().
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class MidiIn {
public:
    MidiIn() = default;
    ~MidiIn() { close(); }
    MidiIn(const MidiIn&) = delete;
    MidiIn& operator=(const MidiIn&) = delete;

    static std::vector<std::string> devices();  // input device names (UTF-8), index = device id

    bool open(int index, std::string& err);
    void close();
    bool poll(uint32_t& msg);  // status | d1 << 8 | d2 << 16; false when the queue is empty

    uint64_t received() const;  // channel messages queued so far
    uint64_t dropped() const;   // channel messages lost because the queue was full

    void push(uint32_t msg);  // called by the driver callback only

private:
    static constexpr uint32_t kSize = 1024;  // power of two

    void* handle_ = nullptr;  // HMIDIIN
    mutable std::mutex mu_;
    uint32_t ring_[kSize] = {};
    uint32_t head_ = 0, tail_ = 0;  // head_ - tail_ = queued messages
    uint64_t received_ = 0, dropped_ = 0;
};
