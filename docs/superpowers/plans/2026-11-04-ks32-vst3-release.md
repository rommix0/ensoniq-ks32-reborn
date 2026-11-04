# KS-32 VST3 Release Implementation Plan (Plan 8)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A KS-32 VST3 that a DAW project brings back as it was left (each instance's memory, sound, slider and wheel positions), with the analog controls and the sound automatable from the host, and the on-screen keys showing notes from the track.

**Architecture:**
- **A bounded hunt** (Task 1) runs the firmware to find which sound selections a program change brings back after a warm boot.
- **The JUCE-free engine** (Task 2) learns the current sound from the firmware's MIDI out and from MIDI in. It sends a requested sound once the boot has ended, applies analog values from the audio side after the boot, and keeps a mask of the notes held on MIDI in.
- **The processor** (Tasks 3 and 4) gets five `AudioProcessorValueTreeState` parameters. The editor drives the parameters instead of the engine. `get/setStateInformation` carry the RAM, the sound and the parameters. A memory mode sets the rules for each format: the Standalone keeps the memory file, and the VST3 keeps the project.

**Tech Stack:** C++17, CMake 3.20+, `tests/check.h`; JUCE 8.0.16-security (already fetched by `app/CMakeLists.txt`), MSVC x64 for `app/`.

**Spec:** `docs/design/2026-11-04-ks32-vst3-release-design.md` ("design"). Background: `docs/design/2026-10-28-ks32-juce-app-design.md` ("app design") and `docs/verification-plan7.md`.

## Global Constraints

