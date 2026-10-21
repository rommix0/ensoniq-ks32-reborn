# MIDI Test Player Implementation Plan (Plan 3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The user plays the raw KS-32 waveforms live from a MIDI keyboard on the MSVC x64 build, through a real KS-32 effect program running on our ESP. `render` turns a MIDI file into a deterministic WAV.

**Architecture:** There are three new layers on top of the chip cores.

- **`src/ks32` (board).** It builds the two wave banks from the ROM bytes, owns one `Otis`, one `Esp` and the 64K-word delay DRAM, and runs one frame. A frame routes OTIS CH1–3 to ESP SER0–2 and ESP SER3 to the main outputs, and advances the ESP by 89 instruction slots.
- **`src/player` (firmware stand-in, pure, no OS).** It covers:
  - a JSON reader for the committed ROM-investigation data
  - the sample map and the ESP programs
  - an ESP host-interface sequencer that boots the ESP and loads effects the way the firmware does
  - the 32-voice manager (pitch, ADSR, allocation)
  - a `Synth` that takes MIDI bytes and returns stereo frames
  - an SMF parser, a WAV writer and a windowed-sinc resampler
- **`tools/ks32play` (the app, the only OS code).** Commands `play` (WinMM MIDI in, WASAPI out), `render` and `waves`. Design §7's `esp-dis` and `test` already exist as `esp_dis` and `ens_tests`, and are not duplicated.

**Tech Stack:** C++17, CMake 3.20+, `tests/check.h`. No third-party libraries. Windows APIs: WinMM (`midiIn*`) and WASAPI (`IAudioClient`, `IAudioRenderClient`, `IMMDeviceEnumerator`). The data helpers are Python 3 scripts run with `python -I`.

**Spec:** `docs/design/2026-10-19-otis-esp-emulator-design.md`. Plan 3 builds §7 layers 3–4 and goals 4–6. Pass bar, §3, in the user's words: "the program passes when we can play it using MIDI inputs" with "the sound roms working". That means raw KS-32 waveforms through real KS-32 effect programs, not factory presets. ROM facts are in `docs/rom-investigation/findings.md` ("findings"). The hand-off list is `docs/verification-plan1.md`. Judgment calls go in `docs/decisions.md`.

## Global Constraints

- C++17. No third-party libraries. ROM files (`ks-32/`, `Ensoniq-KS-32-*`) are git-ignored and never committed.
- `src/` holds no OS calls, threads or file I/O, and no mutable globals or statics. Every class can be instantiated many times. Only `tools/ks32play` touches the OS (design §5, §7).
- `src/otis`, `src/esp` and `src/ks32` are integer-only. `src/player` may use `float`/`double` only in the resampler and in building the FC table. The chip-rate `render` output must be bit-identical across the MSVC x64 and MinGW i686 builds; the resampled WAV only within one build (design §6).
- Everything builds with VS 2022 x64 (`-G "Visual Studio 17 2022" -A x64`) and with MinGW g++ 14 plus Ninja, without warnings at `/W4` and `-Wall -Wextra`. Live `play` is required on MSVC x64 and best-effort on MinGW.
- Every choice the firmware or datasheets do not settle is marked `// DECISION <id>: <one line>` at the point of use and gets a `docs/decisions.md` entry (options, choice, evidence, confidence). Player choices use ids **P1–P14** (table below). Hand-set effect parameters are logged as **provisional** (design §12).
- The frame rate is 30.47618 MHz / 2 / 512 = **29 761.9 Hz** = 30 476 180 / 1024 Hz exactly. A WAV header carries the integer rate **29762**.
- Tests that need the ROMs look for `ENS_ROM_DIR` (CMake: `${CMAKE_SOURCE_DIR}/ks-32`). When the ROMs are missing, the test prints `  skip: ROMs not found` and returns without failing. Every other test uses synthetic waves or the committed JSON under `ENS_DATA_DIR` (`${CMAKE_SOURCE_DIR}/docs/rom-investigation/data`).
- Work on branch `plan3-midi-player`. Commit after each task.

## Player Decisions (P-ids)

Each task that implements one of these writes its `docs/decisions.md` entry under a new `## Player` heading.

| Id | Topic | Choice (my call unless noted) | Task |
|---|---|---|---|
| P1 | ESP slots per frame | 89 (8 MHz ESP, findings B). A program longer than the budget still runs to END. | 1, 4 |
| P2 | Pitch | FC = 768-entry table `round(1024·2^(i/768))`, shifted by octave; P = key·256 + pitch_word + bend; P clamped to [−0x4800, 0x5400] (firmware); FC clamped to 0xFFFE. If Task 2 finds the firmware table at 0xC09F64 differs, use the firmware's values. | 2, 6 |
| P3 | Voice bus | All notes go to one bus, default **1** (CA2 → SER1, the FX1 send); CC103 selects 0/1/2. CA = bus + 1 (findings C). | 6, 7 |
| P4 | Filter | CR LP3 = LP4 = 1 (all four poles low-pass), K1 = K2 = 0xFFF0 (open). Filter storage is zeroed at note start. No HP poles, so no held DC. | 6 |
| P5 | Envelope | Linear in the volume byte (≈0.375 dB per unit, so exponential in amplitude), 8.8 fixed point, one update per frame. Defaults A 5 ms, D 0, S 100 %, R 250 ms; set with `--adsr a,d,s,r`. | 6 |
| P6 | Velocity and CC7 | Peak byte = `0xF0 − (127 − vel)·40/127 − (127 − cc7)·40/127`, floored at 0x10. | 6 |
| P7 | Note end | Release, or a one-shot whose CR reads back STOP0 = 1, fades to byte < 0x10 (release time, or 2 ms for a finished one-shot), then writes LVOL = RVOL = 0 and CR STOP0|STOP1, and frees the voice. | 6 |
| P8 | Stealing | Lowest free voice first, then the oldest voice in release, then the oldest voice. The stolen voice is cut at once (volume 0). The same key on the same channel retriggers its own voice. | 6 |
| P9 | Bend range | ±2 semitones (±512 P units). | 6 |
| P10 | MIDI map | Omni. Program change = wave number. CC0 = 0 internal / 1 rom3 expansion set. CC7, CC64 sustain, CC102 = effect index 0–10, CC103 = bus, CC120 all sound off, CC123 all notes off. | 7 |
| P11 | Effect parameters | Mix GPRs 0x76/0x77, 0x7A/0x7B, 0x7C/0x7D and output level 0x7F come from Task 2's survey (`effect_defaults.json`). Hand-set values are **provisional**. | 2, 5 |
| P12 | Record write order | The JSON has lost the order of type-4 records. Loading order: GPR 0x00–0x8F cleared; type-1 range via $E0; type-2 range via $C0; then every remaining GPR in ascending address via $A0 (SPRs 0xF4–0xF7 last). | 5 |
| P13 | Fade timing | 0x7F ramps over 20 ms (595 frames, one host GPR write per frame) unless Task 2 finds the firmware's timing. | 2, 5 |
| P14 | ESP boot | The player runs `boot_init` from `esp_init_sequence.json` at start-up and waits for the RAM clear (about 737 frames, 25 ms of silence). | 5 |

