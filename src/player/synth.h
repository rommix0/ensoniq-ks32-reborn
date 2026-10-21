#pragma once
// MIDI-driven KS-32 synthesiser: owns the board (OTIS + ESP + DRAM), the ESP effect sequencer and
// the voice manager, takes MIDI short messages and renders stereo frames at the chip rate
// (30476180 / 1024 Hz). Pure: no OS code, no threads. Not copyable or movable (the voice manager
// and sequencer refer to the board, and the voices to the sample map).
#include <cstdint>
#include <string>
#include <vector>

#include "ks32/board.h"
#include "player/effects.h"
#include "player/esp_loader.h"
#include "player/sample_map.h"
#include "player/voices.h"

namespace ens::player {

struct SynthConfig {
    VoiceConfig voice;
    int effect = 1;   // effect index 0..10 queued at start
    int waveSet = 0;  // 0 internal, 1 expansion ROM 3
    int wave = 0;
};

class Synth {
public:
    // Queues the ESP boot and effect cfg.effect and selects the wave cfg.waveSet:cfg.wave. A missing
    // effect or wave is logged (takeLog) and nothing is loaded / the previous selection is kept.
    Synth(std::vector<uint16_t> bank0, std::vector<uint16_t> bank1, SampleMap map, EffectLibrary fx,
          std::vector<uint16_t> fcTable, SynthConfig cfg);
    Synth(const Synth&) = delete;
    Synth& operator=(const Synth&) = delete;

    void midi(uint8_t status, uint8_t d1, uint8_t d2);  // one complete short message (DECISION P10)
    void render(int16_t* interleavedLR, int frames);    // per frame: seq.run, voices.tick, board.frame
    int activeVoices() const { return voices_.activeCount(); }
    std::vector<std::string> takeLog();  // sequencer log plus wave / effect / controller messages

private:
    void selectWave(int set, int number);
    void loadEffect(int index);

    ks32::Board board_;
    SampleMap map_;
    EffectLibrary fx_;
    Voices voices_;
    EspSequencer seq_;
    std::vector<std::string> log_;
    int bank_ = 0;  // CC0 value, applied at the next program change
};

}  // namespace ens::player
