#pragma once
// The plugin's AudioProcessor: owns the settings and the engine and plays it, stereo out, MIDI in.
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "AppSettings.h"
#include "app/analog_map.h"
#include "app/engine.h"

// DECISION A22: File, the Standalone: the memory file is the machine's memory, saved on close and every
// 60 s (A11). Project, every plugin format: the instance starts from the shared file, then its memory
// lives in the plugin state; nothing writes the file but "Copy to shared memory".
enum class MemoryMode { File, Project };

class Ks32Processor : public juce::AudioProcessor, private juce::Timer {
public:
    // File when loaded as the Standalone, else Project (the checks construct directly: Project).
    static MemoryMode defaultMemoryMode() {
        return juce::PluginHostType::getPluginLoadedAs() == wrapperType_Standalone ? MemoryMode::File
                                                                                   : MemoryMode::Project;
    }
    explicit Ks32Processor(MemoryMode mode = defaultMemoryMode());
    ~Ks32Processor() override;  // File mode: saves the memory (A11)
    MemoryMode memoryMode() const { return mode_; }

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
    // leaves the machine running and sets memoryError. Project mode: a cold machine, the file untouched (A22).
    // Either way the machine boots to ROM 00 and `sound` is set to 0 (A15).
    void eraseMemory();
    // Project mode, once booted: writes the running RAM (A21) to the memory file. False with err before
    // the boot, in File mode, or when the write fails (also memoryError).
    bool copyToSharedMemory(juce::String& err);

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

    // DECISION A20: <KS32State version="1" select="..."> with the parameters and, in Project mode, <RAM>.
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

private:
    // Keep: the running RAM once booted, else the start image. File: the memory file. Start: the start image.
    enum class Ram { Keep, File, Start };
    // Both: the caller holds stateMu_.
    bool restart(Ram ram);  // a new engine from roms_; the old one stays if that fails
    std::vector<uint8_t> ram();  // the RAM a state or a copy saves (A21); empty when there is none
    std::vector<uint8_t> select(const ens::app::Snapshot& snap) const;  // the select a state saves
    int soundNow() const;  // `sound` 0-127; NaN reads as 0
    void timerCallback() override { tick(); }
    bool saved(bool ok);            // ok; a failure sets memoryError_, a success clears that

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    const MemoryMode mode_;
    AppSettings settings_;
    // DECISION A21: the image the machine last started from and the selection to replay with it; set
    // by each restart, or by a state that arrives with no ROMs (used once they are good).
    std::vector<uint8_t> startImage_, startSelect_;
    bool haveStart_ = false;
    // DECISION A21: guards the engine swap (restart), ram() and the start fields against a host saving
    // from another thread. processBlock never takes it.
    std::mutex stateMu_;
    // DECISION A21: the thread that last ran processBlock (one relaxed store per block), so ram() knows
    // when it is called on the audio thread.
    std::atomic<void*> audioThread_{nullptr};
    static_assert(std::atomic<void*>::is_always_lock_free, "processBlock's store must not lock");
    ens::ks32::RomSet roms_;  // the last good set; empty until then
    std::unique_ptr<ens::app::Engine> engine_;
    juce::String romError_, memoryError_;
    double rate_ = 0;  // 0 until prepareToPlay
    int block_ = 0;
    int ticks_ = 0;            // timer ticks (30 Hz) since the last save request
    bool ramPending_ = false;  // a requestRam is waiting for takeRam
    std::vector<uint8_t> ramBuf_;
    uint32_t lastSeq_ = 0;  // the snapshot's programSeq at the last tick (A18)
    std::atomic<int> followed_{-1};  // the sound tick() last wrote to `sound`, until processBlock takes it
    juce::AudioProcessorValueTreeState params_;
    std::atomic<float>* analogRaw_[4] = {};  // volume, dataEntry, pitchWheel, modWheel (kAnalogParams in Processor.cpp)
    std::atomic<float>* sound_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ks32Processor)
};
