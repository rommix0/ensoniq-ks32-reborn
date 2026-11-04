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
juce::String memoryInRomFolder(const juce::File& file, const juce::File& roms) {
    if (roms != juce::File() && file.isAChildOf(roms))
        return "The memory file cannot be in the ROM folder: " + file.getFullPathName();
    return {};
}

juce::String memoryTargetProblem(const juce::File& file, const juce::File& roms) {
    if (const auto err = memoryInRomFolder(file, roms); err.isNotEmpty()) return err;
    if (file.isDirectory() || !file.hasWriteAccess())  // a new file: its folder's access
        return "The memory file cannot be written: " + file.getFullPathName();
    return {};
}

using ens::ks32::Analog;
struct AnalogParam {
    const char* id;
    const char* name;
    Analog analog;
};
// Order matters: Ks32Processor::analogRaw_ follows it.
constexpr AnalogParam kAnalogParams[] = {{"volume", "Volume", Analog::Volume},
                                         {"dataEntry", "Data Entry", Analog::DataEntry},
                                         {"pitchWheel", "Pitch Wheel", Analog::PitchWheel},
                                         {"modWheel", "Mod Wheel", Analog::ModWheel}};
static_assert(std::size(kAnalogParams) == 4, "Ks32Processor::analogRaw_ has four slots");
}  // namespace

// DECISION A17: the analogs and the sound are host parameters (automatable): four floats 0..1 whose
// defaults are the rest positions (K13), and `sound` 0..127. A position maps to raw by analog_map.h.
juce::AudioProcessorValueTreeState::ParameterLayout Ks32Processor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (const auto& a : kAnalogParams)
        l.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(a.id, 1), a.name, juce::NormalisableRange<float>(0.0f, 1.0f),
            ens::app::analogPosition(a.analog, ens::ks32::kAnalogRest[static_cast<size_t>(a.analog)])));
    l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID("sound", 1), "Sound", 0, 127, 0));
    return l;
}

Ks32Processor::Ks32Processor(MemoryMode mode)
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      mode_(mode), params_(*this, nullptr, "KS32", layout()) {
    for (size_t i = 0; i < std::size(kAnalogParams); ++i)
        analogRaw_[i] = params_.getRawParameterValue(kAnalogParams[i].id);
    sound_ = params_.getRawParameterValue("sound");
    // No compiled-in default: the first start, with no settings, opens with no ROMs.
    if (const auto dir = settings_.romFolder(); dir != juce::File())
        setRomFolder(dir, romError_);
    else
        romError_ = "No ROM folder:\nopen Settings";
    startTimerHz(30);  // DECISION A18: one 30 Hz timer; the 60 s save counts 1800 ticks
}

Ks32Processor::~Ks32Processor() {
    stopTimer();
    // DECISION A11: saved on close only once booted; audio has stopped, so the machine is read directly.
    // DECISION A22: File mode only; a plugin instance's memory is in the project.
    if (mode_ == MemoryMode::File && engine_ && engine_->snapshot().booted)
        saveMemoryFile(settings_.memoryFile(), engine_->machine().ramImage());
}

