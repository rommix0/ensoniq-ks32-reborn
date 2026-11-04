// KS-32 app checks: checkRomFolder on a temp folder of dummy files the right size (never ROM
// content), the memory file's load and save, the processor's memory-file guards, and (ruling R8) the built KS-32.vst3 loaded,
// played and its editor opened through JUCE's own VST3 host, as a DAW would.
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "../source/AppSettings.h"
#include "../source/MemoryFile.h"
#include "../source/Processor.h"
#include "../source/RomFolder.h"
#include "app/analog_map.h"
#include "ks32/panel.h"

namespace {

int failures = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        if (!(c)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);   \
            ++failures;                                                \
        }                                                              \
    } while (0)

void write(const juce::File& f, size_t size) {
    f.getParentDirectory().createDirectory();
    const std::vector<char> zeros(size);
    f.replaceWithData(zeros.data(), zeros.size());
}

int count(const std::vector<RomFileStatus>& list, RomFileStatus::State s) {
    int n = 0;
    for (const auto& r : list) n += r.state == s;
    return n;
}

// The six ROM files as zeros of the right sizes.
void dummyRoms(const juce::File& dir) {
    const std::pair<const char*, size_t> files[] = {
        {"ks32/ks32v301upper.bin", 0x20000}, {"ks32/ks32v301lower.bin", 0x10000}, {"sq1-u25.bin", 0x80000},
        {"sq1-u26.bin", 0x80000},            {"rom2.u39", 0x100000},             {"rom3.u38", 0x100000}};
    for (const auto& [name, size] : files) write(dir.getChildFile(name), size);
}

void romFolder(const juce::File& dir) {
    dummyRoms(dir);

    ens::ks32::RomSet roms;
    auto list = checkRomFolder(dir, &roms);
    CHECK(list.size() == 6 && count(list, RomFileStatus::Found) == 6);
    std::string err;
    CHECK(ens::ks32::Machine::create(roms, err) != nullptr);  // the sizes are the ones create takes

    // (b) a renamed wave ROM: missing, and no RomSet.
    CHECK(dir.getChildFile("sq1-u26.bin").moveFileTo(dir.getChildFile("sq1-u26.bak")));
    ens::ks32::RomSet none;
    list = checkRomFolder(dir, &none);
    CHECK(list[3].file == "sq1-u26.bin" && list[3].state == RomFileStatus::Missing);
    CHECK(count(list, RomFileStatus::Found) == 5 && none.osUpper.empty() && none.u25.empty());

    write(dir.getChildFile("rom2.u39"), 100);  // a short file
    list = checkRomFolder(dir, nullptr);
    CHECK(list[4].state == RomFileStatus::WrongSize);

    CHECK(count(checkRomFolder(juce::File(), nullptr), RomFileStatus::Missing) == 6);  // no folder chosen
}

void memoryFile(const juce::File& dir) {
    const auto mem = dir.getChildFile("mem/ks32-memory.bin");
    juce::String err;
    CHECK(loadMemoryFile(mem, err).empty() && err.isEmpty());  // none yet: a cold boot, no error

    std::vector<uint8_t> img(static_cast<size_t>(kMemoryBytes));
    img.front() = 1;
    img.back() = 2;
    CHECK(saveMemoryFile(mem, img));
    CHECK(loadMemoryFile(mem, err) == img && err.isEmpty());
    img[5] = 9;
    CHECK(saveMemoryFile(mem, img));  // over an existing file
    CHECK(loadMemoryFile(mem, err) == img);
    CHECK(!mem.getSiblingFile("ks32-memory.bin.tmp").exists());

    // Erase (R7): the file becomes 64 KB of zeros at once, over the saved image.
    CHECK(eraseMemoryFile(mem));
    const auto erased = loadMemoryFile(mem, err);
    CHECK(err.isEmpty() && erased.size() == static_cast<size_t>(kMemoryBytes));
    CHECK(std::all_of(erased.begin(), erased.end(), [](uint8_t b) { return b == 0; }));

    mem.replaceWithData(img.data(), 100);  // the wrong size: reported and ignored
    CHECK(loadMemoryFile(mem, err).empty() && err.isNotEmpty());
}

