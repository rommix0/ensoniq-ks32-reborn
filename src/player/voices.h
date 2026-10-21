#pragma once
// Firmware stand-in: turns MIDI-style note events into ES5505 (OTIS) register writes.
// Pure layer: owns no chip, only a reference to one. Integer-only apart from makeFcTable().
#include <cstdint>
#include <vector>

#include "otis/otis.h"
#include "player/sample_map.h"

namespace ens::player {

struct Adsr {
    int attackMs = 5, decayMs = 0, sustainPct = 100, releaseMs = 250;
};

struct VoiceConfig {
    int bus = 1;  // 0..2; OTIS channel (CA) = bus + 1
    Adsr adsr;
    int bendSemis = 2;  // pitch bend range, +/- semitones
};

// 768 entries, round(1024 * 2^(i/768)): FC for one octave above the base, 4 pitch units per entry.
std::vector<uint16_t> makeFcTable();
// Pitch P (256 per semitone, 3072 per octave; 0 = FC 0x400, one sample per frame) to an FC word.
uint16_t fcForPitch(const std::vector<uint16_t>& table, int32_t p);

class Voices {
public:
    Voices(Otis& otis, std::vector<uint16_t> fcTable, VoiceConfig cfg);

    void init();                   // SERMODE = 0, ACT = 31, every voice stopped and silent
    void setWave(const Wave* w);   // used by later note-ons; nullptr = notes ignored
    void setBus(int bus);          // 0..2
    void noteOn(int ch, int key, int vel);
    void noteOff(int ch, int key);
    void sustain(int ch, bool down);
    void pitchBend(int ch, int value14);  // 0..16383, centre 8192; retunes sounding voices of ch
    void channelVolume(int ch, int cc7);
    void allNotesOff();            // release all (sustain cleared)
    void allSoundOff();            // cut all at once (volume 0, stopped, freed)
    void tick();                   // once per frame, before the OTIS frame
    int activeCount() const;

private:
    enum class Stage { Attack, Decay, Sustain, Release, Fade };
    struct Slot {
        bool active = false;
        bool held = false;  // note-off arrived while the pedal was down
        int ch = 0, key = 0, vel = 0;
        uint32_t age = 0;
        Stage stage = Stage::Attack;
        int peak = 0;       // peak volume byte
        int32_t level = 0;  // envelope, 8.8 fixed point of the volume byte
        int lastByte = 0;   // volume byte last written to LVOL/RVOL
        int basePitch = 0;  // key * 256 + zone.pitchWord
        uint16_t fc = 0;
    };

    void page(int p) { otis_.write(15, static_cast<uint16_t>(p)); }
    void start(int v, int ch, int key, int vel);
    void release(Slot& s);
    void writeVol(int v, int byte);
    void free(int v);
    void retune(int v);
    int pitchOf(const Slot& s) const;
    int pickVoice() const;

    Otis& otis_;
    std::vector<uint16_t> table_;
    VoiceConfig cfg_;
    const Wave* wave_ = nullptr;
    Slot slot_[32];
    int bend_[16];
    int cc7_[16];
    bool pedal_[16];
    uint32_t clock_ = 0;
};

}  // namespace ens::player
