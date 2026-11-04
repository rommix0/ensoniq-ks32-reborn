#pragma once
// The plugin's AudioProcessor: owns the settings and the engine and plays it, stereo out, MIDI in.
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "AppSettings.h"
#include "app/analog_map.h"
#include "app/engine.h"

class Ks32Processor : public juce::AudioProcessor, private juce::Timer {
public:
    Ks32Processor();
    ~Ks32Processor() override;  // saves the memory (A11)

    ens::app::Engine* engine() { return engine_.get(); }  // null until the ROMs are good
    // Why there is no engine, two display rows separated by '\n' (design section 6.3).
    juce::String romError() const { return romError_; }
    // Why the memory file was not loaded or not saved, or empty.
    juce::String memoryError() const { return memoryError_; }
    // The host parameters (A17): volume, dataEntry, pitchWheel, modWheel (0..1) and sound (0..127).
    juce::AudioProcessorValueTreeState& params() { return params_; }
    // The 30 Hz timer's work (A18), public so the checks can call it: follows the engine's sound
    // into `sound`, and counts the 60 s save.
    void tick();
    bool prepared() const { return rate_ > 0; }  // prepareToPlay has run: an audio device is open
    const AppSettings& settings() const { return settings_; }

    // Message thread. Each suspends processing around the engine swap itself.
    // Stores dir and, when all six ROMs are good, re-creates the machine keeping its memory.
    bool setRomFolder(const juce::File& dir, juce::String& err);
    // Stores file and, when it holds an image, restarts the machine from it. A file in the ROM
    // folder, one that cannot be written or one of the wrong size is refused (err) and the setting stays.
    bool setMemoryFile(const juce::File& file, juce::String& err);
    // Zeros to the file, then a cold machine (A11, R7); the caller has confirmed. A failed write
    // leaves the machine running and sets memoryError.
    void eraseMemory();

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
    enum class Ram { Keep, File };
    bool restart(Ram ram);  // a new engine from roms_; the old one stays if that fails
    void timerCallback() override { tick(); }
    bool saved(bool ok);            // ok; a failure sets memoryError_, a success clears that

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    AppSettings settings_;
    ens::ks32::RomSet roms_;  // the last good set; empty until then
    std::unique_ptr<ens::app::Engine> engine_;
    juce::String romError_, memoryError_;
    double rate_ = 0;  // 0 until prepareToPlay
    int block_ = 0;
    int ticks_ = 0;            // timer ticks (30 Hz) since the last save request
    bool ramPending_ = false;  // a requestRam is waiting for takeRam
    std::vector<uint8_t> ramBuf_;
    int lastProgram_ = -1;  // the sound last seen in the snapshot
    juce::AudioProcessorValueTreeState params_;
    std::atomic<float>* analogRaw_[4] = {};  // volume, dataEntry, pitchWheel, modWheel
    std::atomic<float>* sound_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ks32Processor)
};
