# Plan 8 verification

Closing check of Plan 8 (the KS-32 VST3 release: the sound and the analogs tracked and restored by the engine, host parameters, the plugin state in the project, the memory per format, keys lit by MIDI): the test suites on both toolchains, the real-time tests, the three Plan 6 hashes, the app checks and the headless VST3 check, and every manual check that needs a person, left for the user's acceptance pass (as in Plan 7, ruling R1). What is left open is listed at the end.

The usage is in `docs/ks32app.md`. The decisions are `A15` to `A23` in `docs/decisions.md` (`# KS-32 app`). The design is `docs/design/2026-11-04-ks32-vst3-release-design.md`. The facts the plan relies on are in `docs/ks32-hardware-interface.md` ("MIDI" and "Battery RAM").

All figures below are from runs on 2026-11-04 on the branch `plan8-vst3-release`, re-recorded after the final review's fixes (I1, M1 to M4; code as of `b33bd91`).

## Test counts

| Toolchain | Result |
|---|---|
| MinGW-w64 g++ 14.2.0, i686, Ninja, Release (`build`) | `build\ens_tests.exe`: **389 passed, 0 failed**. `build\ens_rt_tests.exe`: **3 passed, 0 failed** |
| MSVC 19.44, x64, Release (`build-msvc`) | `build-msvc\Release\ens_tests.exe`: **389 passed, 0 failed**. `build-msvc\Release\ens_rt_tests.exe`: **3 passed, 0 failed** |
| MSVC x64, Release, with the app (`build-app`) | `KS32_AppTests.exe`: **app_tests: all passed** (exit 0), with the VST3 lines below |

