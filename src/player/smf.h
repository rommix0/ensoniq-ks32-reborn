#pragma once
// Standard MIDI File reader for the offline renderer: channel messages with sample-frame times.
#include <cstdint>
#include <string>
#include <vector>

namespace ens::player {

struct MidiEvent {
    uint64_t frame;  // chip frame (30476180 / 1024 Hz) since the start of the file
    uint8_t status, d1, d2;
};

// Formats 0 and 1 with a PPQ division. Events of all tracks are merged by frame, stable by track
// then file order. Meta and sysex events are skipped (only FF 51 tempo is used). Never throws and
// never reads past the buffer; on failure returns false with `err` set and `out` empty.
bool parseSmf(const std::vector<uint8_t>& file, std::vector<MidiEvent>& out, std::string& err);

}  // namespace ens::player
