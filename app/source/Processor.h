#pragma once
// The plugin's AudioProcessor: owns the engine and plays it, stereo out, MIDI in.
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

#include "app/engine.h"

class Ks32Processor : public juce::AudioProcessor {
public:
    Ks32Processor();

    ens::app::Engine* engine() { return engine_.get(); }  // null until ROMs are loaded
    // Message thread, with processing suspended by the caller. On failure the engine is dropped
    // and err (also romError()) says why.
    bool setRomSet(const ens::ks32::RomSet& roms, juce::String& err);
    juce::String romError() const { return romError_; }

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // No plugin state in this plan (design section 4).
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    std::unique_ptr<ens::app::Engine> engine_;
    juce::String romError_;
    double rate_ = 0;  // 0 until prepareToPlay
    int block_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ks32Processor)
};
