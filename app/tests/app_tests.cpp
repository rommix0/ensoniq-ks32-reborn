// KS-32 app checks: checkRomFolder on a temp folder of dummy files the right size (never ROM
// content), the memory file's load and save, the processor's memory-file guards, and (ruling R8) the built KS-32.vst3 loaded,
// played and its editor opened through JUCE's own VST3 host, as a DAW would. On the real ROMs: the
// sound follow (A18), the plugin state and the memory per format (A20-A23).
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

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
    Ks32Processor p(MemoryMode::File);  // the memory file's own rules (A11)
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

// The state checks (A20-A23) on the real ROMs, with 10 ms blocks and no tick(), so `sound` has not
// followed a pick: a state taken before the 30 Hz timer caught up.
bool realRoms(const juce::File& dir, const juce::File& romDir = juce::File(KS32_ROM_DIR)) {
    if (!juce::File(KS32_ROM_DIR).getChildFile("sq1-u25.bin").existsAsFile()) {
        std::printf("  skip: ROMs not found\n");
        return false;
    }
    _putenv_s("KS32_SETTINGS_DIR", dir.getFullPathName().toRawUTF8());
    AppSettings().setRomFolder(romDir);
    return true;
}

void run(Ks32Processor& p, int ms, juce::MidiBuffer midi = {}) {
    juce::AudioBuffer<float> buf(2, 480);
    for (int i = 0; i < ms / 10; ++i) {
        buf.clear();
        p.processBlock(buf, midi);
        midi.clear();
    }
}

void press(Ks32Processor& p, ens::ks32::Button b) {
    p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), true, 0});
    run(p, 100);
    p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), false, 0});
    run(p, 200);
}

juce::MidiBuffer programChange(int n) {
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage(0xC0, n), 0);
    return m;
}

bool shows(Ks32Processor& p, const char* text) {
    const auto panel = p.engine()->snapshot().panel;
    return ens::ks32::rowText(panel, 0).find(text) != std::string::npos ||
           ens::ks32::rowText(panel, 1).find(text) != std::string::npos;
}

juce::MemoryBlock stateOf(juce::AudioProcessor& p) {
    juce::MemoryBlock m;
    p.getStateInformation(m);
    return m;
}

std::unique_ptr<juce::XmlElement> xmlOf(const juce::MemoryBlock& s) {
    return juce::AudioProcessor::getXmlFromBinary(s.getData(), static_cast<int>(s.getSize()));
}

std::vector<uint8_t> ramOf(const juce::MemoryBlock& s) {
    const auto xml = xmlOf(s);
    const auto* ram = xml ? xml->getChildByName("RAM") : nullptr;
    if (!ram) return {};
    juce::MemoryOutputStream out;
    if (!juce::Base64::convertFromBase64(out, ram->getAllSubText())) return {};
    const auto* b = static_cast<const uint8_t*>(out.getData());
    return {b, b + out.getDataSize()};
}

juce::String selectOf(const juce::MemoryBlock& s) {
    const auto xml = xmlOf(s);
    return xml ? xml->getStringAttribute("select") : juce::String();
}

juce::MemoryBlock fileBytes(const juce::File& f) {
    juce::MemoryBlock m;
    f.loadFileAsData(m);  // empty when there is no file
    return m;
}

int soundOf(juce::AudioProcessor& p) {
    for (auto* q : p.getParameters())
        if (q->getName(64) == "Sound") return juce::roundToInt(q->getValue() * 127.0f);
    return -1;
}

// state_round_trip_project: Master Tune +02, ROM 55 by MIDI and Volume 0.5 come back in a new instance
// from the state alone. Returns A's state for the checks after it (empty when skipped).
juce::MemoryBlock stateRoundTrip(const juce::File& dir) {
    if (!realRoms(dir)) return {};
    using ens::ks32::Button;
    Ks32Processor a;
    CHECK(a.memoryMode() == MemoryMode::Project);  // constructed outside a wrapper
    if (!a.engine()) return {};
    a.prepareToPlay(48000, 480);
    run(a, 3400);
    press(a, Button::SystemMidi);
    press(a, Button::UpYes);
    press(a, Button::UpYes);
    CHECK(shows(a, "+02"));
    press(a, Button::SelectSound);
    run(a, 200, programChange(0x37));
    a.params().getParameter("volume")->setValueNotifyingHost(0.5f);
    run(a, 500);
    CHECK(shows(a, "ROM   55"));
    const auto s = stateOf(a);
    CHECK(ramOf(s).size() == static_cast<size_t>(kMemoryBytes) && selectOf(s).endsWithIgnoreCase("c037"));

    Ks32Processor b;
    b.prepareToPlay(48000, 480);
    b.setStateInformation(s.getData(), static_cast<int>(s.getSize()));
    run(b, 3800);
    CHECK(b.engine() != nullptr && shows(b, "ROM   55"));
    using ens::ks32::Analog;
    CHECK(*b.params().getRawParameterValue("volume") == 0.5f &&
          b.engine()->machine().analog(Analog::Volume) == ens::app::analogRaw(Analog::Volume, 0.5f));
    press(b, Button::SystemMidi);
    CHECK(shows(b, "+02"));
    return s;
}