// The processor's memory-file guards, on dummy ROMs (a machine is made but never run) in a temp
// settings folder.
void processor(const juce::File& dir) {
    const auto roms = dir.getChildFile("roms");
    dummyRoms(roms);
    _putenv_s("KS32_SETTINGS_DIR", dir.getFullPathName().toRawUTF8());
    AppSettings().setRomFolder(roms);
    Ks32Processor p;
    CHECK(p.engine() != nullptr);

    // A ROM is never the memory file, though ks32v301lower.bin is 64 KB: the 60 s save would overwrite it.
    juce::String err;
    const auto lower = roms.getChildFile("ks32/ks32v301lower.bin");
    CHECK(!p.setMemoryFile(lower, err) && err.isNotEmpty() && p.settings().memoryFile() != lower);

    // The other order: a memory file chosen first, then a ROM folder that contains it, is refused;
    // the file and the ROM folder setting are untouched.
    const auto dump = dir.getChildFile("dump"), inDump = dump.getChildFile("m.bin");
    CHECK(eraseMemoryFile(inDump));
    CHECK(p.setMemoryFile(inDump, err) && p.settings().memoryFile() == inDump);
    const auto* engine0 = p.engine();
    CHECK(!p.setRomFolder(dump, err) && err.isNotEmpty() && p.settings().romFolder() == roms &&
          p.engine() == engine0 && inDump.getSize() == kMemoryBytes);
    CHECK(p.setMemoryFile(dir.getChildFile("mem0.bin"), err));  // back to a file outside the dump

    // A file that cannot be written is refused.
    const auto mem = dir.getChildFile("mem.bin");
    CHECK(eraseMemoryFile(mem) && mem.setReadOnly(true));
    CHECK(!p.setMemoryFile(mem, err) && err.isNotEmpty() && p.settings().memoryFile() != mem);

    // Taken while writable (a restart from it). Then, read-only, Erase fails: the memory error says
    // so and the machine is not restarted.
    CHECK(mem.setReadOnly(false) && p.setMemoryFile(mem, err) && p.memoryError().isEmpty());
    const auto* engine = p.engine();
    CHECK(mem.setReadOnly(true));
    p.eraseMemory();
    CHECK(p.memoryError().startsWith("Memory not saved") && p.engine() == engine);
    // Writable again: Erase saves, clears the error and restarts.
    CHECK(mem.setReadOnly(false));
    p.eraseMemory();
    CHECK(p.memoryError().isEmpty() && p.engine() != engine);
}

// The host parameters (A17): five, by ID and name; the defaults are the analogs' rest values.
void parameters(const juce::File& dir) {
    const auto roms = dir.getChildFile("roms");
    dummyRoms(roms);
    _putenv_s("KS32_SETTINGS_DIR", dir.getFullPathName().toRawUTF8());
    AppSettings().setRomFolder(roms);
    Ks32Processor p;
    CHECK(p.getParameters().size() == 5);
    const std::pair<const char*, const char*> expect[] = {{"volume", "Volume"},        {"dataEntry", "Data Entry"},
                                                           {"pitchWheel", "Pitch Wheel"}, {"modWheel", "Mod Wheel"},
                                                           {"sound", "Sound"}};
    for (const auto& [id, name] : expect) {
        auto* q = p.params().getParameter(id);
        CHECK(q != nullptr && q->getName(64) == name);
    }
    auto* sound = p.params().getParameter("sound");
    CHECK(sound != nullptr && sound->convertFrom0to1(0.0f) == 0.0f && sound->convertFrom0to1(1.0f) == 127.0f);
    using ens::ks32::Analog;
    const std::pair<Analog, const char*> analogs[] = {{Analog::Volume, "volume"},       {Analog::DataEntry, "dataEntry"},
                                                      {Analog::PitchWheel, "pitchWheel"}, {Analog::ModWheel, "modWheel"}};
    for (const auto& [a, id] : analogs)
        CHECK(ens::app::analogRaw(a, *p.params().getRawParameterValue(id)) == ens::ks32::kAnalogRest[static_cast<size_t>(a)]);
}

