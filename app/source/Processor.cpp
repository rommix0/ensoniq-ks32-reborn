#include "Processor.h"

#include <algorithm>
#include <cmath>

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
    // No compiled-in default. With no folder chosen, a "roms" folder beside the app (the dist's
    // layout, next to KS-32.exe) is taken and stored, so the plugin finds it too; with neither, the
    // first start opens with no ROMs.
    auto dir = settings_.romFolder();
    if (dir == juce::File()) {
        const auto beside =
            juce::File::getSpecialLocation(juce::File::currentApplicationFile).getSiblingFile("roms");
        if (beside.isDirectory()) dir = beside;
    }
    if (dir != juce::File())
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
    const std::lock_guard<std::mutex> lock(stateMu_);
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
    if (file.existsAsFile() && engine_) {
        const std::lock_guard<std::mutex> lock(stateMu_);
        restart(Ram::File);
    }
    return true;
}

// DECISION A11, R7: erase writes the 64 KB zero image to the memory file at once (an explicit act,
// not held back by the boot gate) and restarts the machine from it, so the firmware rebuilds its
// memory as it boots. With no machine, only the file is erased. If the write fails, nothing is
// erased and the machine runs on; the settings page says so.
// DECISION A22: in Project mode this instance alone restarts cold from zeros; the shared file is left alone.
// DECISION A15: either way the machine is cold on ROM 00: no select to replay and `sound` 0, set after
// the restart (outside the state mutex: a host may save from its notification), long before the boot ends.
void Ks32Processor::eraseMemory() {
    {
        const std::lock_guard<std::mutex> lock(stateMu_);
        if (mode_ == MemoryMode::File) {
            if (!saved(eraseMemoryFile(settings_.memoryFile()))) return;
            memoryError_ = {};
        }
        startSelect_.clear();
        if (mode_ == MemoryMode::Project || engine_) {  // File mode with no machine: the file alone
            startImage_.assign(static_cast<size_t>(kMemoryBytes), 0);
            haveStart_ = true;
            if (engine_) restart(Ram::Start);
        }
    }
    if (auto* p = params_.getParameter("sound"); p != nullptr && soundNow() != 0) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(0.0f));
        p->endChangeGesture();
    }
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
    std::vector<uint8_t> img;
    // DECISION A21: Start replays the start image's select; Keep and File keep the select a state would
    // save (the running one once booted, else the start one), so a new memory file keeps the sound.
    auto sel = ram == Ram::Start ? startSelect_ : select(snap);
    if (ram == Ram::Keep && snap.booted) {
        img = engine_->machine().ramImage();  // audio stopped: the machine is ours to read
    } else if (ram != Ram::File && haveStart_) {
        img = startImage_;  // DECISION A21: never a half-booted image, nor the file over a project's image
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
    // host overwrote before any block took it is cleared here. While a marker still waits for a block, no
    // new follow starts (lastSeq_ stays, the next tick retries): a block that read the older followed
    // value just after a newer marker was stored would send it.
    if (int m = followed_.load(); m >= 0 && m != soundNow()) followed_.compare_exchange_strong(m, -1);
    const auto snap = engine_->snapshot();
    if (snap.program >= 0 && snap.programSeq != lastSeq_ && followed_.load() < 0) {
        lastSeq_ = snap.programSeq;
        if (auto* p = params_.getParameter("sound"); p != nullptr && soundNow() != snap.program) {
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
    audioThread_.store(juce::Thread::getCurrentThreadId(), std::memory_order_relaxed);  // DECISION A21
    if (!engine_ || buffer.getNumChannels() < 2) {
        buffer.clear();
        return;
    }
    // Parameter values cross as atomic raw values: no lock, no allocation (A17).
    for (size_t i = 0; i < std::size(kAnalogParams); ++i)
        engine_->setAnalog(kAnalogParams[i].analog,
                           ens::app::analogRaw(kAnalogParams[i].analog, analogRaw_[i]->load()));
    // DECISION A18: a `sound` that tick() just followed (it holds the marker) is adopted, not sent. Since
    // tick() starts no follow while a marker waits, any other value is one already requested or adopted
    // (setProgram sends nothing) or a host request (sent). Remaining case: a panel pick and a host change
    // of `sound` inside one tick period can leave the host showing the followed value while the machine
    // plays the host's, until the next change of either.
    const int sound = soundNow();
    int marker = sound;
    if (followed_.compare_exchange_strong(marker, -1))
        engine_->adoptProgram(sound);
    else
        engine_->setProgram(sound);
    // DECISION A12: every byte of every message, sysex and real-time included, as the MIDI IN jack.
    for (const auto m : midi) engine_->midi(m.samplePosition, m.data, m.numBytes);
    engine_->render(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
}

// DECISION A21: once booted, the running RAM, whatever thread asks. On the audio thread (the one that
// last ran processBlock) no block runs during this call, so the machine is read directly: no wait, no
// gap, but a 64 KB copy that allocates there, which is the host's choice in calling getState on it. On
// any other thread the audio thread copies it (requestRam) and this waits at most 300 ms; if no block
// ran (the machine is not running either) it reads the machine under suspendProcessing. A host that has
// stopped calling processBlock so costs up to 300 ms per Project instance per save. Before the boot
// ends, the start image (anti-goal: chaining short boots); with no machine, the image a state gave,
// unchanged (anti-goal: losing a project's memory).
// The 60 s save (ramPending_) also uses requestRam/takeRam, but only in File mode, whose state has no
// RAM and which refuses copyToSharedMemory: the two users never meet in one instance.
std::vector<uint8_t> Ks32Processor::ram() {
    if (!engine_ || !engine_->snapshot().booted) return startImage_;
    if (juce::Thread::getCurrentThreadId() == audioThread_.load(std::memory_order_relaxed))
        return engine_->machine().ramImage();
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
// It takes the state mutex on every thread, the audio thread included: a wait, bounded, behind a
// concurrent save or restart. processBlock itself never takes it.
void Ks32Processor::getStateInformation(juce::MemoryBlock& dest) {
    juce::XmlElement xml("KS32State");
    xml.setAttribute("version", 1);
    const std::lock_guard<std::mutex> lock(stateMu_);  // a host may save from any thread (A21)
    const auto sel = select(engine_ ? engine_->snapshot() : ens::app::Snapshot{});
    if (!sel.empty())
        xml.setAttribute("select", juce::String::toHexString(sel.data(), static_cast<int>(sel.size()), 0));
    if (auto tree = params_.copyState().createXml()) xml.addChildElement(tree.release());
    if (mode_ == MemoryMode::Project)  // DECISION A23: the Standalone's state has no RAM; its memory is the file
        if (const auto img = ram(); !img.empty())
            xml.createNewChildElement("RAM")->addTextElement(juce::Base64::toBase64(img.data(), img.size()));
    copyXmlToBinary(xml, dest);
}

// DECISION A20: a blob that is not a KS32State is ignored. Otherwise the parameters are replaced and a
// valid `select` sets `sound` to its program (the state may predate the timer's follow, A18), written into
// the restored tree so no extra host edit is sent while a project loads. In Project mode a version 1 RAM
// of 65 536 bytes restarts the machine from it with that selection; with no ROMs it waits as the start
// image. No RAM, a wrong size or another version leaves the machine as it is.
void Ks32Processor::setStateInformation(const void* data, int size) {
    const auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName("KS32State")) return;
    juce::ValueTree tree;
    if (const auto* t = xml->getChildByName(params_.state.getType())) tree = juce::ValueTree::fromXml(*t);

    juce::MemoryBlock hex;
    hex.loadFromHexString(xml->getStringAttribute("select"));
    const auto* b = static_cast<const uint8_t*>(hex.getData());
    const auto n = hex.getSize();
    std::vector<uint8_t> sel;
    if (n >= 2 && n <= 8 && (b[n - 2] & 0xF0) == 0xC0 && b[n - 1] < 0x80) {  // ends Cn pp; setSelect checks the rest
        sel.assign(b, b + n);
        if (!tree.isValid()) tree = params_.copyState();
        auto sound = tree.getChildWithProperty("id", "sound");
        if (!sound.isValid()) {
            sound = juce::ValueTree("PARAM");
            sound.setProperty("id", "sound", nullptr);
            tree.appendChild(sound, nullptr);
        }
        sound.setProperty("value", static_cast<float>(b[n - 1]), nullptr);
    }
    if (tree.isValid()) params_.replaceState(tree);

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
    const std::lock_guard<std::mutex> lock(stateMu_);
    if (!img.empty()) {
        startImage_ = std::move(img);
        startSelect_ = std::move(sel);
        haveStart_ = true;
        if (engine_) restart(Ram::Start);  // with no ROMs, setRomFolder starts from it
    } else if (!sel.empty()) {
        // DECISION A23: the Standalone's wrapper state arrives before the first block; its select is the one
        // the next boot replays, and with no ROMs it waits for setRomFolder's restart. Once booted the
        // running select stays what a state saves, and the engine sends this one when `sound` asks for it.
        if (!engine_ || !engine_->snapshot().booted) startSelect_ = sel;
        if (engine_) {
            suspendProcessing(true);
            engine_->setSelect(sel.data(), static_cast<int>(sel.size()));
            suspendProcessing(false);
        }
    }
}

// The select a state saves and a Keep or File restart keeps (A20, A21): the running one once booted,
// else the one the start image came with.
std::vector<uint8_t> Ks32Processor::select(const ens::app::Snapshot& snap) const {
    if (snap.booted && snap.selectLen > 0) return {snap.select.begin(), snap.select.begin() + snap.selectLen};
    return startSelect_;
}

// `sound` as a program; a non-finite value (a project file is not trusted) reads as 0.
int Ks32Processor::soundNow() const {
    const float v = sound_->load();
    return std::isfinite(v) ? static_cast<int>(std::clamp(v, 0.0f, 127.0f)) : 0;
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
    std::vector<uint8_t> img;
    {
        const std::lock_guard<std::mutex> lock(stateMu_);
        img = ram();
    }
    if (!saved(saveMemoryFile(settings_.memoryFile(), img))) {
        err = memoryError_;
        return false;
    }
    return true;
}

juce::AudioProcessorEditor* Ks32Processor::createEditor() { return new PanelEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Ks32Processor(); }