// state_saved_during_boot_keeps_start_image (anti-goal: chaining short boots).
void stateDuringBoot(const juce::File& dir, const juce::MemoryBlock& a) {
    if (a.isEmpty() || !realRoms(dir)) return;
    Ks32Processor b;
    b.prepareToPlay(48000, 480);
    b.setStateInformation(a.getData(), static_cast<int>(a.getSize()));
    run(b, 1000);
    const auto s = stateOf(b);
    CHECK(ramOf(s) == ramOf(a) && selectOf(s) == selectOf(a));
}

// state_without_roms_round_trips (anti-goal: losing a project's memory).
void stateWithoutRoms(const juce::File& dir, const juce::MemoryBlock& a) {
    const auto empty = dir.getChildFile("no-roms");
    empty.createDirectory();
    if (a.isEmpty() || !realRoms(dir, empty)) return;
    Ks32Processor p;
    CHECK(p.engine() == nullptr);
    p.prepareToPlay(48000, 480);
    p.setStateInformation(a.getData(), static_cast<int>(a.getSize()));
    const auto s = stateOf(p);
    CHECK(ramOf(s) == ramOf(a) && selectOf(s) == selectOf(a));
    juce::String err;
    CHECK(p.setRomFolder(juce::File(KS32_ROM_DIR), err) && p.engine() != nullptr);
    if (!p.engine()) return;
    run(p, 3800);
    CHECK(shows(p, "ROM   55"));
}

// standalone_state_before_boot (goal 5, A23): a File processor given a Project state (ROM 55, Master Tune
// +02) before its first block comes back on ROM 55, its memory still from the file (cold here: not +02).
void standaloneStateBeforeBoot(const juce::File& dir, const juce::MemoryBlock& a) {
    if (a.isEmpty() || !realRoms(dir)) return;
    Ks32Processor p(MemoryMode::File);
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    p.setStateInformation(a.getData(), static_cast<int>(a.getSize()));
    run(p, 3800);
    CHECK(shows(p, "ROM   55"));
    press(p, ens::ks32::Button::SystemMidi);
    std::printf("  standalone display: [%s]\n", (ens::ks32::rowText(p.engine()->snapshot().panel, 0) +
                                                 ens::ks32::rowText(p.engine()->snapshot().panel, 1)).c_str());
    CHECK(shows(p, "Master Tune") && !shows(p, "+02"));
}

// int00_survives_restarts (A15, I1/M1): INT 00 differs from the boot's ROM 00 only in its bank. It comes
// back from a state, across a ROM-folder change and across Change... to a file; Erase then gives a cold
// machine on ROM 00 with `sound` 0, nothing sent.
void int00Restarts(const juce::File& dir) {
    if (!realRoms(dir)) return;
    using ens::ks32::Button;
    juce::MemoryBlock s;
    {
        Ks32Processor a;
        if (!a.engine()) return;
        a.prepareToPlay(48000, 480);
        run(a, 3400);
        press(a, Button::BankSet);
        press(a, Button::Bank0);
        press(a, Button::Screen0);
        CHECK(shows(a, "INT   00"));
        s = stateOf(a);
    }
    CHECK(selectOf(s).equalsIgnoreCase("b000002000c000"));
    Ks32Processor b;
    b.prepareToPlay(48000, 480);
    b.setStateInformation(s.getData(), static_cast<int>(s.getSize()));
    run(b, 3800);
    CHECK(shows(b, "INT   00"));
    juce::String err;
    CHECK(b.setRomFolder(juce::File(KS32_ROM_DIR), err));
    run(b, 3800);
    CHECK(shows(b, "INT   00"));
    const auto file = dir.getChildFile("int00.bin");
    CHECK(saveMemoryFile(file, ramOf(s)) && b.setMemoryFile(file, err));
    run(b, 3800);
    CHECK(shows(b, "INT   00"));

    auto* sound = b.params().getParameter("sound");
    sound->setValueNotifyingHost(sound->convertTo0to1(5.0f));
    run(b, 500);
    CHECK(shows(b, "05"));
    b.eraseMemory();
    CHECK(soundOf(b) == 0);
    run(b, 3800);
    CHECK(shows(b, "ROM   00") && b.engine()->snapshot().programsSent == 0);
}