## Review Focus

1. **Velocity-0 note-on and running status from real keyboards.** Note-on with velocity 0 must act as note-off, or notes hang. Test: `synth_velocity0_is_note_off` (Task 7). Running status in files: `smf_running_status` (Task 7).
2. **More than 32 held notes, fast repeats, sustain pedal.** There must be no stuck voices and no silent notes. Tests: `voices_steal_33rd_note_and_release_all`, `voices_sustain_holds_until_pedal_up` (Task 6).
3. **Switching effect while notes sound.** The switch must fade, halt, load and resume, with no DC burst and no hang waiting for the RAM clear. Tests: `loader_switch_while_running_fades_out_first`, `loader_every_program_runs_and_decays` (Task 5).
4. **Notes at the keyboard extremes with bend.** FC must clamp and never wrap to a low pitch. Test: `voices_extreme_keys_and_bend_clamp` (Task 6).
5. **Missing ROMs, no MIDI device, odd device rates (44.1 kHz, int16 mix format).** The program must give a clear error naming the file or device, not crash. It must play at any rate. Tests: `wave_banks_reject_wrong_sizes` (Task 4) and `resampler_44100_passband` (Task 8). Task 10 adds manual checks.

---

### Task 1: ESP advance API (decision E30, ruling R17)

**Files:**
- Modify: `src/esp/esp.h` (declare next to `runSample`), `src/esp/esp.cpp`, `docs/decisions.md` (E30; add P1)
- Test: `tests/test_esp_core.cpp`

**Interfaces:**
- Produces: `int Esp::advance(int slots);` advances the ESP by one serial frame of `slots` instruction periods.
  - Running: `runSample()`. If that END halted the chip (HALT pin high), it then calls `idleStep()` for the rest of the frame, `max(0, slots − steps)` times.
  - Halted: `idleStep()` `slots` times.
  - Returns the number of instructions executed, 0 while halted.
  - A program longer than `slots` still runs to END (P1, logged as an overrun in E30).
  - `runSample()` and `idleStep()` keep their current behaviour.

- [ ] **Step 1: Write the failing tests.** Build programs the way the existing tests in this file do (`esp::Instr{...}.encode()`).
  - `esp_advance_running_matches_runSample`: two `Esp`s get the same 6-word program ending in END and the same SER0 input for 4 frames. One calls `runSample()` and the other `advance(89)`. `CHECK_EQ` the returns (6) and both SER3 outputs, frame by frame.
  - `esp_advance_halted_runs_idle_slots`: halted (pin high, `$1F` = 0x02), MEMSIZ 0xFF, DLENGTH 0x00FF00, host control `$12` = 0x03 then 0x02 (a 256-write RAM clear). After `advance(89)` twice, `hostRead(0x12) & 2` is still set. After the third call it reads 0, and `advance` returned 0 each time.
  - `esp_advance_halt_at_end_idles_rest_of_frame`: running, refresh enabled, MEMSIZ 0xFF, DLENGTH 0x00FF00, a 10-word program, DBASE recorded from `agen()`. Set the pin high, then `advance(89)`. It returns 10, `halted()` is true, and DBASE is 80 units lower (END + 79 refresh steps, wrapping per E7).
  - `esp_advance_overlong_program_runs_to_end`: a 120-word program with END at 119. `advance(89)` returns 120.
- [ ] **Step 2: Run them.** Command: `cmake --build build && build/ens_tests`. Expected: these 4 fail to compile (`advance` undeclared).
- [ ] **Step 3: Implement `Esp::advance`.** Add `// DECISION E30` at the definition. Rewrite the E30 entry: choice (b) is now implemented, with (a) kept as `runSample`. Add P1. Update `Status` with the new test names.
- [ ] **Step 4: Run the tests.** Expected: all pass, the existing tests plus these 4.
- [ ] **Step 5: Commit.** `feat(esp): advance(slots) frame API with halted idle time (E30, R17)`

### Task 2: Firmware survey of effect parameters, output fade and FC table (time-box: one session)

No C++. This produces data and decision entries that Tasks 5 and 6 consume. Follow `docs/rom-investigation/tools/*.py` style: pure Python, `python -I`, reading `ks-32/ks32/ks32v301upper.bin` (even bytes) and `ks32v301lower.bin` (odd bytes) merged at 0xC00000. Ghidra (MCP) may be used in a throw-away project outside the repo. Do not touch the user's open project.

**Files:**
- Create: `docs/rom-investigation/tools/extract_effect_params.py`, `docs/rom-investigation/effect-params.md`, `docs/rom-investigation/data/esp/effect_defaults.json`
- Modify: `docs/decisions.md` (P2 evidence, P11, P13)

