#include <cstdint>
#include <string>
#include <vector>

#include "check.h"
#include "player/smf.h"
#include "player/wav.h"

using namespace ens::player;

namespace {

using Bytes = std::vector<uint8_t>;

void add(Bytes& o, const Bytes& more) { o.insert(o.end(), more.begin(), more.end()); }

void be32(Bytes& o, uint32_t v) {
    for (int i = 3; i >= 0; --i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

Bytes header(int format, int ntrks, int ppq) {
    Bytes o = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, static_cast<uint8_t>(format), 0, static_cast<uint8_t>(ntrks),
               static_cast<uint8_t>(ppq >> 8), static_cast<uint8_t>(ppq)};
    return o;
}

Bytes track(const Bytes& body) {
    Bytes o = {'M', 'T', 'r', 'k'};
    be32(o, static_cast<uint32_t>(body.size()));
    add(o, body);
    return o;
}

const Bytes kEndOfTrack = {0x00, 0xFF, 0x2F, 0x00};

}  // namespace

TEST(smf_tempo_and_two_tracks) {
    Bytes f = header(1, 2, 480);
    Bytes t0 = {0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20};
    add(t0, kEndOfTrack);
    Bytes t1 = {0x83, 0x60, 0x90, 0x3C, 0x64};  // delta 480
    add(t1, kEndOfTrack);
    add(f, track(t0));
    add(f, track(t1));
    std::vector<MidiEvent> ev;
    std::string err;
    CHECK(parseSmf(f, ev, err));
    CHECK_EQ(ev.size(), 1u);
    if (ev.size() != 1) return;
    CHECK_EQ(ev[0].frame, 14880u);
    CHECK_EQ(ev[0].status, 0x90);
    CHECK_EQ(ev[0].d1, 0x3C);
    CHECK_EQ(ev[0].d2, 0x64);
}

TEST(smf_running_status) {
    Bytes f = header(0, 1, 480);
    Bytes t = {0x00, 0x90, 0x3C, 0x64,  // note on
               0x00, 0x3E, 0x50,        // running status: second note on
               0x00, 0xC1, 0x07,        // program change (one data byte)
               0x00, 0x3F};             // running status again: program change 0x3F
    add(t, kEndOfTrack);
    add(f, track(t));
    std::vector<MidiEvent> ev;
    std::string err;
    CHECK(parseSmf(f, ev, err));
    CHECK_EQ(ev.size(), 4u);
    if (ev.size() != 4) return;
    CHECK_EQ(ev[0].status, 0x90);
    CHECK_EQ(ev[1].status, 0x90);
    CHECK_EQ(ev[1].d1, 0x3E);
    CHECK_EQ(ev[1].d2, 0x50);
    CHECK_EQ(ev[2].status, 0xC1);
    CHECK_EQ(ev[2].d1, 0x07);
    CHECK_EQ(ev[2].d2, 0);
    CHECK_EQ(ev[3].status, 0xC1);
    CHECK_EQ(ev[3].d1, 0x3F);
    CHECK_EQ(ev[3].d2, 0);
}

TEST(smf_tempo_change_midway) {
    Bytes f = header(1, 2, 480);
    Bytes t0 = {0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,  // 500000
                0x83, 0x60, 0xFF, 0x51, 0x03, 0x03, 0xD0, 0x90};  // tick 480: 250000
    add(t0, kEndOfTrack);
    Bytes t1 = {0x87, 0x40, 0x90, 0x3C, 0x64};  // tick 960
    add(t1, kEndOfTrack);
    add(f, track(t0));
    add(f, track(t1));
    std::vector<MidiEvent> ev;
    std::string err;
    CHECK(parseSmf(f, ev, err));
    CHECK_EQ(ev.size(), 1u);
    if (ev.size() != 1) return;
    // 480 * 500000 + 480 * 250000 = 360e6 / 480 = 750000 us; floored whole us, then frames.
    CHECK_EQ(ev[0].frame, 22321u);
}

TEST(smf_merges_tracks_stable_and_defaults_tempo) {
    // No tempo event: 500000. Same-tick events keep track order, then file order.
    Bytes f = header(1, 2, 96);
    Bytes t0 = {0x60, 0x90, 0x40, 0x01, 0x00, 0x90, 0x41, 0x02};  // tick 96 twice
    add(t0, kEndOfTrack);
    Bytes t1 = {0x30, 0x90, 0x42, 0x03, 0x30, 0x90, 0x43, 0x04};  // tick 48, tick 96
    add(t1, kEndOfTrack);
    add(f, track(t0));
    add(f, track(t1));
    std::vector<MidiEvent> ev;
    std::string err;
    CHECK(parseSmf(f, ev, err));
    CHECK_EQ(ev.size(), 4u);
    if (ev.size() != 4) return;
    CHECK_EQ(ev[0].d1, 0x42);  // tick 48
    CHECK_EQ(ev[1].d1, 0x40);  // tick 96, track 0
    CHECK_EQ(ev[2].d1, 0x41);
    CHECK_EQ(ev[3].d1, 0x43);  // tick 96, track 1
    CHECK_EQ(ev[1].frame, 14880u);  // 500000 us * 30476180 / 1024e6
    CHECK_EQ(ev[0].frame, 7440u);
}

TEST(smf_skips_meta_and_sysex) {
    Bytes f = header(0, 1, 480);
    Bytes t = {0x00, 0xFF, 0x03, 0x02, 'a', 'b',                // track name
               0x00, 0xF0, 0x03, 0x01, 0x02, 0xF7,              // sysex
               0x00, 0x90, 0x3C, 0x64,
               0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08,  // time signature
               0x00, 0xE0, 0x00, 0x40};
    add(t, kEndOfTrack);
    add(f, track(t));
    std::vector<MidiEvent> ev;
    std::string err;
    CHECK(parseSmf(f, ev, err));
    CHECK_EQ(ev.size(), 2u);
    if (ev.size() != 2) return;
    CHECK_EQ(ev[1].status, 0xE0);
    CHECK_EQ(ev[1].d2, 0x40);
}

TEST(smf_rejects_bad_headers) {
    std::vector<MidiEvent> ev;
    std::string err;
    Bytes body = kEndOfTrack;

    Bytes f2 = header(2, 1, 480);  // format 2
    add(f2, track(body));
    CHECK(!parseSmf(f2, ev, err));
    CHECK(!err.empty());

    Bytes smpte = header(0, 1, 0xE728);  // SMPTE division (bit 15 set)
    add(smpte, track(body));
    CHECK(!parseSmf(smpte, ev, err));
    CHECK(err.find("SMPTE") != std::string::npos);

    Bytes noMthd = header(0, 1, 480);
    noMthd[0] = 'X';
    add(noMthd, track(body));
    CHECK(!parseSmf(noMthd, ev, err));

    Bytes noMtrk = header(0, 1, 480);
    Bytes bad = track(body);
    bad[0] = 'X';
    add(noMtrk, bad);
    CHECK(!parseSmf(noMtrk, ev, err));

    Bytes orphan = header(0, 1, 480);  // data byte with no running status
    add(orphan, track({0x00, 0x3C, 0x64, 0x00, 0xFF, 0x2F, 0x00}));
    CHECK(!parseSmf(orphan, ev, err));
}

TEST(smf_rejects_truncations_without_crash) {
    Bytes f = header(1, 2, 480);
    Bytes t0 = {0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x00, 0xF0, 0x02, 0x7D, 0xF7};
    add(t0, kEndOfTrack);
    Bytes t1 = {0x83, 0x60, 0x90, 0x3C, 0x64, 0x00, 0x3E, 0x64, 0x00, 0xC0, 0x05};
    add(t1, kEndOfTrack);
    add(f, track(t0));
    add(f, track(t1));
    std::vector<MidiEvent> ev;
    std::string err;
    CHECK(parseSmf(f, ev, err));
    for (size_t n = 0; n < f.size(); ++n) {
        const Bytes prefix(f.begin(), f.begin() + static_cast<std::ptrdiff_t>(n));
        err.clear();
        CHECK(!parseSmf(prefix, ev, err));
        CHECK(!err.empty());
    }
    // A chunk length that points past the end, and an event cut off inside its own chunk.
    Bytes lie = header(0, 1, 480);
    add(lie, Bytes{'M', 'T', 'r', 'k', 0x7F, 0xFF, 0xFF, 0xFF, 0x00, 0x90, 0x3C, 0x64});
    CHECK(!parseSmf(lie, ev, err));
    Bytes cut = header(0, 1, 480);
    add(cut, track({0x00, 0x90, 0x3C}));  // chunk is complete but the note-on is not
    CHECK(!parseSmf(cut, ev, err));
}

TEST(wav_header) {
    const std::vector<int16_t> pcm = {1, -2, 0x1234, -32768};
    const Bytes w = makeWav16(pcm, 2, 29762);
    CHECK_EQ(w.size(), 52u);
    if (w.size() != 52) return;
    auto tag = [&](size_t at, const char* s) { return std::string(w.begin() + static_cast<std::ptrdiff_t>(at), w.begin() + static_cast<std::ptrdiff_t>(at) + 4) == s; };
    auto u16 = [&](size_t at) { return static_cast<uint32_t>(w[at] | w[at + 1] << 8); };
    auto u32 = [&](size_t at) { return u16(at) | u16(at + 2) << 16; };
    CHECK(tag(0, "RIFF"));
    CHECK_EQ(u32(4), 44u);
    CHECK(tag(8, "WAVE"));
    CHECK(tag(12, "fmt "));
    CHECK_EQ(u32(16), 16u);
    CHECK_EQ(u16(20), 1u);
    CHECK_EQ(u16(22), 2u);
    CHECK_EQ(u32(24), 29762u);
    CHECK_EQ(u32(28), 119048u);
    CHECK_EQ(u16(32), 4u);
    CHECK_EQ(u16(34), 16u);
    CHECK(tag(36, "data"));
    CHECK_EQ(u32(40), 8u);
    CHECK_EQ(u16(44), 1u);
    CHECK_EQ(u16(46), 0xFFFEu);
    CHECK_EQ(u16(48), 0x1234u);
    CHECK_EQ(u16(50), 0x8000u);
}
