#include "MemoryFile.h"

std::vector<uint8_t> loadMemoryFile(const juce::File& file, juce::String& err) {
    err = {};
    if (!file.existsAsFile()) return {};
    juce::MemoryBlock mb;
    if (!file.loadFileAsData(mb) || static_cast<juce::int64>(mb.getSize()) != kMemoryBytes) {
        err = "Memory file ignored (expected " + juce::String(kMemoryBytes) + " bytes, got " +
              juce::String(static_cast<juce::int64>(mb.getSize())) + "): " + file.getFullPathName();
        return {};
    }
    const auto* p = static_cast<const uint8_t*>(mb.getData());
    return {p, p + mb.getSize()};
}

// DECISION A11: each save writes <file>.tmp and then replaces the file, so a crash mid-write
// leaves the previous image whole. A read-only file is never replaced: Windows refuses that rename
// itself, POSIX allows it, so the write-access check keeps Linux the same.
bool saveMemoryFile(const juce::File& file, const std::vector<uint8_t>& img) {
    const auto tmp = file.getSiblingFile(file.getFileName() + ".tmp");
    return file.getParentDirectory().createDirectory().wasOk() && file.hasWriteAccess() &&
           tmp.replaceWithData(img.data(), img.size()) && tmp.replaceFileIn(file);
}