**Interfaces:**
- Produces `effect_defaults.json`:
  ```json
  {"source": "...", "fade": {"gpr": "7F", "frames": 595},
   "programs": [{"index": 0, "name": null,
                 "gpr": {"7F": "xxxxxx", "7C": "xxxxxx", "7D": "...", "7A": "...", "7B": "...", "76": "...", "77": "..."},
                 "basis": {"7F": "firmware ramp target at 0xC1....", "7C": "table 0xC0A72C entry n | hand-set: <why>"}}]}
  ```
  It has one entry per program index 0–10. `fade.frames` may be replaced by the firmware's timing.
- Produces, in `effect-params.md`, the FC-table check. Either "the firmware table 0xC09F64 equals round(1024·2^(i/768)) for all 768 entries", or the list of differences. If there are differences, also commit `docs/rom-investigation/data/fc_table.json` as `{"fc": [768 ints]}`.

Steps:
- [ ] **Step 1: Dump the FC table.** Read the 768 words at 0xC09F64 and compare them with `round(1024 * 2**(i/768))`. Write the result into `effect-params.md`, and write `fc_table.json` only if they differ.
- [ ] **Step 2: Decode the level table and its indexing.** Decode the level table at 0xC0A72C and the routine that indexes it for the mix GPRs (`set_mix_gprs` in `esp_init_sequence.json`), and find the 0x7F fade-up target and step timing (around 0xC1CD3C). Record the addresses, the table contents and the index logic in `effect-params.md`.
- [ ] **Step 3: Fill `effect_defaults.json`.** Use firmware values with the default index the firmware uses at power-on, where Step 2 identified one. Values Step 2 could not settle are found empirically with the chip core, using a scratch harness that is not committed:
  1. Load the program.
  2. Feed a −12 dBFS 440 Hz sine on SER0 (dry) or SER1 (send).
  3. Choose levels so that dry passes at 0 dB ±1 dB and wet RMS sits about 6 dB below dry.
  4. Mark each such value `hand-set` in `basis`, with its measurement.
- [ ] **Step 4: Record effect names if found.** Look up the effect-ID-to-name mapping (findings 2b "Unknown"). The KS-32 manual's effect list may settle it. If it is found, fill `name`; otherwise leave `null`.
- [ ] **Step 5: Log the decisions.** Write P11 and P13 (provisional where hand-set) and the P2 evidence.
- [ ] **Step 6: Verify.** `python -I docs/rom-investigation/tools/extract_effect_params.py` regenerates `effect_defaults.json` byte-identically, and the JSON parses: `python -I -c "import json;json.load(open('docs/rom-investigation/data/esp/effect_defaults.json'))"`.
- [ ] **Step 7: Commit.** `docs(rom): effect mix levels, output fade and FC table survey`

### Task 3: JSON reader, sample map and ESP program data

**Files:**
- Create: `src/player/json.h`, `src/player/json.cpp`, `src/player/sample_map.h`, `src/player/sample_map.cpp`, `src/player/effects.h`, `src/player/effects.cpp`
- Modify: `CMakeLists.txt` (new `ens_player` static library, PUBLIC include `src`), `tests/CMakeLists.txt` (add sources and link `ens_player`; add `ENS_DATA_DIR` and `ENS_ROM_DIR` definitions)
- Test: `tests/test_player_data.cpp`

**Interfaces:**
- Produces (namespace `ens::player`):
  ```cpp
  struct JsonValue {
      enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
      bool b = false; double num = 0; std::string str;
      std::vector<JsonValue> arr;
      std::vector<std::pair<std::string, JsonValue>> obj;   // file order kept
      const JsonValue* find(const std::string& key) const;  // nullptr if absent / not an object
  };
  bool parseJson(const std::string& text, JsonValue& out, std::string& err);  // never throws

  struct Zone { int topKey; int pitchWord; int bank; uint32_t start, loopStart, end; int endFrac; uint8_t crLow; };
  struct Wave { int set; int number; std::string name; std::string note; std::vector<Zone> zones; };  // set 0 internal, 1 rom3
  struct SampleMap { std::vector<Wave> waves; const Wave* find(int set, int number) const; };
  bool parseSampleMap(const std::string& json, SampleMap& out, std::string& err);  // only waves with >= 1 zone
  const Zone& zoneForKey(const Wave& w, int key);  // first zone with key <= topKey, else the last

  struct DramTable { uint32_t wordAddr; std::vector<uint16_t> words; };
  struct EspProgram {
      int index; int effectId;                      // effectId = header byte 5
      std::string name;                             // from effect_defaults, "" if none
      std::vector<uint64_t> instr;                  // 89 words, pc 0x00..0x58
      int type1Count, type2Count;                   // from records: $E0 range [0,type1Count), $C0 range next type2Count
      std::vector<std::pair<uint8_t, uint32_t>> gpr;  // all GPR/SPR records, ascending address
      std::vector<DramTable> tables;                // index 10 only
      std::vector<std::pair<uint8_t, uint32_t>> mix;  // effect_defaults "gpr" (includes 0x7F), ascending
  };
  struct EffectLibrary { std::vector<EspProgram> programs; int fadeFrames = 595; };
  bool parseEffects(const std::string& programsJson, const std::string& defaultsJson,
                    EffectLibrary& out, std::string& err);
  ```
  The JSON holds hex strings ("0x1b", "FFFFFFFF9040", "039900") and decimal numbers. Parse each as its field needs. `crLow` comes from `cr_low_byte`. The type-1 and type-2 counts are summed over records of that type.

