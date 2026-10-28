#include "app/engine.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace ens::app {

namespace {
// DECISION A6: booted once the engine has run kBootMs of emulated time since power-on.
constexpr uint64_t kBootFrames = static_cast<uint64_t>(ks32::kBootMs) * ks32::kWavRate / 1000;
}  // namespace

std::unique_ptr<Engine> Engine::create(const ks32::RomSet& roms, std::string& err) {
    auto m = ks32::Machine::create(roms, err);
    if (!m) return nullptr;
    return std::unique_ptr<Engine>(new Engine(std::move(m)));
}

Engine::Engine(std::unique_ptr<ks32::Machine> m) : m_(std::move(m)), ram_(m_->ramImage().size()) {}

void Engine::prepare(double outRate, int maxBlock) {
    outRate_ = outRate;
    maxBlock_ = std::max(1, maxBlock);
    rs_ = std::make_unique<player::Resampler>(ks32::kWavRate, outRate, 2);
    // The machine is fed one frame at a time, so out_ never holds more than a piece plus the
    // frames one input frame completes: reserved here, appending never reallocates.
    const auto perFrame = static_cast<size_t>(std::ceil(outRate / ks32::kWavRate)) + 1;
    out_.clear();
    out_.reserve(2 * (static_cast<size_t>(maxBlock_) + perFrame));
}

bool Engine::loadRam(const std::vector<uint8_t>& img, std::string& err) {
    if (!m_->loadRamImage(img, err)) return false;
    frames_ = 0;  // a power cycle: the boot starts again
    return true;
}

ks32::Machine& Engine::machine() { return *m_; }

// DECISION A4: the queue holds 256 events; they are applied at the start of the next block, in
// post order.
bool Engine::post(const Event& e) { return events_.push(e); }

Snapshot Engine::snapshot() const {
    std::lock_guard<std::mutex> lock(snapMu_);
    return snap_;
}

void Engine::requestRam() { ramWanted_.store(true, std::memory_order_release); }

bool Engine::takeRam(std::vector<uint8_t>& out) {
    if (!ramReady_.load(std::memory_order_acquire)) return false;
    out.assign(ram_.begin(), ram_.end());
    ramReady_.store(false, std::memory_order_release);
    return true;
}

// DECISION A3: a byte at offset o enters before machine frame floor(o * 29762 / outRate) of the
// block; the store holds 1024 bytes a block, and the rest are dropped and counted.
void Engine::midi(int offset, const uint8_t* data, int size) {
    const auto at = offset > 0 ? static_cast<uint64_t>(offset * static_cast<double>(ks32::kWavRate) / outRate_) : 0;
    for (int i = 0; i < size; ++i) {
        if (midiCount_ == kMidiStore) {
            storeDropped_ += static_cast<uint32_t>(size - i);
            return;
        }
        midi_[midiCount_++] = {at, data[i]};
    }
}

void Engine::apply(const Event& e) {
    switch (e.kind) {
        case Event::Kind::Button:
            m_->button(static_cast<ks32::Button>(e.id), e.down);
            break;
        case Event::Kind::Key:
            m_->key(e.id, e.down, static_cast<uint8_t>(std::min<uint16_t>(e.value, 127)));
            break;
        case Event::Kind::Analog:
            if (e.id < std::size(ks32::kAnalogRest)) m_->setAnalog(static_cast<ks32::Analog>(e.id), e.value);
            break;
    }
}

void Engine::feedMidi(uint64_t upTo) {
    while (midiNext_ < midiCount_ && midi_[midiNext_].frame <= upTo) m_->midiIn(midi_[midiNext_++].byte);
}

void Engine::render(float* l, float* r, int frames) {
    if (!rs_) {  // not prepared
        std::fill_n(l, std::max(0, frames), 0.0f);
        std::fill_n(r, std::max(0, frames), 0.0f);
        return;
    }
    // DECISION A4: a button or key goes on the panel link only when it has room for both bytes,
    // so an event is never split; the rest wait for the next block, in order.
    for (;;) {
        if (!holding_ && !events_.pop(held_)) break;
        holding_ = true;
        if (held_.kind != Event::Kind::Analog && m_->panelRoom() < 2) break;
        apply(held_);
        holding_ = false;
    }
    blockFrame_ = 0;
    for (int done = 0; done < frames;) {
        const int n = std::min(frames - done, maxBlock_);
        renderPiece(l + done, r + done, n);
        done += n;
    }
    feedMidi(UINT64_MAX);  // bytes due after the frames this block ran
    midiCount_ = midiNext_ = 0;
    // DECISION A2: MIDI out is drained every block and discarded, counted.
    for (uint8_t b; m_->popMidiOut(b);) ++midiOut_;
    // DECISION A5: published under try_lock; the block is skipped when the GUI holds the lock.
    if (snapMu_.try_lock()) {
        snap_.panel = m_->panel();
        snap_.booted = frames_ >= kBootFrames;
        snap_.frames = frames_;
        snap_.midiDropped = storeDropped_ + m_->duart().rxDropped(ks32::kMidiChannel);
        snap_.midiOutBytes = midiOut_;
        snapMu_.unlock();
    }
    if (ramWanted_.load(std::memory_order_acquire) && !ramReady_.load(std::memory_order_relaxed)) {
        std::copy(m_->ramImage().begin(), m_->ramImage().end(), ram_.begin());
        ramWanted_.store(false, std::memory_order_relaxed);
        ramReady_.store(true, std::memory_order_release);
    }
}

void Engine::renderPiece(float* l, float* r, int frames) {
    const auto want = 2 * static_cast<size_t>(frames);
    while (out_.size() < want) {
        feedMidi(blockFrame_);
        int16_t s[2];
        m_->runFrame(s[0], s[1]);
        ++blockFrame_;
        ++frames_;
        rs_->process(s, 1, out_);
    }
    for (int i = 0; i < frames; ++i) {
        l[i] = out_[2 * static_cast<size_t>(i)];
        r[i] = out_[2 * static_cast<size_t>(i) + 1];
    }
    out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(want));  // the leftover stays
}

}  // namespace ens::app