Plan 7 ended at 376 tests; Plan 8 added 13 to `ens_tests` (the engine's sound tracking and restore, the analogs after the boot, the note mask and the analog map, `analog_map_non_finite` from the final review) and the new checks in `app_tests`. `engine_restores_task1_kinds` now also restores INT 00. The builds use `-DENS_ROM_DIR` as in Plan 7, so no firmware test skips.

## Real-time tests (`ens_rt_tests`)

| Test | MinGW | MSVC |
|---|---|---|
| `rt_allocation_counter_counts` | OK | OK |
| `rt_machine_runframe_does_not_allocate` | allocations: 0 | allocations: 0 |
| `rt_engine_render_does_not_allocate` | allocations: 0, frames 163326 | allocations: 0, frames 163326 |

The engine's new work in `render` (the MIDI parser, the requests, the analogs, the note mask) allocates nothing. The processor's state save is not real-time: on the audio thread it copies 64 KB (A21), which is the host's choice in calling it there.

## Cross-toolchain determinism

The Plan 6 hashes did not change:

| Value | MinGW | MSVC |
|---|---|---|
| Boot trace hash | `d2afd0fed8861cbd` | `d2afd0fed8861cbd` |
| Render hash | `2bae46b3acc34cd0` | `2bae46b3acc34cd0` |
| `ks32emu render tests/data/player_smoke.mid` (PCM hash) | `a479ba867298cc55` | `a479ba867298cc55` |
| The smoke WAV file, SHA-256 | `3E661FC0C1D831AA04288492B27BE4877AFFD7F0CD9A8520EE1B1AAD3538E4CB` | `3E661FC0C1D831AA04288492B27BE4877AFFD7F0CD9A8520EE1B1AAD3538E4CB` |

The smoke render printed "103 MIDI events, 230654 frames (7.750 s at 29762 Hz), peak 12781, MIDI out 0 bytes" on both.

## Task 1: which selections a program change brings back

From `docs/ks32-hardware-interface.md` (a scratch harness; nothing ROM-derived is committed). After a warm boot, the bytes the firmware sent were put on MIDI in and 500 ms was run:

| Selection | What the firmware sent | What brings it back | Result |
|---|---|---|---|
| ROM 01 | `01` | `C0 01` (the data byte alone does nothing) | comes back |
| ROM 55 | `32 37` | `C0 37` (the data bytes alone do nothing) | comes back |
| INT 34 | `7C B0 00 00 20 00 C0 00 1E 22` (one capture) | `B0 00 00 20 00 C0 22`; `C0 22` alone gives ROM 34 | comes back with the bank select |
| Preset (PRESET 00 Loc=30) | nothing | nothing | **cannot come back** |

The engine's tests (`engine_restores_task1_kinds`, A15) replay these through the app's own tracking.

## The app checks (`app_tests`)

Beside the Plan 7 checks (ROM folder, memory file, the guards), `app_tests` now covers: the host `parameters`; the sound `follow` on the real ROMs (A18); `stateRoundTrip`, `stateBadBlobs`, `stateDuringBoot`, `stateWithoutRoms` and `stateThreads` (A20, A21); `projectLeavesSharedFile` (A22); `standaloneNoRam` and `standaloneStateBeforeBoot` (A23); `int00Restarts` (A15, A21, A22: INT 00 through a state, a ROM-folder change and Change..., then Erase to ROM 00 with Sound 0). The output of the last run (the `standaloneStateBeforeBoot` display after System/MIDI, then the VST3 part):

```
  standalone display: [Master Tune= +00Global Bend= 02 ]
  VST3 note peak: 0.1601 0.1780
  VST3 editor: 1360 x 480
  VST3 restored sound: 55
app_tests: all passed
```

The VST3 check runs in a temporary `KS32_SETTINGS_DIR`: a first instance is put on ROM 55 by MIDI, its state is taken, and a second instance given that state reports `sound` 55 through the host's parameters; the memory file is unchanged across both.

What it does not check: a real DAW's project save and reload, its MIDI routing and its audio thread. Those are D1 to D8 below.

## Launch smokes (in place of the controller's manual checks)

The Task 3 and Task 4 steps "check by hand in the Standalone" could not be done by the controller (no person, no Q25, no ears). The implementers ran launch smokes instead: the Standalone started with a temporary `KS32_SETTINGS_DIR`, wrote a `KS32_SNAPSHOT` PNG, and closed cleanly on `WM_CLOSE`. The checks themselves became the user rows S6 and S7 below.

One of those smokes let JUCE's wrapper write a default `filterState` (default parameters, no RAM) into the user's real `%APPDATA%\KS-32\KS-32.settings`, because the wrapper's file is not moved by `KS32_SETTINGS_DIR` (A23). A backup of the earlier file was kept in the session scratchpad.

## Manual checks: user to run

These need a person, a DAW, the Q25 or ears. D1 to D8 are in FL Studio or Cakewalk, with the built `KS-32.vst3` copied to `C:\Program Files\Common Files\VST3` as in Plan 7's V1. Presets cannot be restored (A15), so D1 to D7 use ROM sounds.

| # | Steps | Expected | Checked by code | Result |
|---|---|---|---|---|
| D1 | One KS-32 on ROM 55, Master Tune +02 (System/MIDI), Volume lowered. Save the project, close it, reopen | It comes back on ROM 55, at +02, with Volume as left | `stateRoundTrip`, `vst3` | user to run |
| D2 | Two KS-32s on different sounds and Master Tune values. Note the time stamp of `%APPDATA%\KS-32\ks32-memory.bin`. Save and reopen | Each comes back as it was; the time stamp has not changed | `projectLeavesSharedFile`, `vst3` (the file is unchanged) | user to run |
| D3 | Add a new KS-32 to the project | It starts with the shared file's Master Tune | the constructor starts from the file (A22) | user to run |
| D4 | On an instance set a Master Tune, then the gear, **Copy to shared memory…**, confirm. Add another KS-32 | The new one has the copied Master Tune | `projectLeavesSharedFile` (the file equals the RAM after the copy) | user to run |
| D5 | Draw a Volume automation lane and play | The slider and the level follow it | `parameters` | user to run |
| D6 | Draw a Sound lane: the display changes sound. Then turn the lane off and pick a sound on the panel | The host's Sound value follows the panel pick | `follow` | user to run |
| D7 | Play notes from the track | The keys on the panel light while held | `engine_note_mask` | user to run |
| D8 | Put INT 34 on an instance (BankSet, Bank 3, Screen 4). Save and reopen. Repeat with INT 00 (BankSet, Bank 0, Screen 0) | It comes back on INT 34, and on INT 00 | `engine_restores_task1_kinds` (INT 34, INT 00), `int00Restarts` | user to run |
| S6 | In the Standalone, drag Volume, close and reopen the editor. Play a Q25 note. Press Up/Yes on the panel | Volume is drawn where it was left; the Q25 note lights its key; the panel pick shows the new sound | `parameters`, `engine_note_mask`, a launch smoke | user to run |
| S7 | In the Standalone, pick ROM 55 and lower Volume. Close, reopen | After the boot it is on ROM 55, Volume is where it was, and Master Tune keeps its value from the memory file | `standaloneNoRam`, `standaloneStateBeforeBoot`, a launch smoke | user to run |

## Controller rulings

- **P8-1**: the manual steps of Tasks 3 and 4 became the user rows S6 and S7 (no person, Q25 or ears for the controller).
- **P8-2**: presets are not restored; `docs/ks32app.md` says so, D1 to D7 use ROM sounds, and D8 covers INT.

## Left open

- **User acceptance**: every "user to run" row above (D1 to D8, S6, S7).
- **Presets** cannot come back (no MIDI out when selected). Other MIDI channels and the Select Sound pages after a bank select alone were not tried (A15).
- **A host that stops calling `processBlock`** (for example FL Studio's Smart Disable) makes each such instance take up to 300 ms on a project save (A21).
- **Thread limits** (A21): a host that runs one instance's `processBlock` on several threads is seen as its last one; a save on a previously recorded audio worker thread while `processBlock` has migrated to another worker can read the RAM during a block (rare).
- **A panel pick and a host change of Sound inside one 30 Hz tick** can leave the host showing the followed value while the machine plays the host's, until the next change of either (A18; argued, not run). The one-follow-at-a-time rule (a tick starts no follow while a marker waits for a block) is also argued, not run.
- **A saved selection loaded into a running Standalone** ("Load a saved state...") whose program number the machine is already on (INT 00 against ROM 00, say) is not sent: only the boot's end sends a selection whose number matches (A15, A23).
- **Prog Change= OFF** stops the restore and the Sound parameter (documented).
- **CPU**: each instance is a whole KS-32.
- **MIDI out** is still counted and not sent anywhere.
- The Plan 7 open items not named above stand.

## User acceptance

The pass bar (design section 3): a saved project brings each KS-32 back with its sound, Master Tune, Volume and memory; two instances do not disturb each other or the shared file; a new instance starts from the shared file; Volume and Sound can be automated and follow the panel; keys light from MIDI. The checks are the "user to run" rows above.

User verdict: pending.

## Reproduce

```powershell
$env:PATH = "C:\winlibs\mingw32\bin;" + $env:PATH
cmake --build build; cmake --build build-msvc --config Release
.\build\ens_tests.exe; .\build-msvc\Release\ens_tests.exe           # 389 passed; boot trace and render hashes
.\build\ens_rt_tests.exe; .\build-msvc\Release\ens_rt_tests.exe     # 3 passed, allocations 0
$rom = "C:\Users\abart\Desktop\Ensoniq KS-32\ks-32"
.\build\ks32emu.exe render tests\data\player_smoke.mid smoke_mingw.wav --roms $rom        # pcm hash a479ba867298cc55
.\build-msvc\Release\ks32emu.exe render tests\data\player_smoke.mid smoke_msvc.wav --roms $rom
cmake --build build-app --config Release --target KS32_Standalone KS32_VST3 KS32_AppTests
.\build-app\app\KS32_AppTests_artefacts\Release\KS32_AppTests.exe   # app_tests: all passed, with the VST3 lines
```