- [ ] **Step 1: Write the failing tests.**
  - `json_parses_nested_and_escapes`: `{"a":[1,-12.5e2,true,null],"s":"q\"\\\nA"}` gives `arr.size()` 4, `num` −1250, `str` `q"\` + newline + `A`.
  - `json_rejects_truncations_without_crash`: every prefix of that text shorter than the whole returns false with a non-empty `err`.
  - `sample_map_real_file`: read `ENS_DATA_DIR "/sample_map.json"`.
    - Internal wave 0 has 3 zones. Zone 0 is {54, −12304, 0, 318965, 334150, 349177, 0, 0x1b}.
    - rom3 wave 0 is named "VIOLA SECT", with 3 zones.
    - `find(0, 126)` and `find(0, 127)` are nullptr.
    - `zoneForKey(w0, 54)` is zone 0, 55 gives zone 1, 127 gives zone 2.
  - `effects_real_files`: read `esp/esp_programs.json` and `esp/effect_defaults.json`. There are 11 programs.
    - Program 0: `instr[0] == 0xFFFFFFFF9040`, `instr[0x58] == 0xFFFFFFFFF040`, `type1Count` 47, `type2Count` 42, and gpr address 0x00 = 0x039900.
    - Program 2's `instr` equals program 1's.
    - Program 10 has 2 tables. The first is at 0x1FBF, with 65 words starting 0x8001 and ending 0x7FFF.
    - Program 10's gpr list contains 0xF5 = 0x1F7DFF.
    - Every program's `mix` contains 0x7F.
- [ ] **Step 2: Run them.** Expected: they fail to compile.
- [ ] **Step 3: Implement** the three units. The JSON reader is a recursive-descent parser. It handles `\uXXXX` for the BMP only and limits depth to 64.
- [ ] **Step 4: Run them.** Expected: they pass.
- [ ] **Step 5: Commit.** `feat(player): JSON reader, sample map and ESP program data`

### Task 4: KS-32 board

**Files:**
- Create: `src/ks32/wave_roms.h`, `src/ks32/wave_roms.cpp`, `src/ks32/board.h`, `src/ks32/board.cpp`
- Modify: `CMakeLists.txt` (new `ens_ks32` library linking `ens_otis ens_esp`; `ens_player` links `ens_ks32`), `tests/CMakeLists.txt`
- Test: `tests/test_ks32_board.cpp`

**Interfaces:**
- Produces (namespace `ens::ks32`):
  ```cpp
  // findings A: u25/u26 = signed 8-bit samples on D15..8 (D7..0 = 0), u26 at word 0x80000;
  // rom2/rom3 = little-endian 16-bit words, rom3 at word 0x80000. Sizes: 524288 bytes (u25, u26), 1048576 (rom2, rom3).
  bool buildWaveBanks(const std::vector<uint8_t>& u25, const std::vector<uint8_t>& u26,
                      const std::vector<uint8_t>& rom2, const std::vector<uint8_t>& rom3,
                      std::vector<uint16_t>& bank0, std::vector<uint16_t>& bank1, std::string& err);
  constexpr int kEspSlotsPerFrame = 89;       // DECISION P1
  constexpr uint32_t kDramWords = 65536;      // findings D
  constexpr uint32_t kWavRate = 29762;        // 30476180 / 1024 = 29761.9 Hz, rounded for WAV headers
  class Board {
  public:
      Board(std::vector<uint16_t> bank0, std::vector<uint16_t> bank1);  // banks may be shorter than 1M words: reads past the end return 0
      Otis& otis();
      Esp& esp();
      uint16_t dram(uint32_t word) const;
      void espHostWrite(uint8_t addr, uint8_t v);  // then one Esp::idleStep() while halted (TR2 convention)
      void frame(int16_t& mainL, int16_t& mainR);  // OTIS frame; CH1..3 -> SER0..2; esp.advance(89); SER3 -> main
  };
  ```
  `Board` is non-copyable. `Otis` and `Esp` hold references to the board's memory objects, so the board owns them and never moves.

- [ ] **Step 1: Write the failing tests.**
  - `wave_banks_layout`: u25[0] = 0x12 → bank0[0] = 0x1200. u26[0] = 0x80 → bank0[0x80000] = 0x8000. rom2 bytes {0x34, 0x12} → bank1[0] = 0x1234. rom3 bytes {0xCD, 0xAB} → bank1[0x80000] = 0xABCD.
  - `wave_banks_reject_wrong_sizes`: u26 one byte short returns false, and `err` contains "u26".
  - `board_routes_ca1_to_ser0_to_main`: an ESP program whose ALU adds SER0L into SER3L and SER0R into SER3R, then END; one looped OTIS voice on CA1 with a constant wave; SERMODE 0. Main L/R track the OTIS CH1 output, delayed by the ESP pipeline. Assert a nonzero match after 8 frames.
  - `board_dram_clear_takes_737_frames`: halt the ESP via `espHostWrite(0x1F, 0x02)` with the pin high; MEMSIZ 0xFF and DLENGTH 0xFFFFFF via host GPR writes; `$12` = 0x03 then 0x02. After 736 `frame()` calls, `esp().hostRead(0x12) & 2` is set. After 737 it is clear. If the exact count comes out ±1 because of the TR2 idle steps, pin the observed value and say why in a comment.
- [ ] **Step 2: Run them.** Expected: they fail to compile.
- [ ] **Step 3: Implement.** The `WaveMemory` and `EspBus` adapters are private members, modelled on `src/trace/trace.cpp`. `Board` does not reset or initialise the chips' firmware state; that is Task 5 and Task 6.
- [ ] **Step 4: Run them.** Expected: they pass.
- [ ] **Step 5: Commit.** `feat(ks32): board with wave banks, routing and 89-slot ESP frames`

### Task 5: ESP host sequencer: boot and effect load

**Files:**
- Create: `src/player/esp_loader.h`, `src/player/esp_loader.cpp`
- Modify: `docs/decisions.md` (P11 status, P12, P13, P14)
- Test: `tests/test_player_esp_loader.cpp`

**Interfaces:**
- Consumes: `ks32::Board` (Task 4); `EspProgram` and `EffectLibrary` (Task 3).
- Produces (namespace `ens::player`):
  ```cpp
  class EspSequencer {
  public:
      void queueBoot();                                       // P14: boot_init of esp_init_sequence.json
      void queueLoad(const EspProgram& p, int fadeFrames);    // program_load sequence (below)
      void run(ks32::Board& b);   // once per frame, before Board::frame: executes queued ops until one blocks
      bool idle() const;          // queue empty
      std::vector<std::string> takeLog();  // "effect 3 loaded (id 02, 'Chorus')" etc.
  };
  ```
  The ops are private: `Write(addr, v)` through `Board::espHostWrite`, `HaltPin(level)`, `WaitAccess` (blocks while `$12 & 4`), `WaitClear` (blocks while `$12 & 2`), and `WaitFrames(n)`. A GPR write is `$00, $01, $02` (bits 23..16, 15..8, 7..0), then `$A0` = addr, then `WaitAccess`. An INSTR write is `$03..$08` (bits 47..40 first), then `$C0`. A GPR + INSTR write is `$00..$08`, then `$E0`. The ramp writes 0x7F once per frame with a `WaitFrames(1)` between, and the sequencer remembers the last level it wrote. That is the firmware's RAM copy; it never reads chip state directly.

  The program-load order is:
  1. Ramp 0x7F from the last level to 0 over `fadeFrames`.
  2. `HaltPin(1)`, then `Write($1F, 0x02)`.
  3. DLENGTH 0xF5 = 0xFFFFFF.
  4. `$12` = 0x03, `$12` = 0x02, `WaitClear`.
  5. GPR 0x00–0x8F = 0.
  6. P12 order: the type-1 range via $E0, the type-2 range via $C0, the remaining GPRs ascending via $A0.
  7. DRAM tables: `$14` = 0x7F, then for each word DOL `$0C/$0D`, then DADR `$11`, `$10`, `$0F` (the `$0F` write performs the access, E27). Use the same register order as `host_firmware_program_load_with_dram_table` in `tests/test_esp_host.cpp`.
  8. Park the read select as the firmware does: `Write($80, 0xC0)`, then `WaitAccess`.
  9. The `mix` GPRs except 0x7F.
  10. `HaltPin(0)`.
  11. Ramp 0x7F from 0 up to the program's 0x7F `mix` value over `fadeFrames`.

- [ ] **Step 1: Write the failing tests.** Load the real committed JSON. Use a `Board` with empty wave banks (all reads 0). The helper `runUntilIdle(seq, board, maxFrames)` calls `run` then `frame` and returns the frame count.
  - `loader_boot_sets_firmware_state`: `queueBoot()` goes idle within 800 frames. `esp().serialControl()` is 0x63, every `instr(i)` for i < 160 is 0xFFFFFFFFFFFF, and `gpr(0xF9) & (1u << 22)` is set.
  - `loader_program10_loads_instr_gpr_tables`: boot, then `queueLoad(p10, 32)`, idle.
    - `instr(i) == p.instr[i]` for i < 89.
    - Every `gpr` record not overridden by `mix` reads back equal. Compare SPRs by their effect, e.g. `agen()` DLENGTH, where `gpr()` reads narrow.
    - `dram(0x1FBF) == 0x8001`, `dram(0x1FFF) == 0x7FFF`, and `dram(0x1F7E)` is table B's first word.
    - `halted()` is false, and `gpr(0x7F)` is the `mix` 0x7F value.
  - `loader_switch_while_running_fades_out_first`: program 1 loaded and running. `queueLoad(p3, 32)`. Record `gpr(0x7F)` and `halted()` each frame. 0x7F does not increase until it reaches 0, and `halted()` is false in every frame before that.
  - `loader_every_program_runs_and_decays`: for each of the 11 programs:
    1. Create the board with a bank holding one period of a −6 dBFS sine (68 words ≈ 440 Hz).
    2. Boot and load the program.
    3. Play the bank as a looped OTIS voice on CA2 (SER1, the send) for 0.2 s. Write the OTIS registers directly with `otis().write`, like the `voice()` helper in `tests/test_trace.cpp`, with SERMODE 0.
    4. Set the voice's volume to 0 and render 4 s. The final 0.1 s window RMS of main L and R is ≤ 4 LSB, and no frame of the last 2 s has |x| > 64. Print each program's −60 dB decay time for the verification doc.
- [ ] **Step 2: Run them.** Expected: they fail to compile.
- [ ] **Step 3: Implement** the sequencer, with `// DECISION P12/P13/P14` at the points of use and their decisions.md entries.
- [ ] **Step 4: Run them.** Expected: they pass. If `loader_every_program_runs_and_decays` fails for a program, that is a chip-core finding. Stop, use superpowers:systematic-debugging, and report to the controller before changing `src/esp`.
- [ ] **Step 5: Commit.** `feat(player): firmware-style ESP boot and effect load sequencer`

