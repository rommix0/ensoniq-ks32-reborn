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
    inParse_ = outParse_ = MidiParse{};
    booted_ = false;
    requested_ = current_ = -1;
    dirty_ = false;
    channel_ = 0;
    bankSeen_ = false;
    bankMsb_ = bankLsb_ = 0;
    selectLen_ = setSelLen_ = 0;
    setSelProgram_ = -1;
    notes_ = {};
    wantSet_ = {};
    // The machine keeps its analog inputs over a power cycle (K11); a boot must see the rest values (A16).
    for (size_t i = 0; i < wantAnalog_.size(); ++i) m_->setAnalog(static_cast<ks32::Analog>(i), ks32::kAnalogRest[i]);
    return true;
}

// DECISION A15: the engine tracks the program and bank on MIDI out and MIDI in after the boot, and
// sends the requested sound into MIDI in once, after the boot, if the machine is not already on it.
bool Engine::MidiParse::feed(uint8_t b) {
    if (b >= 0xF8) return false;  // real-time: running status stays
    if (b >= 0x80) {
        n = 0;
        sysex = b == 0xF0;
        status = b >= 0xF0 ? 0 : b;  // system messages end running status
        return false;
    }
    if (sysex || status == 0) return false;
    d[n++] = b;
    if (n < ((status & 0xE0) == 0xC0 ? 1 : 2)) return false;
    n = 0;
    return true;
}

void Engine::track(const MidiParse& p, bool fromIn) {
    const uint8_t type = p.status & 0xF0;
    if (fromIn) {  // DECISION A19: the mask follows note on/off on any channel; all notes off clears it
        auto& w = notes_[p.d[0] >> 6];
        const uint64_t bit = uint64_t{1} << (p.d[0] & 63);
        if (type == 0x90 && p.d[1] != 0) w |= bit;
        else if (type == 0x80 || type == 0x90) w &= ~bit;
        else if (type == 0xB0 && (p.d[0] == 0x7B || p.d[0] == 0x78)) notes_ = {};
    }
    if (!booted_) return;
    if (type == 0xB0 && (p.d[0] == 0x00 || p.d[0] == 0x20)) {
        (p.d[0] == 0 ? bankMsb_ : bankLsb_) = p.d[1];
        bankSeen_ = true;
    } else if (type == 0xC0) {
        current_ = p.d[0];
        channel_ = p.status & 0x0F;
        uint8_t n = 0;
        if (bankSeen_) {
            const uint8_t bank[] = {static_cast<uint8_t>(0xB0 | channel_), 0x00, bankMsb_, 0x20, bankLsb_};
            for (const uint8_t b : bank) select_[n++] = b;
        }
        select_[n++] = p.status;  // the status byte, also when the firmware sent the data alone
        select_[n++] = p.d[0];
        selectLen_ = n;
    }
}

// The bytes come from saved state, so only `Bn 00 xx 20 yy` (a bank select; each pair may repeat the
// `Bn`) and one final `Cn pp`, all on one channel and at most 8 bytes, are taken; anything else is
// ignored and a bare `Cn pp` is sent.
void Engine::setSelect(const uint8_t* bytes, int len) {
    setSelLen_ = 0;
    setSelProgram_ = -1;
    if (len < 2 || len > static_cast<int>(setSel_.size())) return;
    const uint8_t ch = bytes[0] & 0x0F;
    int i = 0;
    while (len - i > 2) {
        if (bytes[i] == (0xB0 | ch)) ++i;
        if (len - i <= 2 || (bytes[i] != 0x00 && bytes[i] != 0x20) || bytes[i + 1] >= 0x80) return;
        i += 2;
    }
    if (len - i != 2 || bytes[i] != (0xC0 | ch) || bytes[i + 1] >= 0x80) return;
    for (int k = 0; k < len; ++k) setSel_[static_cast<size_t>(k)] = bytes[k];
    setSelLen_ = static_cast<uint8_t>(len);
    setSelProgram_ = bytes[len - 1];
}

void Engine::setProgram(int n) {
    n = std::clamp(n, 0, 127);
    if (n != requested_) {  // DECISION A15: only a changed request is sent; panel picks stay
        requested_ = n;
        dirty_ = true;
    }
}

