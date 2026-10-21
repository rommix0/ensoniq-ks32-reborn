#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ens::player {

struct DramTable {
    uint32_t wordAddr = 0;
    std::vector<uint16_t> words;
};

struct EspProgram {
    int index = 0;
    int effectId = 0;                 // header byte 5
    std::string name;                 // from effect_defaults
    std::vector<uint64_t> instr;      // 89 words, pc 0x00..0x58
    int type1Count = 0, type2Count = 0;  // $E0 range [0,type1Count), $C0 range the next type2Count
    std::vector<std::pair<uint8_t, uint32_t>> gpr;  // all GPR/SPR records, ascending address
    std::vector<DramTable> tables;    // index 10 only
    std::vector<std::pair<uint8_t, uint32_t>> mix;  // effect_defaults "gpr" (includes 0x7F), ascending
};

struct EffectLibrary {
    std::vector<EspProgram> programs;
    int fadeFrames = 595;
    const EspProgram* find(int index) const;  // nullptr if absent
};

// programsJson = esp/esp_programs.json, defaultsJson = esp/effect_defaults.json. Every program must
// have a defaults entry. Never throws; on failure returns false with err set.
bool parseEffects(const std::string& programsJson, const std::string& defaultsJson, EffectLibrary& out,
                  std::string& err);

}  // namespace ens::player