### Task 6: Voice manager

**Files:**
- Create: `src/player/voices.h`, `src/player/voices.cpp`
- Modify: `docs/decisions.md` (P2–P9)
- Test: `tests/test_player_voices.cpp`

**Interfaces:**
- Consumes: `Otis` (`write`/`read`, page register 15, reg 13 ACT, page 0x40 reg 8 SERMODE); `Wave`, `Zone`, `zoneForKey` (Task 3).
- Produces (namespace `ens::player`):
  ```cpp
  struct Adsr { int attackMs = 5, decayMs = 0, sustainPct = 100, releaseMs = 250; };
  struct VoiceConfig { int bus = 1; Adsr adsr; int bendSemis = 2; };
  std::vector<uint16_t> makeFcTable();                         // P2: 768 entries, round(1024 * 2^(i/768))
  uint16_t fcForPitch(const std::vector<uint16_t>& table, int32_t p);  // clamp P, octave shift, clamp 0xFFFE
  class Voices {
  public:
      Voices(Otis& otis, std::vector<uint16_t> fcTable, VoiceConfig cfg);
      void init();                       // SERMODE = 0 (findings C), ACT = 31, every voice: CR = STOP0|STOP1, vol 0, K1 = K2 = 0xFFF0
      void setWave(const Wave* w);       // used by later note-ons; nullptr = notes ignored
      void setBus(int bus);              // 0..2
      void noteOn(int ch, int key, int vel);
      void noteOff(int ch, int key);
      void sustain(int ch, bool down);
      void pitchBend(int ch, int value14);  // 0..16383, centre 8192; retunes sounding voices of ch
      void channelVolume(int ch, int cc7);
      void allNotesOff();                // release all (sustain cleared)
      void allSoundOff();                // cut all at once (volume 0, stopped, freed)
      void tick();                       // once per frame before the OTIS frame
      int activeCount() const;
  };
  ```
  Note start, in the firmware order (findings "Voice start"):
  1. Page = voice.
  2. LVOL = RVOL = 0.
  3. CR = `crLow | BS(bank) | CA(bus+1)<<8 | LP3 | LP4`, where `crLow` already holds STOP0|STOP1.
  4. K1 = K2 = 0xFFF0. Filter storage regs 1–6 of page|0x20 = 0.
  5. START, END and ACC from the zone. The word address goes to `addr29 = word << 9`, H = `addr29 >> 16`, L = `addr29 & 0xFFFF`, and END L |= `endFrac << 5`.
  6. FC = `fcForPitch(P)`.
  7. CR without STOP bits.
  8. The envelope starts at level 0.

  Only `tick()` writes volumes, and only when the byte changes.