// state_rejects_bad_blobs: garbage, a short RAM and an unknown version leave the machine as it is.
void stateBadBlobs(const juce::File& dir) {
    if (!realRoms(dir)) return;
    Ks32Processor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    run(p, 3400);
    const auto blob = [](const char* version, size_t ramBytes) {
        juce::XmlElement x("KS32State");
        x.setAttribute("version", version);
        const std::vector<uint8_t> ram(ramBytes, 0x55);
        x.createNewChildElement("RAM")->addTextElement(juce::Base64::toBase64(ram.data(), ram.size()));
        juce::MemoryBlock m;
        juce::AudioProcessor::copyXmlToBinary(x, m);
        return m;
    };
    juce::MemoryBlock noise(100);
    juce::Random rng(8);
    for (size_t i = 0; i < noise.getSize(); ++i) noise[i] = static_cast<char>(rng.nextInt(256));
    for (const auto& s : {noise, blob("1", 1000), blob("2", static_cast<size_t>(kMemoryBytes))}) {
        const auto* engine = p.engine();
        p.setStateInformation(s.getData(), static_cast<int>(s.getSize()));
        CHECK(p.engine() == engine);
        run(p, 100);
    }
}

// project_mode_leaves_shared_file (A22): no 60 s save, no close save, Erase spares the file; only
// copyToSharedMemory writes it, once booted.
void projectLeavesSharedFile(const juce::File& dir) {
    if (!realRoms(dir)) return;
    const auto shared = AppSettings().memoryFile();
    CHECK(eraseMemoryFile(shared));
    const auto before = fileBytes(shared);
    {
        Ks32Processor p;
        if (!p.engine()) return;
        p.prepareToPlay(48000, 480);
        run(p, 3400);
        for (int i = 0; i < 1900; ++i) p.tick();
        const auto* engine = p.engine();
        p.eraseMemory();
        CHECK(p.engine() != engine && p.memoryError().isEmpty());
        CHECK(fileBytes(shared) == before);
    }
    {
        Ks32Processor p;
        p.prepareToPlay(48000, 480);
        run(p, 3400);
    }
    CHECK(fileBytes(shared) == before);
    Ks32Processor p;
    p.prepareToPlay(48000, 480);
    juce::String err;
    CHECK(!p.copyToSharedMemory(err) && err.isNotEmpty());  // not booted
    run(p, 3400);
    CHECK(p.copyToSharedMemory(err));
    const auto& ram = p.engine()->machine().ramImage();
    CHECK(fileBytes(shared) == juce::MemoryBlock(ram.data(), ram.size()));
}

// A21 across threads: processBlock runs on its own thread while saves come from the message thread, a
// third thread and the audio thread itself. Each holds the running RAM with Master Tune +02, set after
// the boot, never the start image (64 KB of zeros here).
void stateThreads(const juce::File& dir) {
    if (!realRoms(dir)) return;
    CHECK(eraseMemoryFile(AppSettings().memoryFile()));
    Ks32Processor p;
    if (!p.engine()) return;
    p.prepareToPlay(48000, 480);
    std::atomic<bool> stop{false}, onAudio{false};
    juce::MemoryBlock fromAudio;
    std::thread audio([&] {
        juce::AudioBuffer<float> buf(2, 480);
        juce::MidiBuffer midi;
        while (!stop) {
            buf.clear();
            p.processBlock(buf, midi);
            if (onAudio.load()) {
                fromAudio = stateOf(p);
                onAudio = false;
            }
        }
    });
    const auto wait = [&](int ms) {  // machine time
        const auto until = p.engine()->snapshot().frames + static_cast<uint64_t>(ms) * ens::ks32::kWavRate / 1000;
        while (p.engine()->snapshot().frames < until) juce::Thread::sleep(1);
    };
    const auto hold = [&](ens::ks32::Button b) {
        p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), true, 0});
        wait(100);
        p.engine()->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(b), false, 0});
        wait(200);
    };
    wait(3400);
    hold(ens::ks32::Button::SystemMidi);
    hold(ens::ks32::Button::UpYes);
    hold(ens::ks32::Button::UpYes);
    CHECK(shows(p, "+02"));
    const auto fromMessage = stateOf(p);
    juce::MemoryBlock fromOther;
    std::thread([&] { fromOther = stateOf(p); }).join();
    onAudio = true;
    while (onAudio.load()) juce::Thread::sleep(1);
    stop = true;
    audio.join();

    const std::vector<uint8_t> zeros(static_cast<size_t>(kMemoryBytes), 0);
    const juce::MemoryBlock* states[] = {&fromMessage, &fromOther, &fromAudio};
    for (const auto* s : states) {
        const auto ram = ramOf(*s);
        CHECK(ram.size() == static_cast<size_t>(kMemoryBytes) && ram != zeros);
        Ks32Processor q;
        q.prepareToPlay(48000, 480);
        q.setStateInformation(s->getData(), static_cast<int>(s->getSize()));
        run(q, 3800);
        press(q, ens::ks32::Button::SystemMidi);
        CHECK(shows(q, "+02"));
    }
}

