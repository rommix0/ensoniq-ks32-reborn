#pragma once
// MIDI-driven KS-32 synthesiser: owns the board (OTIS + ESP + DRAM), the ESP effect sequencer and
// the voice manager, takes MIDI short messages and renders stereo frames at the chip rate
// (30476180 / 1024 Hz). Pure: no OS code, no threads. Not copyable or movable (the voice manager
// and sequencer refer to the board, and the voices to the sample map).
//
// Real-time contract: render() does not allocate, except that the sequencer appends one log line
// when an effect load or boot finishes, and that it queues a pending effect change (DECISION P17)
// when the load in progress ends: the thousands of ESP host writes of one effect load, once per
// effect change. midi() may allocate: CC102 queues such a load at once when no load is running,
// and program change and out-of-range messages build log strings. A real-time caller accepts that
// burst; it happens only on those messages, and a burst of CC102 costs at most two loads. The log
// keeps the newest 256 lines; call takeLog() regularly (older lines are dropped and counted).
#include <cstdint>
#include <deque>
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
    // Per frame: start a pending effect change (P17), seq.run, voices.tick, board.frame.
    void render(int16_t* interleavedLR, int frames);
    int activeVoices() const { return voices_.activeCount(); }
    // True once the ESP boot and every queued effect change have finished (the effect output is
    // faded in). Offline renders run the synth until ready() before their first frame.
    bool ready() const { return seq_.idle() && pending_ < 0; }
    // The chips, for tests and diagnostics (reading chip state between render() calls).
    ks32::Board& board() { return board_; }
    // Wave / effect / controller messages and the sequencer log, in the order produced (sequencer
    // lines are merged at the end of each render() call). If more than 256 lines piled up, the
    // first line is "(N log lines dropped)" and the oldest lines are gone.
    std::vector<std::string> takeLog();

private:
    void pushLog(std::string line);
    void drainSeqLog();
    void selectWave(int set, int number);
    void loadEffect(int index, bool queueNow = false);  // queue, or make it the pending change (P17)
    void startPending();  // queue the pending change once the sequencer is idle

    ks32::Board board_;
    SampleMap map_;
    EffectLibrary fx_;
    Voices voices_;
    EspSequencer seq_;
    std::deque<std::string> log_;
    uint32_t dropped_ = 0;
    int bank_ = 0;  // CC0 value, applied at the next program change
    int pending_ = -1;  // DECISION P17: effect queued behind the load in progress, -1 = none
};

}  // namespace ens::player