- [ ] **Step 1: Write the failing tests.** Use a synthetic `WaveMemory` and hand-built `Wave`s. Read registers back through the page register.
  - `voices_fc_table_and_clamps`: `fcForPitch` of 0 → 0x400, 1536 → 0x5A8, 3072 → 0x800, −3072 → 0x200, −0x4800 → 0x10, 0x5400 → 0xFFFE, and 0x7FFF → 0xFFFE (clamped). `makeFcTable()[767]` is 0x7FE.
  - `voices_note_on_programs_firmware_registers`: zone {topKey 127, pitchWord −15360, bank 1, start 0x100, loop 0x200, end 0x300, endFrac 5, crLow 0x0B}, bus 1, `noteOn(0, 60, 127)`, `tick()`. FC = 0x400; ACC H/L = 0x0002/0x0000; START H/L = 0x0004/0x0000; END H/L = 0x0006/0x00A0; CR = 0x0E0C.
  - `voices_attack_release_reaches_exact_silence`: a looped constant-0x4000 wave, A 5 ms, R 50 ms. After 10 ms, `otis.runFrame().ch[2]` L ≠ 0. After `noteOff` plus 60 ms of `tick` and frames, ch[2] L = R = 0 exactly and `activeCount()` is 0.
  - `voices_oneshot_end_mutes`: a one-shot (crLow 0x03) of 100 words of 0x4000, key at root, no `noteOff`. Within 100 + 60 + 4 frames (2 ms fade), ch[2] is exactly 0 and `activeCount()` is 0.
  - `voices_steal_33rd_note_and_release_all`: 33 note-ons on keys 30–62. `activeCount()` is 32, and key 30's voice now plays key 62 (its FC). After note-offs on all 33 keys and 300 ms, `activeCount()` is 0.
  - `voices_sustain_holds_until_pedal_up`: note on, pedal down, note off, 300 ms. The voice is still active. Pedal up, 300 ms: count 0.
  - `voices_extreme_keys_and_bend_clamp`: pitchWord 0, key 127, bend 16383: FC = 0xFFFE. pitchWord −15360, key 0, bend 0: FC ≥ 0x10 and ≤ 0x40.
- [ ] **Step 2: Run them.** Expected: they fail to compile.
- [ ] **Step 3: Implement** `voices.cpp`, with `// DECISION P2`–`P9` at the points of use and their decisions.md entries. `makeFcTable` uses `std::lround(1024.0 * std::exp2(i / 768.0))`.
- [ ] **Step 4: Run them.** Expected: they pass.
- [ ] **Step 5: Commit.** `feat(player): 32-voice manager with pitch, ADSR, stealing and note-end mute`

### Task 7: Synth, MIDI dispatch, SMF parser and WAV writer

**Files:**
- Create: `src/player/synth.h`, `src/player/synth.cpp`, `src/player/smf.h`, `src/player/smf.cpp`, `src/player/wav.h`, `src/player/wav.cpp`
- Modify: `docs/decisions.md` (P3 and P10 status)
- Test: `tests/test_player_synth.cpp`, `tests/test_player_smf.cpp`

**Interfaces:**
- Consumes: `Board`, `EspSequencer`, `Voices`, `SampleMap`, `EffectLibrary`.
- Produces (namespace `ens::player`):
  ```cpp
  struct SynthConfig { VoiceConfig voice; int effect = 1; int waveSet = 0; int wave = 0; };
  class Synth {
  public:
      Synth(std::vector<uint16_t> bank0, std::vector<uint16_t> bank1, SampleMap map, EffectLibrary fx,
            std::vector<uint16_t> fcTable, SynthConfig cfg);   // queues boot + effect cfg.effect, sets the wave
      void midi(uint8_t status, uint8_t d1, uint8_t d2);       // one complete short message (P10)
      void render(int16_t* interleavedLR, int frames);         // per frame: seq.run, voices.tick, board.frame
      int activeVoices() const;
      std::vector<std::string> takeLog();                      // sequencer log + "wave 0:12 'NAME'" / "wave 0:126 has no zones; ignored"
  };
  struct MidiEvent { uint64_t frame; uint8_t status, d1, d2; };
  bool parseSmf(const std::vector<uint8_t>& file, std::vector<MidiEvent>& out, std::string& err);
  std::vector<uint8_t> makeWav16(const std::vector<int16_t>& interleaved, int channels, uint32_t rate);
  ```
  `parseSmf` covers:
  - formats 0 and 1, with PPQ division only (SMPTE gives an error), running status, the FF 51 tempo meta, and skipped sysex and other meta events;
  - events merged by time, stable by track then file order;
  - integer timing: whole microseconds `us = Σ(Δticks·tempo) / ppq` (floor), then `frame = us · 30476180 / 1024000000` (floor, uint64).

  `midi()` does the following:
  - Velocity-0 note-on is note-off.
  - Program change to a missing wave is logged and ignored.
  - CC102 0–10 calls `queueLoad`; other values are ignored.
  - CC103 0–2 calls `setBus`.

