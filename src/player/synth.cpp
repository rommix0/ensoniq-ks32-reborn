#include "player/synth.h"

#include <utility>

namespace ens::player {

Synth::Synth(std::vector<uint16_t> bank0, std::vector<uint16_t> bank1, SampleMap map, EffectLibrary fx,
             std::vector<uint16_t> fcTable, SynthConfig cfg)
    : board_(std::move(bank0), std::move(bank1)),
      map_(std::move(map)),
      fx_(std::move(fx)),
      voices_(board_.otis(), std::move(fcTable), cfg.voice),
      bank_(cfg.waveSet) {
    voices_.init();
    seq_.queueBoot();
    loadEffect(cfg.effect, true);  // straight behind the boot
    selectWave(cfg.waveSet, cfg.wave);
}

void Synth::pushLog(std::string line) {
    constexpr size_t kMaxLog = 256;
    if (log_.size() >= kMaxLog) {
        log_.pop_front();
        ++dropped_;
    }
    log_.push_back(std::move(line));
}

void Synth::drainSeqLog() {
    for (std::string& s : seq_.takeLog()) pushLog(std::move(s));
}

void Synth::loadEffect(int index, bool queueNow) {
    const EspProgram* p = fx_.find(index);
    if (!p) {
        pushLog("effect " + std::to_string(index) + " not found; nothing loaded");
        return;
    }
    // DECISION P17: at most one load waits behind the one in progress. A change while a load
    // (or the boot) runs replaces the waiting program instead of queueing another full load;
    // render() starts it when the sequencer is idle, with the usual fade out, load, fade in.
    if (queueNow || (seq_.idle() && pending_ < 0)) seq_.queueLoad(*p, fx_.fadeFrames);
    else pending_ = index;
}

void Synth::startPending() {
    if (pending_ < 0 || !seq_.idle()) return;
    const EspProgram* p = fx_.find(pending_);
    pending_ = -1;
    if (p) seq_.queueLoad(*p, fx_.fadeFrames);
}

void Synth::selectWave(int set, int number) {
    const std::string id = std::to_string(set) + ":" + std::to_string(number);
    const Wave* w = map_.find(set, number);
    if (!w) {
        pushLog("wave " + id + " has no zones; ignored");
        return;
    }
    voices_.setWave(w);
    pushLog("wave " + id + (w->name.empty() ? std::string() : " '" + w->name + "'"));
}

void Synth::midi(uint8_t status, uint8_t d1, uint8_t d2) {
    // DECISION P10: omni (the channel only separates sustain, bend and volume state). Note on/off,
    // pitch bend, program change = wave number in the bank last set by CC0 (0 internal, 1 rom3;
    // applied at the program change), CC7 volume, CC64 sustain (>= 64 down), CC102 effect 0..10
    // (queues a load, P17), CC103 bus 0..2, CC120 all sound off, CC123 all notes off. Everything else
    // is ignored.
    const int ch = status & 0x0F;
    d1 &= 0x7F;
    d2 &= 0x7F;
    switch (status & 0xF0) {
    case 0x80:
        voices_.noteOff(ch, d1);
        break;
    case 0x90:
        if (d2 == 0) voices_.noteOff(ch, d1);
        else voices_.noteOn(ch, d1, d2);
        break;
    case 0xB0:
        switch (d1) {
        case 0:
            if (d2 <= 1) bank_ = d2;
            else pushLog("bank " + std::to_string(d2) + " ignored");
            break;
        case 7:
            voices_.channelVolume(ch, d2);
            break;
        case 64:
            voices_.sustain(ch, d2 >= 64);
            break;
        case 102:
            if (d2 <= 10) loadEffect(d2);
            else pushLog("effect " + std::to_string(d2) + " out of range; ignored");
            break;
        case 103:
            if (d2 <= 2) voices_.setBus(d2);
            else pushLog("bus " + std::to_string(d2) + " out of range; ignored");
            break;
        case 120:
            voices_.allSoundOff();
            break;
        case 123:
            voices_.allNotesOff();
            break;
        default:
            break;
        }
        break;
    case 0xC0:
        selectWave(bank_, d1);
        break;
    case 0xE0:
        voices_.pitchBend(ch, d1 | (d2 << 7));
        break;
    default:
        break;
    }
}

void Synth::render(int16_t* interleavedLR, int frames) {
    for (int i = 0; i < frames; ++i) {
        startPending();
        seq_.run(board_);
        voices_.tick();
        board_.frame(interleavedLR[2 * i], interleavedLR[2 * i + 1]);
    }
    drainSeqLog();
}

std::vector<std::string> Synth::takeLog() {
    drainSeqLog();
    std::vector<std::string> out;
    if (dropped_) out.push_back("(" + std::to_string(dropped_) + " log lines dropped)");
    for (std::string& l : log_) out.push_back(std::move(l));
    log_.clear();
    dropped_ = 0;
    return out;
}

}  // namespace ens::player
