#pragma once
// The ROM folder: the six files design section 6.4 lists, checked for presence and size.
#include <juce_core/juce_core.h>

#include <vector>

#include "ks32/machine.h"

struct RomFileStatus {
    juce::String file, what;  // the path in the folder, and what it holds
    enum State { Found, Missing, WrongSize } state;
};

// One entry per file, in the design's order. When every file is found with the size
// Machine::create requires and outIfAllGood is set, the files are read into it.
std::vector<RomFileStatus> checkRomFolder(const juce::File& dir, ens::ks32::RomSet* outIfAllGood);