- [ ] **Step 1: Write the failing tests.**
  - `smf_tempo_and_two_tracks`: in-test bytes, format 1, PPQ 480, tempo 500000 in track 0, track 1 note-on at tick 480. The event frame is 14880, and status/d1/d2 are as written.
  - `smf_running_status`: two note-ons with one status byte both decode.
  - `smf_tempo_change_midway`: tempo 250000 from tick 480. A note at tick 960 is at 750 000 µs, which is frame 22321. Whole microseconds are floored before the frame conversion, so this is not 14880 + 7440.
  - `smf_rejects_truncations_without_crash`: every proper prefix returns false with `err`.
  - `wav_header`: 2 frames of stereo at 29762 Hz give "RIFF", RIFF size 44, "WAVE", fmt PCM 1, channels 2, rate 29762, byte rate 119048, block align 4, bits 16, data size 8.
  - `synth_velocity0_is_note_off`: synthetic looped bank. `midi(0x90, 60, 100)`, render 10 ms, `midi(0x90, 60, 0)`, render 400 ms. `activeVoices()` is 0.
  - `synth_program_change_missing_wave_ignored`: `midi(0xC0, 126, 0)`. The log contains "no zones", and a following note still sounds (`activeVoices()` is 1).
  - `synth_render_deterministic`: two `Synth`s on a pseudo-random bank. The same scripted events give identical buffers.
  - `synth_rom_note_sounds_and_decays` (skips without ROMs): real banks and JSON, effect 1.
    1. Render 0.1 s.
    2. Note on 60, velocity 100, wave 0:0. Render 0.5 s. The peak |main| is > 1000.
    3. Note off. Render 4 s. The last 0.1 s RMS is ≤ 4 LSB.
- [ ] **Step 2: Run them.** Expected: they fail to compile.
- [ ] **Step 3: Implement** the units, with `// DECISION P10` in `Synth::midi`.
- [ ] **Step 4: Run them.** Expected: they pass, and the ROM test passes on this machine.
- [ ] **Step 5: Commit.** `feat(player): Synth front end, SMF parser and WAV writer`

### Task 8: Resampler

**Files:**
- Create: `src/player/resampler.h`, `src/player/resampler.cpp`
- Test: `tests/test_player_resampler.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class Resampler {
  public:
      Resampler(double inRate, double outRate, int channels);
      void process(const int16_t* in, size_t inFrames, std::vector<float>& out);  // appends; ±1.0 = full scale
      double latencyFrames() const;   // in output frames
  };
  ```
  It is a polyphase windowed sinc: 64 taps per phase, 512 phases with linear interpolation between neighbouring phases, a Kaiser window with β = 9, and cutoff 0.92 × min(inRate, outRate)/2. It handles any ratio.

- [ ] **Step 1: Write the failing tests.** The test-local helper `goertzelDb(signal, rate, freq)` is used throughout. Each test feeds 1 s at 29761.9 Hz and skips the first `latencyFrames() + 64` outputs.
  - `resampler_48000_passband`: a 1 kHz sine at amplitude 16384 gives output RMS within ±0.05 dB of 0.5/√2.
  - `resampler_48000_image_rejection`: a 10 kHz input gives a 19 761.9 Hz image ≥ 70 dB below the 10 kHz tone.
  - `resampler_44100_passband`: 1 kHz and 12 kHz tones at 44.1 kHz output, each within ±0.1 dB.
  - `resampler_frame_count`: 29762 input frames produce 48000 ± 2 output frames in total when fed in uneven chunks (1, 7, 1000, rest).
- [ ] **Step 2: Run them.** Expected: they fail to compile.
- [ ] **Step 3: Implement** the resampler.
- [ ] **Step 4: Run them.** Expected: they pass.
- [ ] **Step 5: Commit.** `feat(player): windowed-sinc resampler`

### Task 9: `ks32play render` and `waves`

**Files:**
- Create: `tools/ks32play/main.cpp`, `tools/ks32play/files.h`, `tools/ks32play/files.cpp`, `tools/ks32play/make_test_mid.py`, `tests/data/player_smoke.mid` (generated), `docs/player.md`
- Modify: `CMakeLists.txt` (`ks32play` executable; `_CRT_SECURE_NO_WARNINGS`)

**Interfaces:**
- Consumes: everything in `ens_player`.
- The CLI:
  ```
  ks32play render <in.mid> <out.wav> [--resampled <out2.wav>] [--rate <hz, default 48000>] [--tail <s, default 4>] [common]
  ks32play waves [--set internal|rom3]
  ks32play waves --audition <set>:<n> <out.wav> [common]
  ks32play play ... (Task 10)
  common: --roms <dir, default ks-32>  --data <dir, default docs/rom-investigation/data>
          --effect <0-10, default from Task 2 or 1>  --bus <0-2, default 1>  --wave <set>:<n, default 0:0>
          --adsr <a,d,s,r>
  ```
  - `render` writes the chip-rate WAV with rate 29762, 16-bit stereo.
  - The resampled WAV is 16-bit, rounded and clamped.
  - `waves` prints one line per wave: set, number, name, zone count, key ranges with root keys, the loop mode of each zone, and the `note` flag.
  - `--audition` plays each zone's rounded root key for 1 s, with 0.5 s gaps.
  - Errors exit with code 2 and one line naming the file, for example `error: cannot open ROM 'ks-32/sq1-u25.bin'`.
  - The FC table is `fc_table.json` if Task 2 committed one, otherwise `makeFcTable()`.

