#include "Processor.h"

#include "PanelEditor.h"
#include "MemoryFile.h"
#include "RomFolder.h"

namespace {

// The first file that is not good, as two display rows; empty when all six are good.
juce::String romProblem(const std::vector<RomFileStatus>& list) {
    for (const auto& s : list)
        if (s.state != RomFileStatus::Found)
            return juce::String(s.state == RomFileStatus::Missing ? "ROM missing:\n" : "ROM wrong size:\n") + s.file;
    return {};
}

// Why file cannot be the memory file, or empty. Not in the ROM folder (ks32v301lower.bin is 64 KB
// too, and the 60 s save would overwrite it), and a file that can be written.
juce::String memoryTargetProblem(const juce::File& file, const juce::File& roms) {
    if (roms != juce::File() && file.isAChildOf(roms))
        return "The memory file cannot be in the ROM folder: " + file.getFullPathName();
    if (file.isDirectory() || !file.hasWriteAccess())  // a new file: its folder's access
        return "The memory file cannot be written: " + file.getFullPathName();
    return {};
}

}  // namespace

Ks32Processor::Ks32Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    // No compiled-in default: the first start, with no settings, opens with no ROMs.
    if (const auto dir = settings_.romFolder(); dir != juce::File())
        setRomFolder(dir);
    else
        romError_ = "No ROM folder:\nopen Settings";
    startTimer(1000);
}

Ks32Processor::~Ks32Processor() {
    stopTimer();
    // DECISION A11: saved on close only once booted; audio has stopped, so the machine is read directly.
    if (engine_ && engine_->snapshot().booted) saveMemoryFile(settings_.memoryFile(), engine_->machine().ramImage());
}

bool Ks32Processor::setRomFolder(const juce::File& dir) {
    settings_.setRomFolder(dir);
    ens::ks32::RomSet roms;
    const auto problem = romProblem(checkRomFolder(dir, &roms));
    if (problem.isNotEmpty()) {
        if (!engine_) romError_ = problem;
        return false;  // a running machine keeps running
    }
    roms_ = std::move(roms);
    return restart(Ram::Keep);
}

bool Ks32Processor::setMemoryFile(const juce::File& file, juce::String& err) {
    err = memoryTargetProblem(file, settings_.romFolder());
    if (err.isNotEmpty()) return false;
    if (file.existsAsFile()) {
        loadMemoryFile(file, err);
        if (err.isNotEmpty()) return false;
    }
    settings_.setMemoryFile(file);
    memoryError_ = {};
    if (file.existsAsFile() && engine_) restart(Ram::File);
    return true;
}

// DECISION A11, R7: erase writes the 64 KB zero image to the memory file at once (an explicit act,
// not held back by the boot gate) and restarts the machine from it, so the firmware rebuilds its
// memory as it boots. With no machine, only the file is erased. If the write fails, nothing is
// erased and the machine runs on; the settings page says so.
void Ks32Processor::eraseMemory() {
    if (!saved(eraseMemoryFile(settings_.memoryFile()))) return;
    memoryError_ = {};
    if (engine_) restart(Ram::File);
}

// A failed save is shown on the settings page (memoryError) until a save succeeds.
bool Ks32Processor::saved(bool ok) {
    const auto msg = "Memory not saved: " + settings_.memoryFile().getFullPathName();
    if (!ok)
        memoryError_ = msg;
    else if (memoryError_ == msg)
        memoryError_ = {};
    return ok;
}

bool Ks32Processor::restart(Ram ram) {
    suspendProcessing(true);  // from here the audio thread is out of processBlock, and stays out
    std::vector<uint8_t> img;
    if (ram == Ram::Keep && engine_ && engine_->snapshot().booted)
        img = engine_->machine().ramImage();  // audio stopped: the machine is ours to read
    else
        img = loadMemoryFile(settings_.memoryFile(), memoryError_);  // wrong size: reported, cold boot
    std::string e;
    auto fresh = ens::app::Engine::create(roms_, e);
    if (fresh && !img.empty() && !fresh->loadRam(img, e)) fresh.reset();
    const bool ok = fresh != nullptr;
    if (ok) {
        if (rate_ > 0) fresh->prepare(rate_, block_);
        engine_.swap(fresh);  // fresh now holds the old engine, freed below after the resume
        ramPending_ = false;
        ticks_ = 0;
        romError_ = {};
    } else if (!engine_) {
        romError_ = juce::String(e);
    }
    suspendProcessing(false);
    return ok;
}

// DECISION A11: every 60 s, once booted, the audio thread copies the RAM (requestRam) and the next
// tick that finds the copy (takeRam) saves it.
void Ks32Processor::timerCallback() {
    if (!engine_) return;
    if (ramPending_) {
        if (engine_->takeRam(ramBuf_)) {
            ramPending_ = false;
            saved(saveMemoryFile(settings_.memoryFile(), ramBuf_));
        }
    } else if (++ticks_ >= 60 && engine_->snapshot().booted) {
        ticks_ = 0;
        engine_->requestRam();
        ramPending_ = true;
    }
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

juce::AudioProcessorEditor* Ks32Processor::createEditor() { return new PanelEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Ks32Processor(); }