// Refused, with the reason in err, when the memory file is inside the new folder: it would load the
// OS ROM as RAM and the 60 s save would overwrite it (setMemoryFile guards the other order).
bool Ks32Processor::setRomFolder(const juce::File& dir, juce::String& err) {
    err = memoryInRomFolder(settings_.memoryFile(), dir);
    if (err.isNotEmpty()) return false;
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
// DECISION A22: in Project mode this instance alone restarts cold from zeros; the shared file is left alone.
void Ks32Processor::eraseMemory() {
    if (mode_ == MemoryMode::Project) {
        startImage_.assign(static_cast<size_t>(kMemoryBytes), 0);
        startSelect_.clear();
        haveStart_ = true;
        if (engine_) restart(Ram::Start);
        return;
    }
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
    const auto snap = engine_ ? engine_->snapshot() : ens::app::Snapshot{};
    std::vector<uint8_t> img, sel;
    if (ram == Ram::Keep && snap.booted) {
        img = engine_->machine().ramImage();  // audio stopped: the machine is ours to read
        sel.assign(snap.select.begin(), snap.select.begin() + snap.selectLen);
    } else if (ram != Ram::File && haveStart_) {
        img = startImage_;  // DECISION A21: never a half-booted image, nor the file over a project's image
        sel = startSelect_;
    } else {
        img = loadMemoryFile(settings_.memoryFile(), memoryError_);  // wrong size: reported, cold boot
    }
    std::string e;
    auto fresh = ens::app::Engine::create(roms_, e);
    if (fresh && !img.empty() && !fresh->loadRam(img, e)) fresh.reset();
    const bool ok = fresh != nullptr;
    if (ok) {
        fresh->setSelect(sel.data(), static_cast<int>(sel.size()));
        if (rate_ > 0) fresh->prepare(rate_, block_);
        engine_.swap(fresh);  // fresh now holds the old engine, freed below after the resume
        startImage_ = std::move(img);
        startSelect_ = std::move(sel);
        haveStart_ = true;
        ramPending_ = false;
        lastSeq_ = 0;  // a new engine counts from 0
        followed_ = -1;  // a stale follow marker would make the new engine adopt the sound, not send it
        ticks_ = 0;
        romError_ = {};
    } else if (!engine_) {
        romError_ = juce::String(e);
    }
    suspendProcessing(false);
    return ok;
}

// DECISION A11: every 60 s, once booted, the audio thread copies the RAM (requestRam) and the next
// tick that finds the copy (takeRam) saves it. DECISION A18: the tick is 30 Hz so a panel sound pick
// reaches the host quickly; 1800 ticks make the 60 s.
void Ks32Processor::tick() {
    if (!engine_) return;
    // DECISION A18: follow a program change tracked from the panel or MIDI (programSeq moved), never the
    // engine's own sends. followed_ is stored first, then `sound` is written, so a processBlock that sees
    // the new `sound` also finds the marker (and adopts the value instead of sending it). A marker the
    // host overwrote before any block took it is cleared here.
    if (int m = followed_.load(); m >= 0 && m != static_cast<int>(sound_->load()))
        followed_.compare_exchange_strong(m, -1);
    const auto snap = engine_->snapshot();
    if (snap.program >= 0 && snap.programSeq != lastSeq_) {
        lastSeq_ = snap.programSeq;
        if (auto* p = params_.getParameter("sound"); p != nullptr && static_cast<int>(sound_->load()) != snap.program) {
            followed_.store(snap.program);
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(snap.program)));
            p->endChangeGesture();
        }
    }
    if (ramPending_) {
        if (engine_->takeRam(ramBuf_)) {
            ramPending_ = false;
            saved(saveMemoryFile(settings_.memoryFile(), ramBuf_));
        }
    } else if (mode_ == MemoryMode::File && ++ticks_ >= 1800 && engine_->snapshot().booted) {  // A22
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
    // Parameter values cross as atomic raw values: no lock, no allocation (A17).
    for (size_t i = 0; i < std::size(kAnalogParams); ++i)
        engine_->setAnalog(kAnalogParams[i].analog,
                           ens::app::analogRaw(kAnalogParams[i].analog, analogRaw_[i]->load()));
    // DECISION A18: a `sound` that tick() just followed (it holds the marker) is adopted, not sent. Any
    // other value is a host request. Remaining case: a panel pick and a host change of `sound` inside
    // one tick period can leave the host showing the followed value while the machine plays the host's.
    const int sound = static_cast<int>(sound_->load());
    int marker = sound;
    if (followed_.compare_exchange_strong(marker, -1))
        engine_->adoptProgram(sound);
    else
        engine_->setProgram(sound);
    // DECISION A12: every byte of every message, sysex and real-time included, as the MIDI IN jack.
    for (const auto m : midi) engine_->midi(m.samplePosition, m.data, m.numBytes);
    engine_->render(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
}

// DECISION A21: once booted, the running RAM. The audio thread copies it (requestRam); the message
// thread waits at most 300 ms for the copy, then, as no block ran (the machine is not running either),
// reads the machine under suspendProcessing. Off the message thread (perhaps the audio thread) nothing
// waits: the start image. Before the boot ends, the start image (anti-goal: chaining short boots); with
// no machine, the image a state gave, unchanged (anti-goal: losing a project's memory).
// The 60 s save (ramPending_) also uses requestRam/takeRam, but only in File mode, whose state has no
// RAM and which refuses copyToSharedMemory: the two users never meet in one instance.
std::vector<uint8_t> Ks32Processor::ram() {
    // ponytail: a host saving off the message thread loses the edits since the boot; keep the last
    // copy taken here if one does.
    if (!engine_ || !engine_->snapshot().booted || !juce::MessageManager::existsAndIsCurrentThread())
        return startImage_;
    std::vector<uint8_t> img;
    engine_->takeRam(img);  // a copy left by a request that timed out is stale: dropped
    engine_->requestRam();
    for (int i = 0; i < 60; ++i) {
        if (engine_->takeRam(img)) return img;
        juce::Thread::sleep(5);
    }
    suspendProcessing(true);
    img = engine_->machine().ramImage();
    suspendProcessing(false);
    return img;
}

// DECISION A20: copyXmlToBinary of <KS32State version="1" select="c037">, the parameter tree and, in
// Project mode, <RAM> (65 536 bytes, base64). `select` is the running selection once booted, else the
// one the start image came with.
void Ks32Processor::getStateInformation(juce::MemoryBlock& dest) {
    juce::XmlElement xml("KS32State");
    xml.setAttribute("version", 1);
    const auto snap = engine_ ? engine_->snapshot() : ens::app::Snapshot{};
    const auto sel = snap.booted && snap.selectLen > 0
                         ? std::vector<uint8_t>(snap.select.begin(), snap.select.begin() + snap.selectLen)
                         : startSelect_;
    if (!sel.empty())
        xml.setAttribute("select", juce::String::toHexString(sel.data(), static_cast<int>(sel.size()), 0));
    if (auto tree = params_.copyState().createXml()) xml.addChildElement(tree.release());
    if (mode_ == MemoryMode::Project)  // DECISION A23: the Standalone's state has no RAM; its memory is the file
        if (const auto img = ram(); !img.empty())
            xml.createNewChildElement("RAM")->addTextElement(juce::Base64::toBase64(img.data(), img.size()));
    copyXmlToBinary(xml, dest);
}

// DECISION A20: a blob that is not a KS32State is ignored. Otherwise the parameters are replaced and a
// valid `select` sets `sound` to its program (the state may predate the timer's follow, A18). In Project
// mode a version 1 RAM of 65 536 bytes restarts the machine from it with that selection; with no ROMs it
// waits as the start image. No RAM, a wrong size or another version leaves the machine as it is.
void Ks32Processor::setStateInformation(const void* data, int size) {
    const auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName("KS32State")) return;
    if (const auto* tree = xml->getChildByName(params_.state.getType()))
        params_.replaceState(juce::ValueTree::fromXml(*tree));

    juce::MemoryBlock hex;
    hex.loadFromHexString(xml->getStringAttribute("select"));
    const auto* b = static_cast<const uint8_t*>(hex.getData());
    const auto n = hex.getSize();
    std::vector<uint8_t> sel;
    if (n >= 2 && n <= 8 && (b[n - 2] & 0xF0) == 0xC0 && b[n - 1] < 0x80) {  // ends Cn pp; setSelect checks the rest
        sel.assign(b, b + n);
        if (auto* p = params_.getParameter("sound"))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(b[n - 1])));
    }

    std::vector<uint8_t> img;
    if (const auto* r = xml->getChildByName("RAM");
        r != nullptr && mode_ == MemoryMode::Project && xml->getIntAttribute("version") == 1) {
        juce::MemoryOutputStream out;
        if (juce::Base64::convertFromBase64(out, r->getAllSubText()) &&
            out.getDataSize() == static_cast<size_t>(kMemoryBytes)) {
            const auto* p = static_cast<const uint8_t*>(out.getData());
            img.assign(p, p + out.getDataSize());
        }
    }
    if (!img.empty()) {
        startImage_ = std::move(img);
        startSelect_ = std::move(sel);
        haveStart_ = true;
        if (engine_) restart(Ram::Start);  // with no ROMs, setRomFolder starts from it
    } else if (!sel.empty() && engine_ && !engine_->snapshot().booted) {
        startSelect_ = sel;  // DECISION A23: the Standalone's wrapper state arrives before the first block
        suspendProcessing(true);
        engine_->setSelect(sel.data(), static_cast<int>(sel.size()));
        suspendProcessing(false);
    }
}

// DECISION A22: the instance's RAM (A21) to the shared file, temporary file then replace (A11).
bool Ks32Processor::copyToSharedMemory(juce::String& err) {
    if (mode_ == MemoryMode::File) {
        err = "The Standalone saves the memory file itself.";
        return false;
    }
    if (!engine_ || !engine_->snapshot().booted) {
        err = "The machine has not finished booting. Try again when it has.";
        return false;
    }
    if (!saved(saveMemoryFile(settings_.memoryFile(), ram()))) {
        err = memoryError_;
        return false;
    }
    return true;
}

juce::AudioProcessorEditor* Ks32Processor::createEditor() { return new PanelEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Ks32Processor(); }