- **Emulation stays pure.** `src/duart` and `src/ks32` keep Plan 6's rules: no JUCE, no OS calls, no threads, no file I/O, no mutable statics, no `float` or `double`. `src/app` is JUCE-free and OS-free, with no file I/O.
- **Determinism.** The Plan 6 hashes stay the same on both builds: boot trace `d2afd0fed8861cbd`, render `2bae46b3acc34cd0`, `ks32emu render tests/data/player_smoke.mid` PCM `a479ba867298cc55`.
- **Builds.** Everything except `app/` builds without warnings with VS 2022 x64 (`build-msvc/`, `/W4`) and MinGW g++ 14 plus Ninja (`build/`, `-Wall -Wextra`). The MinGW executables run from PowerShell with `C:\winlibs\mingw32\bin` on PATH. `app/` builds in `build-app/` (MSVC x64, `-DENS_BUILD_APP=ON`) without warnings from our own files.
- **Real time.** Once booted, nothing `Engine::render` or `processBlock` calls may allocate, lock (other than `try_lock`) or wait. `ens_rt_tests` enforces this.
- **No decompiling** (the user's ruling, 2026-10-23). Facts come from running the firmware and watching it. No ROM code is read and no RAM address is interpreted. Nothing ROM-derived is committed.
- **Never committed:** ROMs, traces, firmware listings, scratch harnesses, `docs/Ensoniq_KS32_Top.png`, JUCE sources, build outputs.
- **No downloads** are planned. JUCE is already in `build-app/_deps`; if it has to be fetched again, the controller asks the user first.
- **ROM tests** use `ENS_ROM_DIR`. Without the ROMs a test prints `  skip: ROMs not found` and passes.
- **The user's memory file is never touched by a test or a hunt.** Tests and the app checks set `KS32_SETTINGS_DIR` to a temporary folder.
- **Decisions.** Every call the design leaves to the builder, and each default below, gets a `// DECISION A<n>: <one line>` comment at the point of use and a `## A<n>: <title>` entry in `docs/decisions.md` under `# KS-32 app`, in the existing entries' format.
- **Stop and ask.** If the firmware or the code contradicts the design, the implementer stops and reports.
- **Branch.** Work on `plan8-vst3-release` in a worktree and commit after each task.

## Decision Defaults

| Id | Topic | Default | Task |
|---|---|---|---|
| A15 | Sound tracking and restore | The last program change seen after the boot, on the firmware's MIDI out or on MIDI in, is the current sound, with its channel and any bank bytes Task 1 finds. A requested sound is sent into MIDI in at a block start, once booted, by the rule in Task 2 Step 3. The boot's own `C0 00` is not tracked; the boot's end sets the current sound to 0. | 1, 2 |
| A16 | Analogs after the boot | The four analog parameters reach the machine only once `booted`. Until then the inputs stay at `kAnalogRest`, because the firmware takes the pitch wheel's power-on value as its centre (K13). | 2 |
| A17 | Parameters | `volume`, `dataEntry`, `pitchWheel`, `modWheel` are 0-1 control positions (0 bottom), and `sound` is an integer from 0 to 127. Position to raw: sliders `lround(p * 1023)`, wheels `lround((1 - p) * 1023)`. The defaults are the K13 rest positions. | 3 |
| A18 | Sound follows the firmware | A processor timer at 30 Hz sets `sound` from `Snapshot::program` when that value changes. The 60 s memory save becomes a counter on the same timer. | 3 |
| A19 | MIDI note mask | Note on with velocity > 0 sets the bit and note off or velocity 0 clears it, on any channel. CC 120 and CC 123 clear all notes. A new engine starts empty. Notes held by MIDI are drawn as pressed keys. | 2, 3 |
| A20 | State format | `copyXmlToBinary` of `<KS32State version="1">`. It holds the parameter tree, a `select` attribute (the replay bytes in hex, which decides `sound` on restore) and, in Project mode only, a `<RAM>` child with 65 536 bytes in base64. An unknown version, a missing RAM or a wrong-size RAM restores what it can and leaves the machine as it is. | 4 |
| A21 | Which RAM is saved | Once booted, the running RAM, from `requestRam`/`takeRam` with a 300 ms wait, then read directly under `suspendProcessing` if no block ran. Before the boot ends, the image the machine started from. With no machine, the image the state gave, unchanged. | 4 |
| A22 | Memory per format | `MemoryMode::File` (the Standalone) works as today. In `MemoryMode::Project` (every other wrapper): there is no close save and no 60 s save; Erase gives this instance a cold boot and leaves the file alone; "Copy to shared memory…" writes the RAM to the shared file once booted. | 4 |
| A23 | Standalone state | The Standalone's processor state carries the parameters and `select`, but not the RAM. JUCE's wrapper keeps that state in `KS-32.settings`. | 4 |

## Review Focus

1. **The project is saved during the boot** (within about 3 s of opening it, which a DAW's autosave can do). The state must carry the image the machine started from and the saved sound, never a half-booted RAM. Test: `state_saved_during_boot_keeps_start_image` (Task 4).
2. **The project opens with the ROMs missing or the folder moved.** The state handed in comes back out unchanged, so saving the project does not lose that instance's memory. Once the ROMs are good, the machine starts from it. Test: `state_without_roms_round_trips` (Task 4).
3. **A panel pick races the Sound parameter.** For up to one timer tick the parameter still holds the old sound. The engine must not send the old sound back. Test: `engine_panel_pick_is_not_undone` (Task 2).
4. **A damaged or foreign state blob** (garbage bytes, a RAM of the wrong size, a later version): no crash, and the machine keeps running. Test: `state_rejects_bad_blobs` (Task 4).
5. **Several instances in one host process.** No VST3 instance writes the shared memory file, on close or on a timer. Test: the `vst3` check's shared-file hash before and after (Task 4).

---

### Task 1: Which sounds come back (bounded hunt)

**Files:**
- Modify: `docs/ks32-hardware-interface.md` (the "MIDI out" and "Battery RAM" sections), `tests/test_app_engine.cpp` (written in Task 2, from this task's facts)
- Scratch, not committed: a harness on `Machine` in the scratchpad, as Plan 6 and Plan 7 Task 3 used.

**Interfaces:** Produces facts for Task 2. For each selection kind: the MIDI-out bytes the firmware sends when it is selected after the boot, and whether sending those bytes back into MIDI in after a warm boot from the saved RAM shows the same display.

- [ ] **Step 1:** Write the harness. It cold-boots a zero RAM image (never a user file) for 3300 ms, drains MIDI out, makes a selection with `Machine::button` presses (100 ms held, 300 ms apart), runs 500 ms and records the MIDI-out bytes and the display rows. Then it takes `ramImage()`, calls `loadRamImage` on it, boots 3300 ms, sends the recorded bytes with `midiIn`, runs 500 ms and records the display again.
- [ ] **Step 2:** Run it once for each of these selections, from the Select Sound screen:
  - ROM 01: Up/Yes;
  - ROM 55: Bank 5, then Screen 5;
  - INT 34: BankSet, then Bank 3, then Screen 4;
  - a preset: Select Seq/Preset, then Bank 3, which gives `PRESET 00 Loc=30`.

  If a selection's display does not show what is named here, record what it shows. Do not search further.
- [ ] **Step 3:** For each kind, write in the interface doc:
  - the bytes sent, including any `Bn 00`/`Bn 20` bank select before the `Cn`;
  - whether the replay brings the display back;
  - "How it was found";
  - "The firmware was run, never studied".
- [ ] **Step 4:** Commit: `docs(ks32): which sound selections a program change restores (A15)`.

### Task 2: The engine: current sound, requested sound, analogs, note mask

**Files:**
- Modify: `src/app/engine.h`, `src/app/engine.cpp`, `src/ks32/machine.h` (one getter), `tests/test_app_engine.cpp`, `tests/rt_main.cpp`, `docs/decisions.md` (A15, A16, A19)

**Interfaces:**
- Consumes: Task 1's facts (which bytes come before the `Cn`).
- Produces, for Tasks 3 and 4:
  - `uint16_t ks32::Machine::analog(Analog a) const`: the raw value last set (the rest value until then).
  - `Snapshot::program` (`int16_t`, -1 until the boot ends, then the current sound 0-127).
  - `Snapshot::select` (`std::array<uint8_t, 8>`) and `Snapshot::selectLen` (`uint8_t`): the bytes that select the current sound (the bank bytes Task 1 found, then `Cn pp`). `selectLen` is 0 until a selection has been seen or sent.
  - `Snapshot::notes` (`std::array<uint64_t, 2>`): the MIDI-in note mask, bit n is note n.
  - `Snapshot::programsSent` (`uint32_t`): the selections the engine has sent into MIDI in.
  - `void Engine::setProgram(int n)` (audio thread, 0-127): requests a sound.
  - `void Engine::setSelect(const uint8_t* bytes, int len)` (message thread, audio stopped, after `loadRam`): the exact bytes to replay after this boot, instead of a bare program change, when they select the same program as the request.
  - `void Engine::setAnalog(ks32::Analog a, uint16_t raw)` (audio thread): applied at a block start, once booted.

- [ ] **Step 1: Write the failing tests** in `tests/test_app_engine.cpp`. Each uses `bootedEngine()` / `makeEngine()` and skips without ROMs.
  - `engine_tracks_boot_as_program_zero`: before 3200 ms `snapshot().program == -1`; after `renderMs(3300)` it is 0, and `programsSent == 0`.
  - `engine_tracks_panel_pick`: on a booted engine, post Up/Yes down and up twice, 300 ms apart, and render 500 ms. Then `program == 2` and `select` ends `C0 02` (channel as the firmware sent it).
  - `engine_tracks_midi_in_program`: `midi(0, {0xC0, 0x05})`, render 500 ms. `program == 5`, the display shows `ROM   05`, and `programsSent == 0`.
  - `engine_restores_program_after_warm_boot`:
    - On engine A, booted, `midi` `C0 37` and render 500 ms. Take `machine().ramImage()`.
    - Create engine B, `loadRam` it, and call `setProgram(55)` before every block.
    - After 3300 ms plus 500 ms, B's display row 0 contains `ROM   55`, `program == 55` and `programsSent == 1`.
  - `engine_panel_pick_is_not_undone`: on a booted engine, call `setProgram(0)` before every block. A panel pick of Up/Yes gives `program == 1`. Then 500 ms more of `setProgram(0)` leaves `program == 1` and `programsSent == 0`. Then `setProgram(1)` for 100 ms leaves `programsSent == 0`. Then `setProgram(7)` gives `programsSent == 1`, the display shows `ROM   07` and `program == 7`.
  - `engine_restores_task1_kinds`: for each kind Task 1 found to come back (INT 34 and the preset, if they did), the same round trip as above with `setSelect`, and the display as Task 1 recorded it. A kind that did not come back gets no test.
  - `engine_analog_waits_for_boot`: on a fresh engine, `setAnalog(PitchWheel, 0x100)` before every block. At 1000 ms `machine().analog(PitchWheel) == 0x200`, and after 3300 ms it is `0x100`.
  - `engine_note_mask`: on a booted engine:
    - `90 3C 64` sets bit 60, and `94 40 64` (channel 5) sets bit 64;
    - `80 3C 00` clears bit 60, and `94 40 00` clears bit 64;
    - `90 3C 64` and then `B0 7B 00` leave the mask zero.
- [ ] **Step 2: Run them to make sure they fail.** Run `cmake --build build && .\build\ens_tests.exe`. Expect compile errors (`program`, `setProgram`, `analog` and the rest are not declared).
- [ ] **Step 3: Implement.**
  - **`Machine::analog`** returns the multiplexer input for `a` (`mux_` at that select).
  - **A running-status MIDI parser** (`struct MidiParse` in `engine.cpp`) for two streams: the bytes `midi()` stores, parsed as they are fed in `feedMidi`, and the MIDI-out bytes drained in `render`. Real-time bytes (`F8`-`FF`) do not break running status. Sysex is skipped until `F7` or the next status byte.
  - **From MIDI in:** track the program and bank, and update the note mask.
  - **From MIDI out:** track the program and bank only once booted.
  - **The send rule (A15).** The tests leave this open, so it is written here:

```cpp
// requested_ = -1, current_ = -1, dirty_ = false at create and loadRam.
void Engine::setProgram(int n) { if (n != requested_) { requested_ = n; dirty_ = true; } }
// when frames_ first reaches kBootFrames:  current_ = 0; dirty_ = true;
// a program change tracked after the boot: current_ = p;   (requested_ and dirty_ untouched)
// at each block start, before the stored MIDI is fed:
if (booted && dirty_) {
    if (requested_ >= 0 && requested_ != current_) {
        send(selectFor(requested_));  // setSelect's bytes if their program == requested_, else Cn requested_
        current_ = requested_;        // n = the channel last seen in a selection, else 0
        ++programsSent_;
    }
    dirty_ = false;
}
```

  - **Analogs (A16):** `setAnalog` stores the wanted raw value per input. At each block start, once booted, the inputs that differ from `Machine::analog` are applied.
  - **`loadRam`** clears the tracker, the note mask and the wanted analogs, as a power cycle.
  - **The snapshot** publishes the new fields in the existing `try_lock` copy.
- [ ] **Step 4: Run the tests and make sure they pass.** Run on both builds: `.\build\ens_tests.exe` and `.\build-msvc\Release\ens_tests.exe`. All pass, and the printed boot trace and render hashes are unchanged.
- [ ] **Step 5: Extend `rt_engine_render_does_not_allocate`.** Inside the counted span, add: a `C0 05` on MIDI in, `setProgram(9)` for a block, `setAnalog(Volume, 0x200)`, and note on and off. Run `.\build\ens_rt_tests.exe` and `.\build-msvc\Release\ens_rt_tests.exe`: 3 passed, allocations 0.
- [ ] **Step 6:** Add A15, A16 and A19 to `docs/decisions.md`. Commit: `feat(app): engine tracks and restores the sound, analogs after boot, MIDI note mask (A15, A16, A19)`.

### Task 3: Parameters, automation and the editor

**Files:**
- Create: `src/app/analog_map.h` (JUCE-free, header-only)
- Modify: `app/source/Processor.h`, `app/source/Processor.cpp`, `app/source/PanelEditor.cpp`, `tests/test_app_panel_layout.cpp`, `app/tests/app_tests.cpp`, `docs/decisions.md` (A17, A18)

**Interfaces:**
- Consumes: Task 2's `Engine::setProgram`, `Engine::setAnalog` and `Snapshot::program`/`notes`.
- Produces, for Task 4:
  - `uint16_t ens::app::analogRaw(ks32::Analog a, float position)` and `float ens::app::analogPosition(ks32::Analog a, uint16_t raw)` in `analog_map.h`. The wheels (`PitchWheel`, `ModWheel`) are inverted; `Volume` and `DataEntry` are not.
  - `juce::AudioProcessorValueTreeState& Ks32Processor::params()`, with the parameter IDs `volume`, `dataEntry`, `pitchWheel`, `modWheel` and `sound` (A17).

- [ ] **Step 1: Write the failing tests.**
  - In `tests/test_app_panel_layout.cpp`, `analog_map_round_trips_rest_values`. For each of the four inputs, `analogRaw(a, analogPosition(a, kAnalogRest[a])) == kAnalogRest[a]`. Also:
    - `analogRaw(PitchWheel, 0.5f) == 0x200`, and `analogRaw(ModWheel, 0.0f) == 0x3FF`;
    - `analogRaw(Volume, 1.0f) == 0x3FF`, and `analogRaw(Volume, 0.0f) == 0`.
  - In `app/tests/app_tests.cpp`, a new `parameters(dir)`:
    - A `Ks32Processor` on the dummy ROMs has exactly five parameters with those IDs, and the names `Volume`, `Data Entry`, `Pitch Wheel`, `Mod Wheel` and `Sound`.
    - `sound` runs from 0 to 127.
    - The defaults give `analogRaw` equal to `kAnalogRest` for the four analogs.
- [ ] **Step 2: Run them to make sure they fail.** Run `.\build\ens_tests.exe`, then build `KS32_AppTests` and run it. Expect compile errors.
- [ ] **Step 3: Implement.**
  - **`analog_map.h`**, with lround as in A17.
  - **The processor** owns an `AudioProcessorValueTreeState` built from a parameter layout:
    - four `AudioParameterFloat` from 0 to 1, defaulting to `analogPosition(rest)`;
    - one `AudioParameterInt` from 0 to 127, defaulting to 0.
  - **`processBlock`**, before `render`: calls `setAnalog(a, analogRaw(a, value))` for the four analogs and `setProgram(sound)`, reading the APVTS atomic raw values.
  - **The timer** runs at 30 Hz (A18) and calls a public `void tick()`, which the tests call directly (no message loop runs in `app_tests`). When `snapshot().program` is 0 or more and differs from the last value seen, the processor sets `sound` with `setValueNotifyingHost` (a begin/end gesture around it). The 60 s save counts 1800 ticks.
- [ ] **Step 4: Change the editor.**
  - **`AnalogPart`** reads and writes its parameter instead of posting `Event::Kind::Analog`:
    - `mouseDown` calls `beginChangeGesture`;
    - a drag sets the parameter;
    - `mouseUp` sets the pitch wheel back to 0.5 and then calls `endChangeGesture`;
    - `paint` draws the parameter's position, at the editor's existing redraw.
  - **Remove** `AnalogPart`'s `sync`, its `sent_` and the analog part of `reset`.
  - **The keys:** a key whose bit is set in `snapshot().notes` is drawn pressed, as a mouse-held key is.
- [ ] **Step 5: Run the tests and make sure they pass.** Run `.\build\ens_tests.exe` and `.\build-msvc\Release\ens_tests.exe` (all pass). Then build `KS32_Standalone`, `KS32_VST3` and `KS32_AppTests` and run `KS32_AppTests.exe`: `app_tests: all passed`.
- [ ] **Step 6: Manual check, by the controller.** Start the Standalone, drag Volume, close the editor (Options menu, or the window) and reopen it.
  - Volume is drawn where it was left.
  - A Q25 note lights its key.
  - Up/Yes on the panel shows the new sound.
- [ ] **Step 7:** Add A17 and A18 to `docs/decisions.md`. Commit: `feat(app): host parameters for the analogs and the sound; keys show MIDI notes (A17, A18)`.

### Task 4: Plugin state and memory per format

**Files:**
- Modify: `app/source/Processor.h`, `app/source/Processor.cpp`, `app/source/SettingsPage.cpp` (and `.h`), `app/tests/app_tests.cpp`, `docs/decisions.md` (A20-A23)

**Interfaces:**
- Consumes: `params()` (Task 3), `Snapshot::select`/`selectLen` and `Engine::setSelect` (Task 2), and `saveMemoryFile`/`loadMemoryFile` (`MemoryFile.h`).
- Produces:
  - `enum class MemoryMode { File, Project };`
  - `explicit Ks32Processor(MemoryMode mode = defaultMemoryMode())`, where `defaultMemoryMode()` is `File` for `wrapperType_Standalone` (from `juce::PluginHostType::getPluginLoadedAs()`) and `Project` otherwise;
  - `MemoryMode Ks32Processor::memoryMode() const`;
  - `bool Ks32Processor::copyToSharedMemory(juce::String& err)`;
  - `getStateInformation` and `setStateInformation` (A20).

- [ ] **Step 1: Read the JUCE source** of `StandalonePluginHolder` (`build-app/_deps/juce-src/modules/juce_audio_plugin_client/Standalone/`) to confirm that it saves the processor state on shut-down and reloads it at start. If it does not, stop and report: A23 is then dropped and the design is updated.
- [ ] **Step 2: Write the failing tests** in `app/tests/app_tests.cpp`. They construct `Ks32Processor` directly on the real ROMs (skip without them), call `prepareToPlay(48000, 480)` and run blocks with a helper `run(p, ms, midi)`. Existing tests that rely on today's file behaviour pass `MemoryMode::File`.
  - `state_round_trip_project`. On processor A (Project):
    - boot;
    - post System/MIDI, then Up/Yes twice, so Master Tune reads `+02`;
    - post Select Sound, then MIDI `C0 37`;
    - set `volume` to 0.5;
    - run 500 ms and take the state.

    Processor B (Project) gets `setStateInformation` and runs 3800 ms. Then:
    - B's display shows `ROM   55`;
    - `volume` is 0.5, and `machine().analog(Volume) == analogRaw(Volume, 0.5f)`;
    - after posting System/MIDI, B's display shows `+02`.
  - `state_saved_during_boot_keeps_start_image`: B as above, but its state is taken 1000 ms after `setStateInformation`. The `<RAM>` equals A's RAM byte for byte, and `select` still names `C0 37`.
  - `state_without_roms_round_trips`: a Project processor whose settings name an empty ROM folder gets A's state. Its `getStateInformation` returns the same RAM and `select`. After `setRomFolder` points it at the real ROMs, it boots to `ROM   55`.
  - `state_rejects_bad_blobs`: on a booted Project processor, `setStateInformation` with 100 random bytes, with a valid XML holding a RAM of 1000 bytes, and with `version="2"`. After each one `engine()` is the same pointer and the next 100 ms render without a crash.
  - `project_mode_leaves_shared_file`: hash the shared memory file in the temp settings folder. A Project processor boots, calls `tick()` 1900 times (past the 60 s save) and runs Erase memory: the hash is unchanged. A second Project processor boots and is destroyed: still unchanged. Then, on a third that has booted, `copyToSharedMemory` makes the file equal to its RAM.
  - `standalone_state_has_no_ram`: a File processor's state has no `<RAM>` child.
  - In `vst3(dir)`: hash `ks32-memory.bin` in the temp folder before the instance is created and check it is unchanged after `plugin.reset()`. This replaces the old check that the close save landed there.

    Then a second instance, given the first instance's `getStateInformation` (taken after MIDI `C0 37`), reports `sound` equal to 55 through `getParameters()` after 3800 ms.
- [ ] **Step 3: Run them to make sure they fail.** Build `KS32_AppTests` and run it. Expect compile errors, then FAIL lines.
- [ ] **Step 4: Implement.**
  - **Memory mode.** Add `MemoryMode`. In Project mode:
    - the destructor does not save;
    - the timer does not save;
    - `eraseMemory` restarts from 64 KB of zeros without writing the file;
    - the constructor still starts from the shared file (the design's "new instance" rule).
  - **The start image.** Keep it (`startImage_`) on every restart (A21).
  - **`getStateInformation`** builds the A20 XML. The RAM follows A21: never wait on the audio thread, and on the message thread wait at most 300 ms in 5 ms steps for `takeRam`.
  - **`setStateInformation`** replaces the parameter state. In Project mode, a 65 536-byte RAM:
    - restarts the machine from it (a `restart` taking an explicit image);
    - then `setSelect` from `select`;
  - **`select` decides the sound.** When `select` is present, `sound` is set to its program on restore, so a state taken before the 30 Hz timer caught up still brings back the right sound;
    - with no ROMs, is kept as the start image, and `setRomFolder` uses it when the ROMs become good.
  - **Bad input** is ignored as A20 says.
  - **`copyToSharedMemory`** refuses before the boot (err) and saves the A21 RAM with `saveMemoryFile`. Its result goes through `saved()`.
- [ ] **Step 5: The settings page in Project mode.**
  - The memory note reads: "This instance's memory is saved in the project. New instances start from the shared memory file."
  - A "Copy to shared memory…" button asks first ("Write this instance's memory to the shared memory file? New instances and the Standalone will start from it.", with **Copy** and **Cancel**), then calls `copyToSharedMemory`. An error goes to the same alert as **Change…**'s.
  - The Erase confirmation text says it erases this instance only.
  - File mode is unchanged.
- [ ] **Step 6: Run the tests and make sure they pass.** Build `KS32_Standalone`, `KS32_VST3` and `KS32_AppTests`; `KS32_AppTests.exe` prints `app_tests: all passed`, with the VST3 lines. Run `ens_tests` on both builds (the hashes are unchanged) and `ens_rt_tests` (allocations 0).
- [ ] **Step 7: Manual check, by the controller.**
  - Standalone: pick ROM 55 and lower Volume, close, reopen. After the boot it is on ROM 55 with Volume where it was, and Master Tune keeps its value from the memory file.
- [ ] **Step 8:** Add A20 to A23 to `docs/decisions.md`. Commit: `feat(app): plugin state in the project, memory per format (A20-A23)`.

### Task 5: Documents and verification

**Files:**
- Create: `docs/verification-plan8.md`
- Modify: `docs/ks32app.md`, `docs/verification-plan7.md` (the open items this plan closes: "Several VST3 instances share one memory file", "No plugin state" and "Reopening the VST3 editor"), `docs/design/2026-11-04-ks32-vst3-release-design.md` (status line only)

- [ ] **Step 1: Update `docs/ks32app.md`.**
  - VST3: the state in the project and the per-format memory table (design section 6.5), "Copy to shared memory…", and the five parameters.
  - The Sound parameter shows numbers.
  - Task 1's limits (which selections come back), and "Prog Change= OFF" stops the restore.
  - Each instance is a whole KS-32 (CPU).
  - Rewrite "It always powers on to ROM 00": the firmware still boots to ROM 00, and the app selects the saved sound once the boot has ended.
  - Rewrite the "Several VST3 instances" bullet: the VST3 no longer writes the shared file by itself.
  - Notes from MIDI light the keys.
- [ ] **Step 2: Write `docs/verification-plan8.md`** in `verification-plan7.md`'s shape:
  - the suite counts on both builds and the three hashes;
  - the RT tests;
  - `app_tests`;
  - Task 1's table;
  - the manual rows for the user, each "user to run", in FL Studio or Cakewalk:
    - **D1:** one KS-32 on ROM 55, Master Tune +02, Volume lowered. Save, close and reopen the project. It comes back on ROM 55, at +02, with Volume as left.
    - **D2:** two KS-32s on different sounds and Master Tune values. Save and reopen. Each comes back as it was, and the time stamp of `%APPDATA%\KS-32\ks32-memory.bin` has not changed.
    - **D3:** add a new KS-32. It starts with the shared file's Master Tune.
    - **D4:** "Copy to shared memory…", then add another KS-32. It has the copied Master Tune.
    - **D5:** draw a Volume automation lane and play. The slider and the level follow it.
    - **D6:** draw a Sound lane. The display changes sound. Then pick a sound on the panel with the lane off: the host's Sound value follows.
    - **D7:** play notes from the track. The keys light.
- [ ] **Step 3:** Move the three closed items in `docs/verification-plan7.md`'s "Left open" to "closed by Plan 8", with one line each. Set the design's status line to "implemented by Plan 8, pending user acceptance".
- [ ] **Step 4:** Commit: `docs: Plan 8 readme, verification`.
