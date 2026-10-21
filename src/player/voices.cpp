#include "player/voices.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace ens::player {

namespace {

constexpr int kPitchPerOctave = 3072;  // 12 semitones * 256
constexpr int kPitchMin = -0x4800, kPitchMax = 0x5400;
constexpr int kFramesPerSecond = 29762;
constexpr int kFadeMs = 2;
constexpr int32_t kFullScale = 0xFF00;  // 8.8 level of the loudest byte, 0xFF

enum : uint16_t { CrStop = 0x3, CrBs = 0x4, CrLp3 = 0x400, CrLp4 = 0x800 };

// DECISION P5: time base is the integer 29762 frames per 1000 ms (OTIS frame rate 29761.8 Hz,
// rounded); a non-zero time is at least one frame.
int msToFrames(int ms) {
    if (ms <= 0) return 0;
    return std::max<int>(1, static_cast<int>(static_cast<int64_t>(ms) * kFramesPerSecond / 1000));
}

// Mitchell log2 in 1/16 octave (n >= 1). The volume byte is itself a 4-bit exponent with a
// linear 4-bit mantissa, so one byte step is 1/16 octave on this scale (O5).
int lg16(uint32_t n) {
    int k = 0;
    while ((n >> (k + 1)) != 0) ++k;
    return k * 16 + static_cast<int>(((n - (1u << k)) * 16u) >> k);
}

// DECISION P6: peak volume byte = 0xFF minus the Mitchell-log attenuation of vel*cc7 against
// 127*127 (16 bytes per 6 dB; velocity and channel volume both act as amplitude); the sustain
// level is the peak minus the attenuation of sustainPct against 100. Zero velocity, cc7 or
// percent means silence.
int peakByte(int vel, int cc7) {
    if (vel <= 0 || cc7 <= 0) return 0;
    const int atten = lg16(127 * 127) - lg16(static_cast<uint32_t>(vel * cc7));
    return std::max(0, 0xFF - atten);
}
int sustainByte(int peak, int pct) {
    if (pct <= 0) return 0;
    if (pct >= 100) return peak;
    return std::max(0, peak - (lg16(100) - lg16(static_cast<uint32_t>(pct))));
}

int32_t ceilDiv(int32_t a, int32_t b) { return (a + b - 1) / b; }

}  // namespace

std::vector<uint16_t> makeFcTable() {
    // DECISION P2: FC table = round(1024 * 2^(i/768)) for i in 0..767 (formula; the firmware
    // table at 0xC09F64 is to be checked against it). 768 entries x 4 units = one octave.
    std::vector<uint16_t> t(768);
    for (int i = 0; i < 768; ++i)
        t[static_cast<size_t>(i)] = static_cast<uint16_t>(std::lround(1024.0 * std::exp2(i / 768.0)));
    return t;
}

uint16_t fcForPitch(const std::vector<uint16_t>& table, int32_t p) {
    // DECISION P3: P clamps to [-0x4800, 0x5400]; octave = floor(P / 3072), table index =
    // (P mod 3072) >> 2; FC is the table value shifted by the octave and clamped to 0xFFFE
    // (FC bit 0 is not implemented by the chip).
    p = std::clamp<int32_t>(p, kPitchMin, kPitchMax);
    const int32_t oct = p >= 0 ? p / kPitchPerOctave : -((-p + kPitchPerOctave - 1) / kPitchPerOctave);
    const int32_t rem = p - oct * kPitchPerOctave;
    const int64_t base = table[static_cast<size_t>(rem >> 2)];
    const int64_t fc = oct >= 0 ? base << oct : base >> -oct;
    return static_cast<uint16_t>(std::min<int64_t>(fc, 0xFFFE));
}

Voices::Voices(Otis& otis, std::vector<uint16_t> fcTable, VoiceConfig cfg)
    : otis_(otis), table_(std::move(fcTable)), cfg_(cfg) {
    cfg_.bus = std::clamp(cfg_.bus, 0, 2);
    std::fill(std::begin(bend_), std::end(bend_), 8192);
    std::fill(std::begin(cc7_), std::end(cc7_), 100);  // GM default channel volume
    std::fill(std::begin(pedal_), std::end(pedal_), false);
}

