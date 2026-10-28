#include "Processor.h"

#include "Editor.h"

namespace {

// The OS and wave ROM files in dir, unchecked (Machine::create checks the sizes).
bool readRoms(const juce::File& dir, ens::ks32::RomSet& r, juce::String& err) {
    const auto read = [&](const char* name, std::vector<uint8_t>& out) {
        juce::MemoryBlock mb;
        if (!dir.getChildFile(name).loadFileAsData(mb)) {
            err = "ROM not found: " + dir.getChildFile(name).getFullPathName();
            return false;
        }
        const auto* p = static_cast<const uint8_t*>(mb.getData());
        out.assign(p, p + mb.getSize());
        return true;
    };
    return read("ks32/ks32v301upper.bin", r.osUpper) && read("ks32/ks32v301lower.bin", r.osLower) &&
           read("sq1-u25.bin", r.u25) && read("sq1-u26.bin", r.u26) && read("rom2.u39", r.rom2) &&
           read("rom3.u38", r.rom3);
}

}  // namespace

Ks32Processor::Ks32Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    ens::ks32::RomSet roms;
    juce::String err;
    if (readRoms(juce::File(ENS_ROM_DIR), roms, err))
        setRomSet(roms, err);
    else
        romError_ = err;
}

bool Ks32Processor::setRomSet(const ens::ks32::RomSet& roms, juce::String& err) {
    std::string e;
    engine_ = ens::app::Engine::create(roms, e);
    if (!engine_) {
        romError_ = err = juce::String(e);
        return false;
    }
    romError_ = {};
    if (rate_ > 0) engine_->prepare(rate_, block_);
    return true;
}

void Ks32Processor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) {
    rate_ = sampleRate;
    block_ = maximumExpectedSamplesPerBlock;
    if (engine_) engine_->prepare(rate_, block_);
}

bool Ks32Processor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.size() == 1 &&
           layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Ks32Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    if (!engine_ || buffer.getNumChannels() < 2) {
        buffer.clear();
        return;
    }
    // DECISION A12: every byte of every message, sysex and real-time included, as the MIDI IN jack.
    for (const auto m : midi) engine_->midi(m.samplePosition, m.data, m.numBytes);
    engine_->render(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
}

juce::AudioProcessorEditor* Ks32Processor::createEditor() { return new Ks32Editor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Ks32Processor(); }