- [ ] **Step 1: Generate the smoke MIDI file.** `make_test_mid.py` writes a format-1 file of 6 s: a C-major arpeggio at 120 bpm, a 4-note chord, a program change to wave 5, and a pitch bend sweep. Run `python -I tools/ks32play/make_test_mid.py tests/data/player_smoke.mid`.
- [ ] **Step 2: Implement** `main.cpp` and `files.cpp`. `readFile(path, bytes, err)` uses `std::ifstream` in binary mode.
- [ ] **Step 3: Verify render determinism.** Build and run `build/ks32play render tests/data/player_smoke.mid a.wav` twice to different names, then `cmp a.wav b.wav`. Expected: identical. The tool prints the frame count, peak and clip count (samples at ±32767/−32768) to stdout.
- [ ] **Step 4: Verify `waves`.** `build/ks32play waves` lists 126 internal and 43 rom3 waves. `build/ks32play waves --audition 1:0 viola.wav` writes a WAV with 3 notes.
- [ ] **Step 5: Verify errors.** `build/ks32play render tests/data/player_smoke.mid x.wav --roms nowhere` prints one error line naming `nowhere/sq1-u25.bin` and exits 2.
- [ ] **Step 6: Write `docs/player.md`.** It covers usage, the MIDI map (P10), the defaults, and where each decision lives.
- [ ] **Step 7: Commit.** `feat(app): ks32play render and waves commands`

### Task 10: `ks32play play`: live MIDI in and WASAPI out

**Files:**
- Create: `tools/ks32play/midi_in_win.h`, `tools/ks32play/midi_in_win.cpp`, `tools/ks32play/audio_out_wasapi.h`, `tools/ks32play/audio_out_wasapi.cpp`
- Modify: `tools/ks32play/main.cpp`, `CMakeLists.txt` (link `winmm ole32` on WIN32), `docs/player.md`

**Interfaces:**
- Produces:
  ```cpp
  class MidiIn {    // WinMM; the MIM_DATA callback pushes complete short messages into a mutex-guarded queue
  public:
      static std::vector<std::string> devices();
      bool open(int index, std::string& err);
      void close();
      bool poll(uint32_t& msg);   // status | d1 << 8 | d2 << 16
  };
  class AudioOut {  // WASAPI shared mode, event-driven, default render device, mix format, ~20 ms buffer
  public:
      using Render = std::function<void(float* interleaved, uint32_t frames, uint32_t channels)>;
      bool start(Render cb, uint32_t& sampleRate, uint32_t& channels, std::string& err);
      void stop();
  };
  ```
  `AudioOut` converts to the device format: 32-bit float or 16-bit PCM, with any channel count ≥ 2. The two synth channels go to front L/R and the rest are zero. The CLI is `ks32play play [--midi <index, default 0>] [--list] [--demo] [common]`:
  - `--list` prints the MIDI inputs and the audio device rate.
  - `--demo` needs no MIDI device. It plays a built-in 4-second arpeggio through the audio device, then exits.

  The render callback:
  1. Drains `MidiIn::poll` into `Synth::midi`.
  2. Renders chip frames in blocks of 64 into the `Resampler` until it has enough output.
  3. Copies the output out.

  Log lines go to a mutex-guarded queue that the main thread prints. The main thread waits for Enter.

- [ ] **Step 1: Implement** the MIDI input and audio output. On MinGW, `__uuidof` may need the `IID_` constants from `<mmdeviceapi.h>`/`<audioclient.h>` declared with `DEFINE_GUID`. If MinGW cannot build them, gate `play` behind `#ifdef _MSC_VER` with the message "play needs the MSVC build", and note it in `docs/player.md` (best-effort, design §6).
- [ ] **Step 2: Verify `--list`.** `build-msvc/Release/ks32play play --list` prints the device lists and exits 0. With no MIDI inputs it prints "no MIDI input devices".
- [ ] **Step 3: Verify `--demo`.** `build-msvc/Release/ks32play play --demo` is audible on the default device with no glitches. Ask the user to confirm, since an agent cannot listen.
- [ ] **Step 4: Verify the error path.** `play --midi 99` exits 2 with "MIDI input 99 not found (N devices)".
- [ ] **Step 5: Commit.** `feat(app): live play with WinMM MIDI in and WASAPI out`

### Task 11: Verification and acceptance

**Files:**
- Create: `docs/verification-plan3.md`
- Modify: `docs/decisions.md` (status lines), `docs/player.md`

- [ ] **Step 1: Run both toolchains.** `cmake --build build && ctest --test-dir build --output-on-failure`, and `cmake --build build-msvc --config Release && ctest --test-dir build-msvc -C Release --output-on-failure`. Expected: all pass on both, with the ROM tests run and not skipped. Record the counts.
- [ ] **Step 2: Check cross-toolchain identity.** Render `tests/data/player_smoke.mid` with each build's `ks32play` (chip-rate WAV), then `cmp`. Expected: byte-identical. Record the result.
- [ ] **Step 3: Sweep all effects.** For `--effect 0` to `10`, render the smoke file. Record the peak, the clip count and the −60 dB tail time per effect, using `tools/mame_ref/analysis/decay_time.py` on the WAV data, or a small inline measure. Flag any effect with clips or a tail over 10 s.
- [ ] **Step 4: Write `docs/verification-plan3.md`.** It covers the counts, the identity check, the effect table, the open P decisions (the provisional ones from P11), and what the user should listen for.
- [ ] **Step 5: User acceptance (the pass bar).** The user runs `build-msvc/Release/ks32play play` with their keyboard, plays several waves (program changes) and switches effects with CC102. Record the user's verdict in `docs/verification-plan3.md`.
- [ ] **Step 6: Commit and finish.** Commit `docs: Plan 3 verification`, then use superpowers:finishing-a-development-branch.