// standalone_state_has_no_ram (A23): the memory file is the Standalone's only memory.
void standaloneNoRam(const juce::File& dir) {
    const auto roms = dir.getChildFile("roms");
    dummyRoms(roms);
    _putenv_s("KS32_SETTINGS_DIR", dir.getFullPathName().toRawUTF8());
    AppSettings().setRomFolder(roms);
    Ks32Processor p(MemoryMode::File);
    CHECK(p.memoryMode() == MemoryMode::File);
    const auto xml = xmlOf(stateOf(p));
    CHECK(xml != nullptr && xml->getChildByName("RAM") == nullptr);
}

// rom_folder_beside_app: with no folder chosen, a "roms" folder beside the app is taken and stored
// (the dist layout); a chosen folder wins over it.
void romsBesideApp(const juce::File& dir) {
    const auto beside =
        juce::File::getSpecialLocation(juce::File::currentApplicationFile).getSiblingFile("roms");
    CHECK(!beside.exists());  // never a real folder of the user's
    if (beside.exists()) return;
    dummyRoms(beside);
    _putenv_s("KS32_SETTINGS_DIR", dir.getFullPathName().toRawUTF8());
    {
        Ks32Processor p(MemoryMode::File);
        CHECK(p.engine() != nullptr && p.settings().romFolder() == beside);
    }
    const auto chosen = dir.getChildFile("chosen");
    dummyRoms(chosen);
    AppSettings().setRomFolder(chosen);
    {
        Ks32Processor p(MemoryMode::File);
        CHECK(p.settings().romFolder() == chosen);
    }
    beside.deleteRecursively();
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

    const auto shared = dir.getChildFile("ks32-memory.bin");
    const auto before = fileBytes(shared);  // A22: a VST3 instance never writes the shared file

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
    midi.addEvent(juce::MidiMessage(0xC0, 0x37), 0);
    play(500, nullptr, nullptr);
    const auto s = stateOf(*plugin);
    plugin->releaseResources();
    plugin.reset();
    CHECK(fileBytes(shared) == before);

    // A second instance given that state: `sound` reads 55 through the host's parameters.
    plugin = format.createInstanceFromDescription(*types[0], kRate, kBlock, err);
    CHECK(plugin != nullptr);
    if (!plugin) return;
    plugin->setPlayConfigDetails(0, 2, kRate, kBlock);
    plugin->prepareToPlay(kRate, kBlock);
    plugin->setStateInformation(s.getData(), static_cast<int>(s.getSize()));
    play(3800, nullptr, nullptr);
    std::printf("  VST3 restored sound: %d\n", soundOf(*plugin));
    CHECK(soundOf(*plugin) == 55);
    plugin->releaseResources();
    plugin.reset();
    CHECK(fileBytes(shared) == before);
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
    const auto a = stateRoundTrip(dir.getChildFile("state"));
    stateDuringBoot(dir.getChildFile("boot"), a);
    stateWithoutRoms(dir.getChildFile("noroms"), a);
    standaloneStateBeforeBoot(dir.getChildFile("standalone-state"), a);
    int00Restarts(dir.getChildFile("int00"));
    stateBadBlobs(dir.getChildFile("bad"));
    projectLeavesSharedFile(dir.getChildFile("shared"));
    standaloneNoRam(dir.getChildFile("standalone"));
    romsBesideApp(dir.getChildFile("beside"));
    stateThreads(dir.getChildFile("threads"));
    vst3(dir.getChildFile("vst3"));
    dir.deleteRecursively();
    std::printf(failures == 0 ? "app_tests: all passed\n" : "app_tests: %d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
