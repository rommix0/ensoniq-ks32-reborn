#include "player/smf.h"

#include <algorithm>
#include <cstddef>

namespace ens::player {

namespace {

constexpr uint64_t kChipHz = 30476180;  // frame = us * 30476180 / (1024 * 10^6)
constexpr uint64_t kFrameDen = 1024000000;
constexpr uint64_t kMaxTicks = 0xFFFFFFFFull;
constexpr uint32_t kDefaultTempo = 500000;

// Bounds-checked cursor over b[pos, end).
struct Reader {
    const std::vector<uint8_t>& b;
    size_t pos, end;
    size_t left() const { return end - pos; }
    bool u8(uint8_t& v) {
        if (left() < 1) return false;
        v = b[pos++];
        return true;
    }
    bool u16(uint32_t& v) {
        if (left() < 2) return false;
        v = static_cast<uint32_t>(b[pos]) << 8 | b[pos + 1];
        pos += 2;
        return true;
    }
    bool u32(uint32_t& v) {
        if (left() < 4) return false;
        v = static_cast<uint32_t>(b[pos]) << 24 | static_cast<uint32_t>(b[pos + 1]) << 16 |
            static_cast<uint32_t>(b[pos + 2]) << 8 | b[pos + 3];
        pos += 4;
        return true;
    }
    enum Vlq { VlqOk, VlqTruncated, VlqTooLong };
    Vlq vlq(uint32_t& v) {  // at most 4 bytes
        v = 0;
        for (int i = 0; i < 4; ++i) {
            uint8_t c = 0;
            if (!u8(c)) return VlqTruncated;
            v = v << 7 | (c & 0x7F);
            if (!(c & 0x80)) return VlqOk;
        }
        return VlqTooLong;
    }
    // A data byte must have bit 7 clear.
    bool data(uint8_t& v) { return u8(v) && !(v & 0x80); }
    bool skip(uint32_t n) {
        if (left() < n) return false;
        pos += n;
        return true;
    }
};

struct RawEvent {
    uint64_t tick;
    uint8_t status, d1, d2;
};

struct Tempo {
    uint64_t tick;
    uint32_t tempo;
};

bool fail(std::string& err, const char* msg) {
    err = msg;
    return false;
}

bool oneDataByte(uint8_t status) { return (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0; }

}  // namespace

bool parseSmf(const std::vector<uint8_t>& file, std::vector<MidiEvent>& out, std::string& err) {
    out.clear();
    err.clear();
    Reader r{file, 0, file.size()};
    uint32_t tag = 0, len = 0, fmt = 0, ntrks = 0, div = 0;
    if (!r.u32(tag)) return fail(err, "file too short for MThd");
    if (tag != 0x4D546864u) return fail(err, "missing MThd");
    if (!r.u32(len) || len < 6) return fail(err, "bad MThd length");
    if (!r.u16(fmt) || !r.u16(ntrks) || !r.u16(div)) return fail(err, "truncated MThd");
    if (!r.skip(len - 6)) return fail(err, "truncated MThd");
    if (fmt > 1) return fail(err, "unsupported SMF format (only 0 and 1)");
    if (div & 0x8000) return fail(err, "SMPTE time division is not supported");
    if (div == 0) return fail(err, "zero ticks per quarter note");
    // DECISION P15: a file that declares no tracks is rejected.
    if (ntrks == 0) return fail(err, "no tracks");

    std::vector<std::vector<RawEvent>> tracks(ntrks);
    std::vector<Tempo> tempos;  // all tracks
    for (uint32_t t = 0; t < ntrks; ++t) {
        // DECISION P15: chunks of an unknown type are skipped (SMF spec); ntrks MTrk chunks must be
        // present, and bytes after the last one are ignored.
        for (;;) {
            if (!r.u32(tag)) return fail(err, "missing MTrk (file ends before the last track)");
            if (!r.u32(len)) return fail(err, "truncated chunk header");
            if (r.left() < len) return fail(err, "truncated chunk data");
            if (tag == 0x4D54726Bu) break;
            r.pos += len;
        }
        Reader tr{file, r.pos, r.pos + len};
        r.pos += len;
        uint64_t tick = 0;
        uint8_t running = 0;
        while (tr.left() > 0) {
            uint32_t delta = 0;
            const Reader::Vlq dv = tr.vlq(delta);
            if (dv == Reader::VlqTruncated) return fail(err, "truncated event delta time");
            if (dv == Reader::VlqTooLong) return fail(err, "delta time longer than 4 bytes");
            tick += delta;
            // DECISION P15: a track longer than 2^32 ticks is rejected (keeps tick * tempo in uint64).
            if (tick > kMaxTicks) return fail(err, "track too long");
            uint8_t st = 0, d1 = 0, d2 = 0;
            if (!tr.u8(st)) return fail(err, "truncated event");
            if (st & 0x80) {
                if (st < 0xF0) running = st;
            } else {  // running status: this byte is the first data byte
                // DECISION P15: running status is cancelled by meta and sysex events (SMF spec).
                // Some real files keep using it after a meta event; such a file is rejected here.
                if (!running) return fail(err, "data byte without running status");
                d1 = st;
                st = running;
                if (!oneDataByte(st)) {
                    uint8_t b2 = 0;
                    if (!tr.u8(b2)) return fail(err, "truncated event");
                    if (b2 & 0x80) return fail(err, "status byte in data position");
                    d2 = b2;
                }
                tracks[t].push_back({tick, st, d1, d2});
                continue;
            }
            if (st < 0xF0) {
                // DECISION P15: a status byte where a data byte belongs is an error, not a desync.
                if (!tr.u8(d1)) return fail(err, "truncated event");
                if (d1 & 0x80) return fail(err, "status byte in data position");
                if (!oneDataByte(st)) {
                    if (!tr.u8(d2)) return fail(err, "truncated event");
                    if (d2 & 0x80) return fail(err, "status byte in data position");
                }
                tracks[t].push_back({tick, st, d1, d2});
            } else if (st == 0xF0 || st == 0xF7) {
                running = 0;
                uint32_t n = 0;
                const Reader::Vlq lv = tr.vlq(n);
                if (lv == Reader::VlqTooLong) return fail(err, "sysex length longer than 4 bytes");
                if (lv != Reader::VlqOk || !tr.skip(n)) return fail(err, "truncated sysex");
            } else if (st == 0xFF) {
                running = 0;
                uint8_t type = 0;
                uint32_t n = 0;
                if (!tr.u8(type)) return fail(err, "truncated meta event");
                const Reader::Vlq lv = tr.vlq(n);
                if (lv == Reader::VlqTooLong) return fail(err, "meta length longer than 4 bytes");
                if (lv != Reader::VlqOk) return fail(err, "truncated meta event");
                const size_t at = tr.pos;
                if (!tr.skip(n)) return fail(err, "truncated meta event");
                if (type == 0x51 && n == 3) {
                    const uint32_t tempo = static_cast<uint32_t>(file[at]) << 16 |
                                           static_cast<uint32_t>(file[at + 1]) << 8 | file[at + 2];
                    if (tempo == 0) return fail(err, "zero tempo");
                    tempos.push_back({tick, tempo});
                } else if (type == 0x2F) {
                    break;  // end of track; anything after it in the chunk is ignored
                }
            } else {
                return fail(err, "unexpected system message in track");
            }
        }
    }

    // acc = sum(delta ticks * tempo) from tick 0; us = floor(acc / ppq) from the running total
    // at each event, so floors never accumulate.
    // DECISION P15: one global tempo map from the tempo events of all tracks (format 1 keeps them in
    // track 0; in a format 0 file there is only one track).
    std::stable_sort(tempos.begin(), tempos.end(), [](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
    std::vector<uint64_t> accAt(tempos.size());
    {
        uint64_t acc = 0, lastTick = 0, cur = kDefaultTempo;
        for (size_t i = 0; i < tempos.size(); ++i) {
            acc += (tempos[i].tick - lastTick) * cur;
            accAt[i] = acc;
            lastTick = tempos[i].tick;
            cur = tempos[i].tempo;
        }
    }
    auto accOf = [&](uint64_t tick) -> uint64_t {
        const size_t n = static_cast<size_t>(
            std::upper_bound(tempos.begin(), tempos.end(), tick,
                             [](uint64_t t, const Tempo& x) { return t < x.tick; }) - tempos.begin());
        if (n == 0) return tick * kDefaultTempo;
        return accAt[n - 1] + (tick - tempos[n - 1].tick) * tempos[n - 1].tempo;
    };

    for (const auto& trk : tracks)
        for (const RawEvent& e : trk) {
            const uint64_t us = accOf(e.tick) / div;
            if (us > 0xFFFFFFFFFFFFFFFFull / kChipHz) {
                out.clear();
                return fail(err, "file duration too long");
            }
            out.push_back({us * kChipHz / kFrameDen, e.status, e.d1, e.d2});
        }
    std::stable_sort(out.begin(), out.end(), [](const MidiEvent& a, const MidiEvent& b) { return a.frame < b.frame; });
    return true;
}

}  // namespace ens::player