void Voices::init() {
    page(0x40);            // global page
    otis_.write(8, 0);     // SERMODE = 0 (findings C)
    otis_.write(13, 31);   // ACT = 31: all 32 voices scanned
    for (int v = 0; v < 32; ++v) {
        page(v);
        otis_.write(0, CrStop);
        otis_.write(8, 0);
        otis_.write(9, 0);
        otis_.write(6, 0xFFF0);
        otis_.write(7, 0xFFF0);
        slot_[v] = Slot();
    }
}

void Voices::setWave(const Wave* w) { wave_ = w; }
void Voices::setBus(int bus) { cfg_.bus = std::clamp(bus, 0, 2); }

int Voices::pitchOf(const Slot& s) const {
    // DECISION P4: P = key*256 + zone.pitchWord + bend, bend = (value14 - 8192) * (bendSemis*256)
    // / 8192 in integer arithmetic (truncation toward zero); fcForPitch clamps the sum.
    const int bend = (bend_[s.ch] - 8192) * (cfg_.bendSemis * 256) / 8192;
    return s.basePitch + bend;
}

int Voices::pickVoice() const {
    // DECISION P8: allocation takes the lowest free voice; with none free it steals the quietest
    // releasing (or fading) voice, else the oldest note.
    for (int v = 0; v < 32; ++v)
        if (!slot_[v].active) return v;
    int best = -1;
    for (int v = 0; v < 32; ++v) {
        const Slot& s = slot_[v];
        if ((s.stage == Stage::Release || s.stage == Stage::Fade) &&
            (best < 0 || s.level < slot_[best].level))
            best = v;
    }
    if (best >= 0) return best;
    best = 0;
    for (int v = 1; v < 32; ++v)
        if (slot_[v].age < slot_[best].age) best = v;
    return best;
}

void Voices::start(int v, int ch, int key, int vel) {
    const Zone& z = zoneForKey(*wave_, key);
    Slot& s = slot_[v];
    s = Slot();
    s.active = true;
    s.ch = ch;
    s.key = key;
    s.vel = vel;
    s.age = ++clock_;
    s.stage = Stage::Attack;
    s.peak = peakByte(vel, cc7_[ch]);
    s.basePitch = key * 256 + z.pitchWord;
    s.fc = fcForPitch(table_, pitchOf(s));

    // Firmware voice-start order (findings "Voice start").
    const uint16_t cr = static_cast<uint16_t>(z.crLow | ((z.bank & 1) ? CrBs : 0) |
                                              ((cfg_.bus + 1) << 8) | CrLp3 | CrLp4);
    page(v);
    otis_.write(8, 0);
    otis_.write(9, 0);
    otis_.write(0, cr);
    otis_.write(7, 0xFFF0);
    otis_.write(6, 0xFFF0);
    page(v | 0x20);
    for (int r = 1; r <= 6; ++r) otis_.write(static_cast<uint8_t>(r), 0);
    page(v);
    auto addr = [&](uint32_t word, int reg, uint16_t lowBits) {
        const uint32_t a29 = word << 9;
        otis_.write(static_cast<uint8_t>(reg), static_cast<uint16_t>(a29 >> 16));
        otis_.write(static_cast<uint8_t>(reg + 1), static_cast<uint16_t>((a29 & 0xFFFF) | lowBits));
    };
    addr(z.loopStart, 2, 0);                                           // START
    addr(z.end, 4, static_cast<uint16_t>((z.endFrac << 5) & 0xFFE0));  // END (+ fraction)
    addr(z.start, 10, 0);                                              // ACC
    otis_.write(1, s.fc);
    otis_.write(0, static_cast<uint16_t>(cr & ~CrStop));
}

void Voices::noteOn(int ch, int key, int vel) {
    ch &= 15;
    if (vel <= 0) {
        noteOff(ch, key);
        return;
    }
    if (!wave_ || wave_->zones.empty()) return;
    start(pickVoice(), ch, std::clamp(key, 0, 127), std::min(vel, 127));
}

void Voices::release(Slot& s) {
    s.held = false;
    if (s.stage != Stage::Fade) s.stage = Stage::Release;
}

void Voices::noteOff(int ch, int key) {
    ch &= 15;
    // DECISION P8: a note-off releases every sounding, unreleased voice of that channel and key
    // (a retriggered key has several), or marks them held while the sustain pedal is down.
    for (Slot& s : slot_) {
        if (!s.active || s.ch != ch || s.key != key) continue;
        if (s.stage == Stage::Release || s.stage == Stage::Fade || s.held) continue;
        if (pedal_[ch]) s.held = true;
        else release(s);
    }
}