// DECISION A16: an analog set by the host is held and applied at a block start once booted, so the
// boot's own reads see the rest values (A6); only the inputs the host has set are touched.
void Engine::setAnalog(ks32::Analog a, uint16_t raw) {
    const auto i = static_cast<size_t>(a);
    if (i >= wantAnalog_.size()) return;
    wantAnalog_[i] = raw & 0x3FF;
    wantSet_[i] = true;
}

void Engine::sendSelect(const uint8_t* b, int n) {
    MidiParse p;  // the bytes go into the machine and through the tracker, as if sent from outside
    for (int i = 0; i < n; ++i) {
        m_->midiIn(b[i]);
        if (p.feed(b[i])) track(p, false);
    }
}

void Engine::blockStart() {
    if (!booted_ && frames_ >= kBootFrames) {
        booted_ = true;
        current_ = 0;  // OS v3.01 powers on to ROM 00 (Plan 6)
        dirty_ = true;
    }
    if (!booted_) return;
    if (dirty_) {
        if (requested_ >= 0 && requested_ != current_) {
            if (setSelLen_ > 0 && setSelProgram_ == requested_) {
                sendSelect(setSel_.data(), setSelLen_);
            } else {
                const uint8_t bare[] = {static_cast<uint8_t>(0xC0 | channel_), static_cast<uint8_t>(requested_)};
                sendSelect(bare, 2);
            }
            current_ = requested_;
            ++programsSent_;
        }
        dirty_ = false;
    }
    for (size_t i = 0; i < wantAnalog_.size(); ++i) {
        const auto a = static_cast<ks32::Analog>(i);
        if (wantSet_[i] && m_->analog(a) != wantAnalog_[i]) m_->setAnalog(a, wantAnalog_[i]);
    }
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
            if (e.id < std::size(ks32::kAnalogRest)) {
                m_->setAnalog(static_cast<ks32::Analog>(e.id), e.value);
                wantAnalog_[e.id] = e.value & 0x3FF;  // so a block start does not undo it (A16)
                wantSet_[e.id] = true;
            }
            break;
    }
}

void Engine::feedMidi(uint64_t upTo) {
    while (midiNext_ < midiCount_ && midi_[midiNext_].frame <= upTo) {
        const uint8_t b = midi_[midiNext_++].byte;
        m_->midiIn(b);
        if (inParse_.feed(b)) track(inParse_, true);
    }
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
    blockStart();
    blockFrame_ = 0;
    for (int done = 0; done < frames;) {
        const int n = std::min(frames - done, maxBlock_);
        renderPiece(l + done, r + done, n);
        done += n;
    }
    feedMidi(UINT64_MAX);  // bytes due after the frames this block ran
    midiCount_ = midiNext_ = 0;
    // DECISION A2: MIDI out is drained every block and discarded, counted.
    for (uint8_t b; m_->popMidiOut(b);) {
        ++midiOut_;
        if (outParse_.feed(b)) track(outParse_, false);
    }
    // DECISION A5: published under try_lock; the block is skipped when the GUI holds the lock.
    if (snapMu_.try_lock()) {
        snap_.panel = m_->panel();
        snap_.booted = frames_ >= kBootFrames;
        snap_.frames = frames_;
        snap_.midiDropped = storeDropped_ + m_->duart().rxDropped(ks32::kMidiChannel);
        snap_.midiOutBytes = midiOut_;
        snap_.program = static_cast<int16_t>(current_);
        snap_.select = select_;
        snap_.selectLen = selectLen_;
        snap_.notes = notes_;
        snap_.programsSent = programsSent_;
        snapMu_.unlock();
    }
    // The acquire on ramReady_ pairs with takeRam's release, so its reads of ram_ end before this
    // copy; the exchange takes the request first, so one made during the copy is kept.
    if (!ramReady_.load(std::memory_order_acquire) && ramWanted_.exchange(false, std::memory_order_acquire)) {
        std::copy(m_->ramImage().begin(), m_->ramImage().end(), ram_.begin());
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
