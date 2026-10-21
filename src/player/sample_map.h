#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ens::player {

// One key zone of a waveform: notes up to and including topKey use it.
struct Zone {
    int topKey = 0;
    int pitchWord = 0;     // signed OTIS FC word at the zone's root key
    int bank = 0;
    uint32_t start = 0, loopStart = 0, end = 0;  // sample addresses within the bank
    int endFrac = 0;       // end_frac16
    uint8_t crLow = 0;     // low byte of the OTIS control register (loop mode, direction)
};

struct Wave {
    int set = 0;           // 0 internal, 1 expansion ROM 3
    int number = 0;
    std::string name, note;
    std::vector<Zone> zones;  // ascending topKey, never empty
};

struct SampleMap {
    std::vector<Wave> waves;
    const Wave* find(int set, int number) const;  // nullptr if absent
};

// Reads docs/rom-investigation/data/sample_map.json. Only waves with at least one zone are kept.
// Never throws; on failure returns false with err set.
bool parseSampleMap(const std::string& json, SampleMap& out, std::string& err);

// First zone with key <= topKey, else the last. w.zones must not be empty.
const Zone& zoneForKey(const Wave& w, int key);

}  // namespace ens::player