// A18 on the real ROMs: a panel or MIDI pick is followed into `sound` and never sent back, even two a
// block apart; two host changes of `sound` a block apart send exactly twice and tick() never undoes them.
void follow(const juce::File& dir) {
    const juce::File roms(KS32_ROM_DIR);
    if (!roms.getChildFile("sq1-u25.bin").existsAsFile()) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    _putenv_s("KS32_SETTINGS_DIR", dir.getFullPathName().toRawUTF8());
    AppSettings().setRomFolder(roms);
    Ks32Processor p;
    CHECK(p.engine() != nullptr);
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    juce::AudioBuffer<float> buf(2, 480);
    juce::MidiBuffer midi;
    auto* sound = p.params().getParameter("sound");
    const auto soundValue = [&] { return static_cast<int>(sound->convertFrom0to1(sound->getValue()) + 0.5f); };
    const auto block = [&](int n, bool tick = true) {  // n blocks of 10 ms, tick() after each
        for (int i = 0; i < n; ++i) {
            buf.clear();
            p.processBlock(buf, midi);
            midi.clear();
            if (tick) p.tick();
        }
    };
    const auto sent = [&] { return p.engine()->snapshot().programsSent; };
    const auto row0 = [&] { return ens::ks32::rowText(p.engine()->snapshot().panel, 0); };
    block(340);  // the boot
    CHECK(p.engine()->snapshot().program == 0);

    // (i) the panel: Up/Yes twice.
    for (int k = 0; k < 2; ++k) {
        p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(ens::ks32::Button::UpYes), true, 0});
        block(10);
        p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(ens::ks32::Button::UpYes), false, 0});
        block(20);
    }
    block(20);
    CHECK(sent() == 0 && soundValue() == 2 && p.engine()->snapshot().program == 2);
    // MIDI in: two program changes one block apart.
    midi.addEvent(juce::MidiMessage(0xC0, 5), 0);
    block(1);
    midi.addEvent(juce::MidiMessage(0xC0, 6), 0);
    block(1);
    block(50);
    CHECK(sent() == 0 && soundValue() == 6 && row0().find("ROM   06") != std::string::npos);

    // (ii) the host: sound 3, then 4, a block apart.
    const auto host = [&](int n) { sound->setValueNotifyingHost(sound->convertTo0to1(static_cast<float>(n))); };
    host(3);
    block(1, false);  // no tick yet: the snapshot says 3 while the host is about to say 4
    host(4);
    p.tick();         // a stale snapshot (program 3) must not write `sound` back
    CHECK(soundValue() == 4);
    block(1);
    CHECK(soundValue() == 4);
    block(80);
    CHECK(sent() == 2 && soundValue() == 4 && p.engine()->snapshot().program == 4 &&
          row0().find("ROM   04") != std::string::npos);
}

// Ruling R8: KS32_SETTINGS_DIR points the plugin at a temp settings file naming the ROM folder,
// so neither the user's settings nor their memory file is touched.
void vst3(const juce::File& dir) {
    const juce::File bundle(KS32_VST3_PATH), roms(KS32_ROM_DIR);
    if (!bundle.exists() || !roms.getChildFile("sq1-u25.bin").existsAsFile()) {
        std::printf("  skip: VST3 or ROMs not found\n");
        return;
    }
    _putenv_s("KS32_SETTINGS_DIR", dir.getFullPathName().toRawUTF8());  // the plugin DLL reads this process's
    AppSettings().setRomFolder(roms);                                    // environment, so it lands here too

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types, bundle.getFullPathName());
    CHECK(types.size() == 1);
    if (types.isEmpty()) return;
    CHECK(types[0]->name == "KS-32" && types[0]->isInstrument);

    constexpr double kRate = 48000;
    constexpr int kBlock = 480;  // 10 ms
    juce::String err;
    auto plugin = format.createInstanceFromDescription(*types[0], kRate, kBlock, err);
    CHECK(plugin != nullptr);
    if (!plugin) {
        std::printf("  load failed: %s\n", err.toRawUTF8());
        return;
    }
    plugin->setPlayConfigDetails(0, 2, kRate, kBlock);
    plugin->prepareToPlay(kRate, kBlock);

    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    const auto play = [&](int ms, float* peakL, float* peakR) {
        for (int b = 0; b < ms / 10; ++b) {
            buf.clear();
            plugin->processBlock(buf, midi);
            midi.clear();
            if (peakL) *peakL = std::max(*peakL, buf.getMagnitude(0, 0, kBlock));
            if (peakR) *peakR = std::max(*peakR, buf.getMagnitude(1, 0, kBlock));
        }
    };
    play(3300, nullptr, nullptr);  // the boot: kBootMs 3200 plus a margin
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
    float l = 0, r = 0;
    play(1000, &l, &r);
    std::printf("  VST3 note peak: %.4f %.4f\n", l, r);
    CHECK(l > 0.001f && r > 0.001f);

    // The host-side editor makes the plugin's IPlugView, which builds the panel editor (A13 size).
    std::unique_ptr<juce::AudioProcessorEditor> ed(plugin->createEditorAndMakeActive());
    CHECK(ed != nullptr);
    if (ed) {
        std::printf("  VST3 editor: %d x %d\n", ed->getWidth(), ed->getHeight());
        CHECK(ed->getWidth() >= 1020 && ed->getHeight() >= 360);
    }
    ed.reset();
    plugin->releaseResources();
    plugin.reset();
    // The plugin used the temp folder: its close save (A11, after the boot) landed here.
    CHECK(dir.getChildFile("ks32-memory.bin").existsAsFile());
}

}  // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI gui;  // the VST3 host and editor need the message manager
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("ks32_app_tests", "", false);
    romFolder(dir.getChildFile("roms"));
    memoryFile(dir);
    processor(dir.getChildFile("proc"));
    parameters(dir.getChildFile("params"));
    follow(dir.getChildFile("follow"));
    vst3(dir.getChildFile("vst3"));
    dir.deleteRecursively();
    std::printf(failures == 0 ? "app_tests: all passed\n" : "app_tests: %d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