void Voices::sustain(int ch, bool down) {
    ch &= 15;
    pedal_[ch] = down;
    if (down) return;
    for (Slot& s : slot_)
        if (s.active && s.ch == ch && s.held) release(s);
}

void Voices::retune(int v) {
    Slot& s = slot_[v];
    const uint16_t fc = fcForPitch(table_, pitchOf(s));
    if (fc == s.fc) return;
    s.fc = fc;
    page(v);
    otis_.write(1, fc);
}

void Voices::pitchBend(int ch, int value14) {
    ch &= 15;
    bend_[ch] = std::clamp(value14, 0, 16383);
    for (int v = 0; v < 32; ++v)
        if (slot_[v].active && slot_[v].ch == ch) retune(v);
}

void Voices::channelVolume(int ch, int cc7) {
    ch &= 15;
    cc7_[ch] = std::clamp(cc7, 0, 127);
    // Sounding voices take the new peak at once; their sustain level follows (a step, not a ramp).
    for (Slot& s : slot_)
        if (s.active && s.ch == ch) s.peak = peakByte(s.vel, cc7_[ch]);
}

void Voices::allNotesOff() {
    std::fill(std::begin(pedal_), std::end(pedal_), false);
    for (Slot& s : slot_)
        if (s.active) release(s);
}

void Voices::writeVol(int v, int byte) {
    page(v);
    otis_.write(8, static_cast<uint16_t>(byte << 8));
    otis_.write(9, static_cast<uint16_t>(byte << 8));
    slot_[v].lastByte = byte;
}

void Voices::free(int v) {
    page(v);
    otis_.write(8, 0);
    otis_.write(9, 0);
    otis_.write(0, CrStop);  // a stopped voice still outputs its held sample (O7), hence volume 0 too
    slot_[v] = Slot();
}

void Voices::allSoundOff() {
    for (int v = 0; v < 32; ++v)
        if (slot_[v].active) free(v);
}

void Voices::tick() {
    const Adsr& a = cfg_.adsr;
    for (int v = 0; v < 32; ++v) {
        Slot& s = slot_[v];
        if (!s.active) continue;

        // DECISION P9: a one-shot that reached END sets STOP0 (O1) but keeps outputting its held
        // sample (O7); detect the stop bits and fade out over kFadeMs instead of clicking, then free.
        if (s.stage != Stage::Release && s.stage != Stage::Fade) {
            page(v);
            if (otis_.read(0) & CrStop) {
                s.stage = Stage::Fade;
                s.held = false;
            }
        }

        // DECISION P7: the envelope is integer and linear in the volume byte (8.8 fixed point), so
        // it is a straight ramp in dB: attack to peak, decay to the sustain level, hold, then release
        // at a constant byte rate (full scale in releaseMs) until level 0, which frees the voice.
        const int32_t peakL = s.peak << 8;
        const int32_t susL = sustainByte(s.peak, a.sustainPct) << 8;
        bool done = false;
        switch (s.stage) {
            case Stage::Attack: {
                const int n = msToFrames(a.attackMs);
                s.level = n == 0 ? peakL : std::min(peakL, s.level + ceilDiv(peakL, n));
                if (s.level < peakL) break;
                s.stage = Stage::Decay;
                [[fallthrough]];
            }
            case Stage::Decay: {
                const int n = msToFrames(a.decayMs);
                if (n == 0 || s.level <= susL) {
                    s.level = susL;
                    s.stage = Stage::Sustain;
                } else {
                    s.level = std::max(susL, s.level - ceilDiv(std::max<int32_t>(peakL - susL, 1), n));
                    if (s.level == susL) s.stage = Stage::Sustain;
                }
                break;
            }
            case Stage::Sustain: s.level = susL; break;
            case Stage::Release:
            case Stage::Fade: {
                const int n = std::max(1, msToFrames(s.stage == Stage::Fade ? kFadeMs : a.releaseMs));
                s.level -= ceilDiv(kFullScale, n);
                if (s.level <= 0) done = true;
                break;
            }
        }
        if (done) {
            free(v);
            continue;
        }
        const int byte = std::clamp(s.level >> 8, 0, 255);
        if (byte != s.lastByte) writeVol(v, byte);
    }
}

int Voices::activeCount() const {
    int n = 0;
    for (const Slot& s : slot_) n += s.active ? 1 : 0;
    return n;
}

}  // namespace ens::player
