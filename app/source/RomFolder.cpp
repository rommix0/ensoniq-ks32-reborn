#include "RomFolder.h"

namespace {

struct RomFile {
    const char* file;
    const char* what;
    juce::int64 size;  // what Machine::create (OS) and buildWaveBanks (waves) require
    std::vector<uint8_t> ens::ks32::RomSet::*member;
};

using RS = ens::ks32::RomSet;
const RomFile kFiles[] = {
    {"ks32/ks32v301upper.bin", "OS v3.01, upper (128 KB)", 0x20000, &RS::osUpper},
    {"ks32/ks32v301lower.bin", "OS v3.01, lower (64 KB)", 0x10000, &RS::osLower},
    {"sq1-u25.bin", "Wave ROM", 0x80000, &RS::u25},
    {"sq1-u26.bin", "Wave ROM", 0x80000, &RS::u26},
    {"rom2.u39", "Wave ROM", 0x100000, &RS::rom2},
    {"rom3.u38", "Wave ROM", 0x100000, &RS::rom3},
};

}  // namespace

std::vector<RomFileStatus> checkRomFolder(const juce::File& dir, ens::ks32::RomSet* outIfAllGood) {
    std::vector<RomFileStatus> list;
    const bool isDir = dir.isDirectory();  // juce::File() would look in the drive's root
    bool good = true;
    for (const auto& f : kFiles) {
        const auto file = dir.getChildFile(f.file);
        const auto state = !isDir || !file.existsAsFile() ? RomFileStatus::Missing
                           : file.getSize() != f.size        ? RomFileStatus::WrongSize
                                                             : RomFileStatus::Found;
        good = good && state == RomFileStatus::Found;
        list.push_back({f.file, f.what, state});
    }
    if (good && outIfAllGood != nullptr) {
        for (const auto& f : kFiles) {
            juce::MemoryBlock mb;
            dir.getChildFile(f.file).loadFileAsData(mb);
            const auto* p = static_cast<const uint8_t*>(mb.getData());
            (outIfAllGood->*f.member).assign(p, p + mb.getSize());
        }
    }
    return list;
}
