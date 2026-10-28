# Plan 7 verification

Closing check of Plan 7 (the KS-32 app: the real-time-safe machine, the JUCE-free engine, the transport hunt, and the JUCE 8 Standalone and VST3 with the front-panel editor, the settings page and the battery memory file): the test suites on both toolchains, the real-time tests, the three Plan 6 hashes, the app checks and the headless VST3 check, and every manual check that needs a person, left for the user's acceptance pass (ruling R1). What is left open is listed at the end.

The usage is in `docs/ks32app.md`. The decisions are `A1` to `A14` in `docs/decisions.md` (`# KS-32 app`). The facts the app relies on are in `docs/ks32-hardware-interface.md`, and the panel inventory in `docs/reference/ks32-front-panel.md`.

All the figures below are from runs on the final tree of the branch (the commit before the documents commit, `6ea1b55`, whose code the documents commit does not change), on 2026-10-28.

## Test counts

| Toolchain | Configure | Result |
|---|---|---|
| MinGW-w64 g++ 14.2.0, i686, Ninja, Release | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_ROM_DIR=...` | `ctest --test-dir build`: **3/3 passed** (`m68k_suite`, `ens_tests`, `ens_rt_tests`; 39.9 s). `build\ens_tests.exe`: **376 passed, 0 failed**, 36.8 s. `build\ens_rt_tests.exe`: **3 passed, 0 failed** |
| MSVC 19.44 (Visual Studio 2022), x64, Release | `cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DENS_ROM_DIR=...` | `ctest --test-dir build-msvc -C Release`: **3/3 passed** (37.2 s). `build-msvc\Release\ens_tests.exe`: **376 passed, 0 failed**, 36.1 s. `build-msvc\Release\ens_rt_tests.exe`: **3 passed, 0 failed** |
| MSVC 19.44, x64, Release, with the app | `cmake -S . -B build-app -G "Visual Studio 17 2022" -A x64 -DENS_BUILD_APP=ON -DENS_ROM_DIR=...` | `KS32_AppTests.exe`: **app_tests: all passed** (exit 0), including the VST3 check below. `ctest --test-dir build-app -C Release -R app_tests`: 1/1 passed |

All three were configured with `-DENS_ROM_DIR="C:/Users/abart/Desktop/Ensoniq KS-32/ks-32"`, so every firmware test runs and none skips. `build/` and `build-msvc/` compile with no warnings (`-Wall -Wextra`, `/W4`) and have `ENS_BUILD_APP` OFF, so they fetch no JUCE. The `build-app` build of `KS32_Standalone`, `KS32_VST3` and `KS32_AppTests` has no warnings in its log, JUCE's included.

Plan 6 ended at 357 tests; Plan 7 added 19 to `ens_tests` (the DUART and panel rings, the engine, the transport codes and the panel layout), the 3 tests of `ens_rt_tests`, and `app_tests`.

## Real-time tests (`ens_rt_tests`)

`ens_rt_tests` replaces the global `operator new` and `delete` (plain, array, sized, aligned and nothrow) and counts allocations between a start and a stop call (design section 7).

| Test | MinGW | MSVC |
|---|---|---|
| `rt_allocation_counter_counts` (the counter sees an allocation) | OK | OK |
| `rt_machine_runframe_does_not_allocate` (`runFrame` and the event calls, after the boot) | allocations: 0 | allocations: 0 |
| `rt_engine_render_does_not_allocate` (`Engine::render` with MIDI and posted events, after the boot) | allocations: 0, frames 157738 | allocations: 0, frames 157738 |

The engine's other locks are `try_lock` only (A5); the GUI reads the published snapshot, never the machine.

## Cross-toolchain determinism

The Plan 6 hashes did not change (design section 6.1). The values were printed by the tests and by `ks32emu` on each build:

| Value | MinGW | MSVC |
|---|---|---|
| Boot trace hash (`ks32_firmware_boot_is_deterministic`) | `d2afd0fed8861cbd` | `d2afd0fed8861cbd` |
| Render hash (`ks32_firmware_render_is_deterministic`) | `2bae46b3acc34cd0` | `2bae46b3acc34cd0` |
| `ks32emu render tests/data/player_smoke.mid` (7.75 s; the PCM hash) | `a479ba867298cc55` | `a479ba867298cc55` |
| The smoke WAV file, SHA-256 | `3E661FC0C1D831AA04288492B27BE4877AFFD7F0CD9A8520EE1B1AAD3538E4CB` | `3E661FC0C1D831AA04288492B27BE4877AFFD7F0CD9A8520EE1B1AAD3538E4CB` |

The smoke render printed "103 MIDI events, 230654 frames (7.750 s at 29762 Hz), peak 12781, MIDI out 0 bytes" on both. The WAV hash is the one Plan 6 recorded.

## The app checks (`app_tests`)

`app/tests/app_tests.cpp`, built as `KS32_AppTests` in `build-app`:

- **The ROM folder** (dummy files of the right sizes, never ROM content): six found, a renamed wave ROM reported missing, a short file reported wrong size, no folder chosen.
- **The memory file**: no file is a cold boot with no error, save and load, save over an existing file, no `.tmp` left, Erase leaves 64 KB of zeros (R7), a wrong-size file reported and ignored.
- **The VST3** (ruling R8), below.

## VST3 (ruling R8)

The VST3 was checked headlessly, in the way a DAW loads it, by `app_tests`:

1. `KS32_SETTINGS_DIR` is set to a temporary folder, and a `settings.xml` there names the ROM folder (so the user's own settings and memory file are not touched).
2. `juce::VST3PluginFormat::findAllTypesForFile` on `build-app\app\KS32_artefacts\Release\VST3\KS-32.vst3` finds one type, named "KS-32", an instrument.
3. `createInstanceFromDescription` at 48 kHz, 480-sample blocks, loads it; `prepareToPlay`.
4. 3.3 s of blocks (the boot, `kBootMs` 3200 ms), then a note-on (channel 1, note 60, velocity 100) and 1 s more.
5. The editor is created through the host (`createEditorAndMakeActive`), which makes the plugin's view and its panel editor, and is deleted; then the plugin is released.

Result: **passed**. The note's peak was 0.1601 left and 0.1780 right (both channels sound); the editor came up at 1360 x 480 (the A13 default). The whole `app_tests` run takes about 1.2 s. It skips the VST3 part with `skip: VST3 or ROMs not found` when either is missing.

What it does not check: a real DAW's own window, its MIDI routing and its audio thread. That is the "user to run" item V1 below. The user has FL Studio and Cakewalk installed; no host was downloaded.

## Manual checks: user to run

These need a person (seeing, hearing, the Q25, a DAW), so by ruling R1 they were left for the user's acceptance pass. "Checked by code" says what the implementers could check without one. Unless a row says otherwise, start `build-app\app\KS32_artefacts\Release\Standalone\KS-32.exe` with the ROM folder already chosen.

| # | From | Steps | Expected | Checked by code | Result |
|---|---|---|---|---|---|
| Q1 | Task 4 | Options → Audio/MIDI settings (or the gear → AUDIO AND MIDI): enable the Q25. Play notes | The KS Grand Piano plays, after the boot (about 2 s) | 12 s launch, exit 0, CPU in use; the engine tests (`engine_midi_offset_places_note`) | user to run |
| S1 | Task 5 (a) | Move `%APPDATA%\KS-32\settings.xml` aside (or set `KS32_SETTINGS_DIR` to an empty folder). Start the app. Open the gear, Choose folder…, pick the ROM folder | First the display reads `No ROM folder:` / `open Settings`; after choosing, all six files are marked found and the machine boots to `ROM   00` / ` KS Grand Piano` | With a settings.xml naming the ROMs the machine boots and saves on close; with none, nothing is written | user to run |
| S2 | Task 5 (b) | Rename `sq1-u26.bin` in the ROM folder, start the app, open the gear. Restore the file afterwards | The list marks `sq1-u26.bin` missing, the display reads `ROM missing:` / `sq1-u26.bin`, the app does not crash | `app_tests` (missing); the app on a dummy folder stays up and closes with exit 0 | user to run |
| S3 | Task 5 (c) | System/MIDI, set Master Tune to +02 (Up/Yes), wait 5 s, close, reopen, System/MIDI | Master Tune reads +02 | The memory is saved on close after the boot and loaded at start (a warm start) | user to run |
| S4 | Task 5 (d) | Note the time stamp of `%APPDATA%\KS-32\ks32-memory.bin`. Start the app and close it within 1 s | The time stamp is unchanged | **Passed** by automation: closed 532 ms after the window appeared, same time stamp and hash | passed (code) |
| S5 | Task 5 (e) | Gear → Erase memory… → Erase. Then System/MIDI | The machine reboots cold; Master Tune reads +00 | The file is 64 KB of zeros at once (R7) and the next start is a cold boot | user to run |
| P1 | Task 6 (a) | Look at the window at its default size | The arrangement matches the "Front panel" artboard: wordmark column with the gear, sliders, display, cursor cluster, the Sound column, the teal strips, Bank and Screen rows, the sequencer column, Tracks, transport (Record dimmed), wheels and 76 keys | A `KS32_SNAPSHOT` PNG compared with the artboard: the arrangement matches. Known differences: no amber glow, Segoe UI instead of a condensed sans, JUCE's resize corner | user to run |
| P2 | Task 6 (b) | Drag the window corner down to the minimum and up again | The aspect ratio stays 2040:720; the labels can still be read at 1020 x 360 | The constrainer fixes the ratio; at the minimum the smallest labels are about 5 px tall | user to run |
| P3 | Task 6 (c) | Click Edit Seq/Preset, right-click Bank 9, click Screen 1, right-click Bank 9 again | The display reads `3.01` (`ENSONIQ KS-32` / `OS Version 3.01`) | The chord in the engine (`engine_chord_reaches_version_screen`) | user to run |
| P4 | Task 6 (d) | Press Up/Yes, drag off the button, release outside it. Then click Down/No | Up/Yes stepped once and is not stuck; Down/No works | The release is sent on mouse-up wherever the mouse is | user to run |
| P5 | Task 6 (e) | Hold a button with the mouse, Alt-Tab away, come back | The button is not stuck | The foreground-loss release is in the editor's timer | user to run |
| K1 | Task 7 (a) | System/MIDI (Master Tune), drag Data Entry | The value changes | — | user to run |
| K2 | Task 7 (b) | Hold a Q25 note, drag the pitch wheel up, let go | The note bends up, and back when the wheel springs to centre | — | user to run |
| K3 | Task 7 (c) | On a sound that uses the mod wheel, hold a note, drag the mod wheel up | Vibrato (or the sound's modulation) comes in | — | user to run |
| K4 | Task 7 (d) | Click near the front of a key, then near the back | The front is louder | The strike mapping (A10) | user to run |
| K5 | Task 7 (e) | Drag across the keys and let go | A glissando; no note keeps sounding | Each key entered is played and the one left released | user to run |
| V1 | Task 8 | Copy `build-app\app\KS32_artefacts\Release\VST3\KS-32.vst3` (the folder) to `C:\Program Files\Common Files\VST3`. In FL Studio or Cakewalk, rescan plugins, add KS-32 to an instrument track, open its window, play from the Q25 through the track | It loads; the panel opens with the AUDIO AND MIDI section of the settings page hidden and the status line "MIDI: host"; the firmware boots to the play screen; notes from the host play | Headless VST3 check (above): loads, boots, a note sounds on both channels, the editor is made at 1360 x 480 | user to run |
| H1 | Plan 7 | Play the Standalone for an hour at the default buffer, with the gear page's underrun line in view; note the count at the end | No audible dropouts; the underrun count, recorded here | `ens_rt_tests`: no allocation in `render` after the boot | user to run |

## Controller rulings

- **R1**: the manual checks of Tasks 4–7 that need a person go to the user's acceptance pass (the table above).
- **R2**: wheels use `ks32::Analog` ids, like the sliders.
- **R3**: Task 1 edited one open-items line of `docs/verification-plan6.md`.
- **R4**: the MIDI overflow test sends `F0 7D` then zeros; the note-on that follows ends the sysex.
- **R5**: "LED within 40 units of its button" is the gap between the two rects.
- **R6**: JUCE 8.0.16-security instead of 8.0.15 (the user approved the fetch).
- **R7**: Erase writes the zero image to the memory file at once; the boot gate covers the automatic saves only.
- **R8**: the VST3 is checked headlessly in `app_tests`; the in-DAW check is V1.

## Left open

- **User acceptance**: every "user to run" row above, and the hour of play (H1).
- **Record** has no code (A14). Play and Stop/Continue are named from the MIDI bytes they send (Medium); how Continue is given was not tested.
- **Display glyphs** for 0x00–0x1F and 0x7F (A7) and the boot's custom character (`93`): drawn blank, with a hex overlay.
- **The blink rate** (A8): 2 Hz, a choice, not a measurement.
- **Several VST3 instances share one memory file**, with no lock: the last writer wins (`docs/ks32app.md`).
- **No plugin state**: the VST3 saves nothing in the host's project; its memory is the memory file (design section 4).
- **MIDI out** from the firmware is counted and not sent anywhere (design section 4).
- **Reopening the VST3 editor** draws the sliders and wheels at rest, while the machine keeps the values the last editor sent; the next drag jumps (Task 7).
- **The settings window** is about 876 px tall on a device with 8 outputs, taller than the 720 artboard; smaller screens get a scroll bar (Task 5).
- **The View menu** for the display codes is the display's right-click menu, not a menu bar (A7).

### Deferred review items

Every minor item the task reviews deferred, one line each. Items carried into a later task and fixed there are not listed (Task 1: the aligned and nothrow `new` overloads, the split panel event and the panel drop count, fixed in Task 2; Task 4: the failed ROM change dropping the running machine, suspending inside the ROM change and the compiled-in ROM path, fixed in Task 5; Task 6: the ignored `post()` result, the button state across an engine change and the blink-off cells in the hex overlay, fixed in Task 7).

**Task 1 (real-time machine)**
- `rowText` does not check `row` (out of range is undefined). Code.
- `rt_main.cpp` duplicates the `tests/main.cpp` runner loop. Test.
- `docs/verification-plan6.md` still names the `takeMidiOut` swap at "Live MIDI timing" and in Task 7's deferred items. Text.

**Task 2 (engine)**
- The overflow test cannot fail on the DUART `rxDropped` half of `midiDropped`. Test.
- `render()` before `prepare()` keeps stale MIDI in the store. Behavioural, a caller error the processor never makes.
- `test_app_engine.cpp`: the second engine is not null-checked (lines 121, 149). Test.
- `machine.h`: the `panelRoom` comment sits above `panelDropped`. Comment.

**Task 3 (transport hunt)**
- The interface doc's "Front panel: buttons", Open, still says "Guess: 09 and 29 behave like a transport button", which Task 3 settled. Text.
- The test observes but does not assert "no LED change" and "nothing on release". Test.
- The name `STOP_CONTINUE` claims Continue, though no `FB` was seen. Name.

**Task 5 (settings and memory)**
- `saveMemoryFile` failures are ignored silently (`Processor.cpp`, the close and timer saves). Behavioural: a failed save is not reported.
- `setRomFolder` stores the folder before checking it. Behavioural, minor.
- "Change…" on the memory file does not save the running RAM to the old file first. Behavioural.
- A wrong-size file at the default path is overwritten by the first autosave (unstated in A11). Behavioural.
- A failed `loadFileAsData` is ignored (harmless).
- `eraseMemory` ignores `eraseMemoryFile`'s result: a failed write means Erase silently does nothing to the file. Behavioural.
- A failed restart keeps `ramPending_`, so the next tick could save the old RAM over the zeros. Behavioural, after a failure only.
- Several VST3 instances share one memory file (above).

**Task 6 (front-panel editor)**
- Releasing Space can release a button the mouse still holds. Behavioural, minor.
- The layout tests miss the Record and gear presence and unknown Button ids. Test.
- The LED gap metric is max(dx, dy) (harmless). Test.
- `Board::paint` hard-codes the bands and the status rect outside the layout table. Code.
- MIDI device names are polled once a second. Code.
- Segoe UI and Consolas instead of a condensed sans. Look.
- Record's "not available" is only in the tooltip, not told to screen readers. Accessibility.

**Task 7 (sliders, wheels, keys)**
- `keyMove` can post a press to a new engine in a narrow restart race (released on mouse-up). Behavioural, rare.
- A quick tap is lost if the press is refused by a full queue (no stuck state). Behavioural, rare.
- Cross-part retries go in table order, not the user's order. Behavioural, rare.
- `roundToInt` rounds halves to even; A10 says round (`std::lround` would match). Behavioural, one strike step.
- The reopened VST3 editor draws the analogs at rest (above).
- A release posted in the editor's destructor into a full queue is lost. Behavioural, rare.
- The engine-pointer ABA reuse is theoretical. Code.

## User acceptance

The pass bar (design section 3): the user starts the app, plays the factory presets from the Q25 and uses the on-screen panel as they would a real KS-32: it boots to `ROM   00` / `KS Grand Piano` with the Select Sound LED lit; sounds are chosen from the panel and the Q25; the version chord shows "OS Version 3.01"; a panel setting survives a restart; no audible dropouts at the default buffer. The checks are the "user to run" rows above.

User verdict: pending.

## Reproduce

```powershell
# builds (configure lines: docs/verification-plan5.md, and docs/ks32app.md for build-app)
$env:PATH = "C:\winlibs\mingw32\bin;" + $env:PATH
cmake --build build; cmake --build build-msvc --config Release
.\build\ens_tests.exe                       # 376 passed; prints "boot trace hash" and "render hash"
.\build-msvc\Release\ens_tests.exe
.\build\ens_rt_tests.exe; .\build-msvc\Release\ens_rt_tests.exe   # 3 passed, allocations 0
ctest --test-dir build; ctest --test-dir build-msvc -C Release

$rom = "C:\Users\abart\Desktop\Ensoniq KS-32\ks-32"
.\build-msvc\Release\ks32emu.exe render tests\data\player_smoke.mid smoke_msvc.wav --roms $rom   # pcm hash a479ba867298cc55
.\build\ks32emu.exe render tests\data\player_smoke.mid smoke_mingw.wav --roms $rom
(Get-FileHash smoke_msvc.wav).Hash; (Get-FileHash smoke_mingw.wav).Hash                             # equal

# the app (MSVC x64)
cmake --build build-app --config Release --target KS32_Standalone KS32_VST3 KS32_AppTests
.\build-app\app\KS32_AppTests_artefacts\Release\KS32_AppTests.exe   # app_tests: all passed, with the VST3 lines
```
