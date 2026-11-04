#pragma once
// The engine between the KS-32 machine and a host's audio callback (Plan 7): it renders host-rate
// audio through the resampler, takes MIDI with block offsets (A3) and GUI events through a fixed
// queue (A4), and publishes a panel snapshot (A5, A6). JUCE-free and OS-free: no file I/O.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "app/spsc_ring.h"
#include "ks32/machine.h"
#include "player/resampler.h"

namespace ens::app {

struct Event {
    enum class Kind : uint8_t { Button, Key, Analog };
    Kind kind;
    uint8_t id;  // Button: ks32::Button code. Key: note. Analog: ks32::Analog.
    bool down;
    uint16_t value;  // Key: strike. Analog: raw.
};

struct Snapshot {
    ks32::PanelState panel{};
    bool booted = false;
    uint64_t frames = 0;       // machine frames since the last power-on (create or loadRam)
    uint32_t midiDropped = 0;  // MIDI-in bytes dropped: the block store (A3) plus the DUART (A1)
    uint64_t midiOutBytes = 0;  // MIDI-out bytes drained and discarded (A2)
    int16_t program = -1;       // the current sound 0-127; -1 until the boot ends (A15)
    std::array<uint8_t, 8> select{};  // the bytes that select it: bank select, then Cn pp (A15)
    uint8_t selectLen = 0;      // 0 until a selection has been seen or sent
    std::array<uint64_t, 2> notes{};  // MIDI-in note mask, bit n is note n (A19)
    uint32_t programsSent = 0;  // selections the engine has sent into MIDI in since create (A15)
};

class Engine {
public:
    static std::unique_ptr<Engine> create(const ks32::RomSet&, std::string& err);
    // Message thread, audio stopped:
    void prepare(double outRate, int maxBlock);  // allocates the resampler and buffers
    bool loadRam(const std::vector<uint8_t>& img, std::string& err);
    // The exact bytes to replay after this boot (A15), when they select the program setProgram asks
    // for, instead of a bare program change. Call after loadRam; up to 8 bytes.
    void setSelect(const uint8_t* bytes, int len);
    ks32::Machine& machine();  // tests, and the RAM at shut-down
    // Message thread, audio running:
    bool post(const Event&);  // false when the queue is full (A4)
    Snapshot snapshot() const;  // A5
    void requestRam();  // the next block copies the RAM
    bool takeRam(std::vector<uint8_t>& out);  // true once a requested copy is ready
    // Audio thread:
    void midi(int offset, const uint8_t* data, int size);  // before render, offsets in this block (A3)
    void render(float* l, float* r, int frames);  // any frames >= 0; pieces of maxBlock
    void setProgram(int n);                       // requests a sound 0-127 (A15)
    void setAnalog(ks32::Analog a, uint16_t raw);  // applied at a block start, once booted (A16)

private:
    explicit Engine(std::unique_ptr<ks32::Machine> m);
    void apply(const Event& e);
    void renderPiece(float* l, float* r, int frames);
    void feedMidi(uint64_t upTo);  // the stored bytes due before block frame upTo
    void blockStart();             // sends the requested sound, applies the analogs (A15, A16)
    void sendSelect(const uint8_t* b, int n);

    struct MidiByte {
        uint64_t frame;  // machine frame of the block it enters before (A3)
        uint8_t byte;
    };
    // A running-status MIDI parser for one stream: feed returns true when a message is complete.
    struct MidiParse {
        uint8_t status = 0, d[2] = {0, 0};
        int n = 0;
        bool sysex = false;
        bool feed(uint8_t b);
    };
    void track(const MidiParse& p, bool fromIn);  // a complete message: program, bank, notes

    static constexpr int kMidiStore = 1024;  // DECISION A3
    static constexpr size_t kEventQueue = 256;  // DECISION A4

    std::unique_ptr<ks32::Machine> m_;
    std::unique_ptr<player::Resampler> rs_;
    std::vector<float> out_;  // resampled frames not yet handed out, interleaved L R
    double outRate_ = 48000;
    int maxBlock_ = 0;
    uint64_t frames_ = 0;      // machine frames since power-on
    uint64_t blockFrame_ = 0;  // machine frames run in this block
    MidiByte midi_[kMidiStore];
    int midiCount_ = 0, midiNext_ = 0;
    uint32_t storeDropped_ = 0;
    uint64_t midiOut_ = 0;
    SpscRing<Event, kEventQueue> events_;
    Event held_{};  // popped, waiting for room on the panel link
    bool holding_ = false;
    mutable std::mutex snapMu_;
    Snapshot snap_;
    std::vector<uint8_t> ram_;
    // The sound (A15), reset by loadRam as a power cycle.
    MidiParse inParse_, outParse_;
    bool booted_ = false;  // the tracker's boot flag: frames_ reached kBootFrames
    int requested_ = -1, current_ = -1;
    bool dirty_ = false;
    uint8_t channel_ = 0;  // the channel last seen in a program change
    bool bankSeen_ = false;
    uint8_t bankMsb_ = 0, bankLsb_ = 0;
    std::array<uint8_t, 8> select_{};
    uint8_t selectLen_ = 0;
    std::array<uint8_t, 8> setSel_{};
    uint8_t setSelLen_ = 0;
    int setSelProgram_ = -1;
    uint32_t programsSent_ = 0;
    std::array<uint64_t, 2> notes_{};  // A19
    std::array<uint16_t, 7> wantAnalog_{};  // A16
    std::array<bool, 7> wantSet_{};
    std::atomic<bool> ramWanted_{false}, ramReady_{false};
};

}  // namespace ens::app
