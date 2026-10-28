# Plan 6 verification

Closing check of Plan 6 (the emulated KS-32, `ks32emu`, running the user's OS ROM v3.01): the test suite on both toolchains, the speed of the machine, cross-toolchain identity of the firmware runs, the answer to each open item of the design (section 10, A to F), the decisions with their confidences, every judgment call in one place, the controls for which no code was found, and what is left open. The user's acceptance is recorded in the last section.

The facts and how each was found are in `docs/ks32-hardware-interface.md`. The decisions are in `docs/decisions.md` (`## DUART`, D1 to D6, and `## KS-32 machine`, K1 to K16). The usage is in `docs/ks32emu.md`.

## Test counts

| Toolchain | Configure | Result |
|---|---|---|
| MinGW-w64 g++ 14.2.0, i686, Ninja, Release | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_ROM_DIR=...` | `ctest --test-dir build`: **2/2 passed** (`m68k_suite`, `ens_tests`; 30.7 s). `build\ens_tests.exe`: **355 passed, 0 failed**, 28.8 s |
| MSVC 19.44 (Visual Studio 2022), x64, Release | `cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DENS_ROM_DIR=...` | `ctest --test-dir build-msvc -C Release`: **2/2 passed** (29.3 s). `build-msvc\Release\ens_tests.exe`: **355 passed, 0 failed**, 27.1 s |

Both builds were configured with `-DENS_ROM_DIR="C:/Users/abart/Desktop/Ensoniq KS-32/ks-32"`, so every firmware test runs and none skips. Both compile with no warnings (`-Wall -Wextra`, `/W4`). The baseline before Plan 6 was 283 tests; Plan 6 added 72.

The suite takes about 28 s. The 20 s budget set for Plan 6 was passed in Task 5 (about 21 s) and has grown with each firmware test since; the tests are firmware runs, and sharing one booted machine is a deferred cleanup.

## Speed (`ks32emu bench`)

Method: `ks32emu bench` boots the machine, then plays a fixed load from MIDI in for 20 emulated seconds. The load is an 8-note chord (MIDI notes 36, 43, 48, 52, 55, 60, 64, 67, velocity 100), on at the start of every second and off 750 ms later, so notes, releases and the effects are always running. The audio is discarded. The host time is `std::chrono::steady_clock` around the chord loop. The boot is excluded from the headline figure and printed on its own line. The `peak` line shows that the load makes sound; it is one maximum over the 20 s.

| Build | Chord loop, 20 s emulated | Boot (3.2 s emulated) |
|---|---|---|
| MSVC x64 Release | host 4.05 to 4.20 s, **realtime x4.77 to x4.94** | host 0.52 to 0.55 s, x5.85 to x6.17 |
| MinGW i686 Release | host 4.40 to 4.51 s, **realtime x4.43 to x4.55** | host 0.54 to 0.57 s, x5.67 to x5.94 |

These are 6 timed runs per build, one after the other, all on the binaries built from the commit that holds this report. (The first version of this report quoted runs from two different binaries, one before and one after the `peak` line was added; those figures were dropped.) Every run printed peak 21299.

Review Focus 1 asked for MSVC at 2.0x or more: it is about 2.4 times that, so no profiling was done and the slice size stays as it is. The MinGW build is the 32-bit one, and it is a little slower.

Headroom: `ks32emu play` needs the machine to keep up with real time while the audio thread also resamples to the device rate, which costs little against these figures. Live play (Task 7) has not been run with a Q25 on the machine; that is the acceptance step below.

## Cross-toolchain determinism

The firmware runs are deterministic, and the two builds agree. The values were printed by the tests and by `ks32emu`, on each build:

| Value | MinGW | MSVC |
|---|---|---|
| Boot trace hash (`ks32_firmware_boot_is_deterministic`: FNV-1a over every bus event, `cycle`, `addr`, `value`, `size`, `write` and device, for the 3200 ms boot) | `d2afd0fed8861cbd` | `d2afd0fed8861cbd` |
| Render hash (`ks32_firmware_render_is_deterministic`: boot, then 2 s of two notes and a program change; FNV-1a of the PCM) | `2bae46b3acc34cd0` | `2bae46b3acc34cd0` |
| `ks32emu render tests/data/player_smoke.mid` (7.75 s; the PCM hash) | `a479ba867298cc55` | `a479ba867298cc55` |
| The smoke WAV file, SHA-256 | `3E661FC0C1D831AA04288492B27BE4877AFFD7F0CD9A8520EE1B1AAD3538E4CB` | `3E661FC0C1D831AA04288492B27BE4877AFFD7F0CD9A8520EE1B1AAD3538E4CB` |

The boot trace hash was not printed anywhere before this task; the test now prints it as `boot trace hash <16 hex digits>`. The render hash and the smoke hash are the values Task 7 reported.

The trace hash covers every device bus event of the boot (OTIS, ESP, DUART, unmapped and acknowledge; RAM accesses are not events), so any difference in timing, in the interleaving of the CPU with the devices or in a device's state would change it. This is the strongest identity check short of comparing registers.

## Open items of the design (section 10)

| Item | Answer | Where | Confidence |
|---|---|---|---|
| **A. CPU and DUART clocks** | The CPU and OTIS run at 15 238 090 Hz (30 476 180 / 2) and a voice slot is 16 CPU clocks. The DUART X1 is 4 MHz. The boot clock test (ACR = 0x30, X1/16, preload 2500) leaves OTIS at 32 voices, so the ratio passes the firmware's own test. MIDI (DUART channel A) runs at 31 250 baud, confirmed by MIDI in: `rxBitClocks(0)` is 128, notes and a 64-message burst are received with no overrun | Hardware doc "Clocks (K3, K16)" and "MIDI (K3)"; K3 | Medium for the CPU/OTIS and X1 values (the test constrains only their ratio); High for the MIDI clock |
| **B. The front-panel protocol** | DUART channel B, 2 rows of 16 characters, 15 LEDs, bytes 0x00 to 0x7F are characters and 0x80 and up commands (`87 pp` cursor, `8C` blank, `8D/8E/8F` LEDs, `90`/`91` field marks, `88`/`89` cursor slots and others). No start-up handshake. Buttons: a press is `code \| 0x80, 00`, a release is `code, 00`; 43 controls found. Keys ride the same link (`code \| 0x80, strike`, K12). The link's serial clock is 500 kHz | Hardware doc "Front panel: display and LEDs", "Front panel: buttons", "Keyboard"; K7, K12, K16 | High for the channel, geometry, text, positioning, LED numbers, button forms and the High-confidence codes; Medium and Low for parts (see the judgment calls) |
| **C. The interrupt map and the timing precision** | The DUART answers the acknowledge with its IVR (0x40), so it is vectored. Its level is bounded to 1 to 5; it is set to 3. OTIS is on level 1, but OS v3.01 never enables a voice interrupt, so the level cannot be observed. Timing: the machine steps slot by slot (16 CPU clocks, then OTIS, the DUART by an exact rational X1 accumulator, the ESP per frame), and the firmware's 1 ms tick comes from the DUART timer | Hardware doc "Interrupts (K2)"; K2, K4, K5 | High for the IVR answer; **Low for both levels** (the evidence cannot tell them apart); Medium for the slot scheduler |
| **D. Battery RAM** | The whole 64 KB RAM (K1), raw. A cold start is blank RAM, which the firmware initializes itself. A warm start keeps the settings (Master Tune survives) but OS v3.01 always powers on to ROM 00. About 6 short power-ons (under about 2 s each) in a row reinitialize the RAM, which is the manual's system reset sequence | Hardware doc "Battery RAM (K11)"; K11, K1 | High (the 64 KB size is Medium, K1) |
| **E. The memory card** | Not emulated in this version. The slot reads empty: DUART IP1 (card detect, active low) rests high, and the firmware never touches the card window at 0x2E0000. The empty-slot screens work ("SORRY! Install a RAM card first.") | Hardware doc "Memory card (K10)"; K10 | High that the slot reads empty; Medium that IP1 is the detect line |
| **F. How much of the 68000's bus timing matters** | Every bus cycle takes the 68000's 4 clocks (no wait states), and the boot clock test, which times OTIS against the DUART, passes with none. The core's cycle counts are exact against Tom Harte's suite (Plan 5). Read-modify-write cycles and the like are in the core (M17, M20), not in the machine | Hardware doc "Wait states (K9)"; K9, K4; `docs/m68k.md` | Medium (no evidence of wait states, and none against them) |

## Decisions

Confidence is the entry's own, after the last task that touched it. M-entries (the 68000 core) were checked in `docs/verification-plan5.md`.

### DUART (`docs/decisions.md`)

| Id | Decision | Confidence |
|---|---|---|
| D1 | Serial timing is character level, counted in 16x ticks | High for the lengths; Medium for the receiver's finishing point |
| D2 | Receive path: a queue standing for the wire, one byte per character time | High for the FIFO and overrun rules; Medium for the queue |
| D3 | Unmodelled features are counted in `unsupportedWrites()` (it is 0 after the boot). External 16x clocks on the IP pins were added in Task 3 | Medium |
| D4 | Reset state: the data sheet's listed values; others cleared | High for the listed values; Medium for the others |
| D5 | Time base: `advance(n)` counts X1 clocks, with the C/T prescaler exact | High for the periods; Medium for the exact phase of a first terminal count |
| D6 | The timer runs from the moment ACR selects a timer mode, from the preload | Medium |

### KS-32 machine

| Id | Decision | Confidence | Settled |
|---|---|---|---|
| K1 | Memory map: ROM at 0xC00000, 64 KB RAM mirrored over 0x000000 to 0x03FFFF and at 0xFF0000, OTIS, ESP and DUART windows | High for the windows and the overlay; **Medium for the RAM size** | Task 3 |
| K2 | The DUART is vectored (IVR 0x40) on level 3; OTIS on level 1, autovector | High for the IVR answer; **Low for both levels** | Task 3, examined Task 7 |
| K3 | Clocks: CPU/OTIS 15 238 090 Hz, DUART X1 4 MHz, MIDI 500 kHz external 16x clock | Medium for CPU/OTIS and X1; High for the MIDI clock | Task 3, confirmed Task 7 |
| K4 | Scheduler: per voice slot, the CPU runs 16 clocks and then the devices step | Medium | Task 3 |
| K5 | ESP cadence: 89 periods per frame; while halted the idle steps are spread over the slots | Medium | Task 3 |
| K6 | Unmapped accesses read 0, writes are ignored, every one is traced | High | Task 3 |
| K7 | Panel protocol: channel B, 2 x 16, 15 LEDs, commands and button forms | High for the core; Medium to Low for parts | Tasks 4 and 5 |
| K8 | Analog inputs through the OTIS POT converter behind a mux on DUART OP3..OP0, conversion at once | High for the path and the select table; Medium for "at once" | Task 6 |
| K9 | No wait states | Medium | Task 3 |
| K10 | Memory card: DUART IP1 is the detect, always empty, card window not modelled | High that the slot reads empty; Medium for IP1 | Task 8 |
| K11 | Battery RAM image is the 64 KB RAM; loading it is a power cycle with the battery kept | High | Task 8 |
| K12 | Local keyboard on the panel link: `code \| 0x80, strike`; note = code + 28; 76 keys | High for the path, forms and mapping; Medium for the key range; Low for what the strike value is physically | Task 8 |
| K13 | Resting analog values (`kAnalogRest`, reference 0x180, inputs 7 to 14 at 0) | High for the battery band and the readings; **Low for the reference value** | Task 6 |
| K14 | The RESET instruction resets the DUART only (the OS never executes it) | High that the OS does not use it | Task 3 |
| K15 | DUART OP6 drives the ESP halt pin, high = halt | High | Task 3 |
| K16 | The panel link's serial clock is 500 kHz | **Low** | Task 3, bound found Task 4 |

## Judgment calls

Facts settled by the lack of a better answer rather than by a document's statement or a test that tells the options apart. "Gets further" is the working criterion of Task 3 (more kinds of device access, fewer faults, no unexpected-event message on the panel link). It cannot decide between options that "get as far".

**Settled by "gets further" or by elimination (the evidence rules out the alternatives but not the whole set):**

- **K1, the RAM size is 64 KB** (Medium). 64 KB, 128 KB and 256 KB all boot to the same outcome; the firmware's size probe adapts to what it finds. 64 KB follows the service manual's "64K of RAM", and the service manual also mentions a part that could be 128K x 8 (U28/U33). The cost if wrong: a real KS-32 with more RAM has more sequencer memory than the emulation. One constant changes it.
- **K1, the RAM is one RAM mirrored, not a separate RAM at 0.** The two give the same trace hash, so the evidence cannot tell them apart; one chip pair serves both windows.
- **K2, both interrupt levels** (Low). The DUART is vectored, and that is settled (autovector gives events 138 and 139). The DUART's level is bounded to 1 to 5 by the masks the firmware runs at, and level 7 breaks the boot. Nothing picks among 1 to 5, and OS v3.01 never lets OTIS interrupt, so OTIS's level is not observable. The values used (DUART 3, OTIS 1) are what the MAME esq5505 driver is recalled to use. **The MAME values were recalled and not checked against MAME.** If a later firmware path enables OTIS interrupts, or load shows a priority problem, this needs a second look.
- **K15, ESPHALT polarity** is settled by elimination (the inverse gives event 33, "bad ESP chip").
- **K14, what RESET does** is not decided by evidence: the three options give the same trace because the OS never executes RESET. The DUART only was kept.
- **K9, no wait states** is the absence of any evidence of wait states.
- **K16, the panel link runs at 500 kHz** (Low). The firmware sets the external clock, and 0.5, 1 and 2 MHz boot identically. The firmware writes the panel's transmit register without waiting for TxRDY, which sets a lower bound of 11 000 baud (176 kHz); 153.6 kHz loses characters. 500 kHz is the MIDI clock's source (16 MHz / 32) and meets the bound. The cost if wrong: panel characters arrive at the wrong rate; both ends are emulated, so only a timeout could notice.
- **K3, the CPU and DUART clock values** are MAME's (K3 option a; not compared against MAME), confirmed only in ratio (the boot clock test constrains the ratio of the two). Medium.
- **K4 and K5, the scheduler and the ESP cadence** are design choices the firmware does not contradict (Medium).
- **D6, the free-running timer phase.** The data sheet says the timer runs continuously and gives no phase for one that has not been started. The model starts counting from the preload when ACR selects a timer mode. A firmware that starts the timer explicitly, as the sheet advises, is not affected. Medium.
- **`kBootMs` = 3200** is the last panel byte (2121 ms) plus 50 %. It is a margin, not a measured end.

**Settled by a choice the evidence leaves open:**

- **K13, the resting values.** The reference (select 15) rests at 0x180: every value from 0 to 0x180 gives the same readings, and 0x180 is the highest, so it is a pick among equals (Low). Inputs 7 to 14 rest at 0: the firmware never selects them (boot, play screen, Analog Test page), so no value can be seen. The pitch wheel at 0x200 reads 64 whatever it is (its centre is its value at power-on). The battery at 0x298 is the middle of the manual's 125 to 205 band. The volume at the maximum is chosen so headless renders are audible, and the data entry at mid.
- **K8, conversion at once.** The data sheet's 1.6 ms conversion is not modelled; the firmware reads 4 ms after each select, so the two cannot be told apart.
- **K7, the panel command parameter counts** (Low): `85` takes 1 parameter, `86` takes 1, `94` takes 1, `95` takes 1, and `93` takes 9. Each is read from the stream (what follows reads as parameter or as text), not from a document. If one were wrong, a byte would land on the display as a character. The effects of `80`, `81`, `83`, `92`, `98`, `85`, `86`, `93`, `94` and `95` are not known and the panel model ignores them.
- **K7, `8F` = blink** (Low), **`90`/`91` mark the edit field** (Medium that it marks it; Low that it blinks), **`8C` blanks the display** (Medium for the blanking), **8D is "on" and 8E "off"** (Medium: one `8D` against fourteen `8E` after a button event). The cursor wrap past position 0x1F and the rule that an out-of-range parameter is ignored and counted are choices (`// DECISION K7`).
- **K7, the Medium-confidence button names.** Left (1C) and Right (1D), Compare (2D), BankSet (39), Replace Track Sound (2A, which acts on the release) and Make Default Preset (32) are named by the function the firmware shows for the code, checked against the service manual's Figure 2 and `docs/reference/ks32-front-panel.md`. Where the screens agree with the manual the name is firm; where one screen is the only evidence it is a reading. LEDs 6 (Edit Track) and 14 (Replace Track Sound) are Medium too. The cost if a name is wrong: `--press` names do not match the button of the same name on the real panel; the byte code is right.
- **K12, the key range** (Medium) is the manual's 76 keys, E to G (MIDI notes 28 to 103); the firmware plays codes up to 127. The strike value's physical meaning (a time between contacts) is not known (Low); `Machine::key` takes it as the firmware reads it and clamps to 1 to 127.
- **K10, IP1 is the card detect** (Medium that the line is the detect itself). IP0 has no observable effect, so it is not named.
- **T3-2, MIDI at 500 kHz** is the only standard MIDI rate and was confirmed by MIDI in; this one is High.

## Controller rulings

From the ledger (`.superpowers/sdd/2026-10-28-ks32-machine/progress.md`), each with its cost if wrong:

| Ruling | What | Cost if wrong |
|---|---|---|
| P1 | Task 7 MIDI tests send on the channel the firmware receives on (found to be omni, base channel 1), not on channel 1 by the plan's assumption | None: only the channel nibble of the test bytes |
| T2-1 | `Board::route` takes an `espSlots` parameter: `Board::frame` passes 89 (player unchanged), `Machine` passes 0 while the ESP is halted, so the halted idle steps come only from K5's per-slot spread | A signature differs from the plan's text; nothing downstream depends on it |
| T3-1 | External 16x clock inputs on the DUART's IP pins, with a test; D3 amended | None |
| T3-2 | MIDI channel A is clocked at 500 kHz (16 MHz / 32): 31 250 baud | MIDI would garble; the Task 7 tests would catch it (and confirmed it) |
| T3-3 | Panel channel B is clocked at 500 kHz too: K16, Low, revisited in Tasks 4 and 5 | Panel characters arrive at a different rate; only a timeout could notice |
| T3-4 | DUART command 101 (reset break-change interrupt) is supported, not counted as unsupported | None |
| T3-5 | RAM is 64 KB (service manual p1), mirrored over 0 to 0x3FFFF and at 0xFF0000 | Less sequencer memory than a real unit with more RAM; one constant |
| T3-6 | K2: decide from the masks if possible, otherwise a provisional level (Low), settled in Task 7 | Interrupt priority inversion under load (Task 7's load did not show one) |
| T5-1 | The two-button tests press and release Edit Seq/Preset first, then hold Bank 9 and press Screen n, as the manual says; the plan's "hold Edit Seq/Preset" recipe was wrong | None; the tests follow the manual |
| T5-2 | The reinit test answers the prompt with Down/No, so the shared rig leaves no prompt open | None |
| T6-1 | Knob= (data entry) reads up to 255, not the plan's 127: the manual's rendered page lists Knob= 0 to 255 | None; the test follows the manual |
| T7-1 | The objective check (peak and RMS of the note region against silence), plus keeping the WAV for the user, replaces "the controller listens to the WAV" | A subtly wrong-sounding render passes until the user hears it (the acceptance step) |
| T7-2 | Task 7 also settles K2 using notes that make OTIS interrupt; if the levels stay indistinguishable, keep them Low | Priority inversion under load |
| T8-1 | The selected-sound test is replaced by a kept-setting test (Master Tune +02 survives); "powers on to ROM 00" is recorded as a fact; `loadRamImage` resets the CPU and every device and keeps the RAM and the analog inputs | None for behaviour; the test's name changes from the plan |
| T8-2 | Repeated short (under about 2 s) `--ram` runs trip the firmware's system-reset sequence, as on the real unit; documented, no workaround | A user loses their RAM after many short runs (documented in `docs/ks32emu.md`) |

## Controls with no code found

- **Record, Stop and Play** (the sequencer transport). No code in 0x00 to 0x7F shows anything on any screen tried (the sound screen, Edit Sound and Edit Seq/Preset pages, Select Seq/Preset, the Locate page of a new sequence). Codes 09 and 29 run about 3 500 more instructions than an unused code and resend the LEDs, which could be a transport button acting on an empty sequence. A sequencer clock the machine does not give yet (OTIS interrupts) is another possibility. These codes are not in `Button`, and `--press` has no name for them.
- **The foot switches and the sustain pedal.** DUART IP0 has no observable effect, and no input for the foot switches (service manual PDF p19) was found; the release of a note does not depend on it.
- **Per-key pressure.** There is none; pressure is one analog input (K8).

## Left open

- **User acceptance**: the machine has not yet been played with the user's Q25 over live MIDI (below).
- **K2**: both interrupt levels. A firmware path that enables OTIS interrupts would settle OTIS's. The MAME values are recalled and unchecked.
- **K16** and **K1's RAM size**: the evidence cannot decide.
- **The effects of some panel commands**: `80`, `81`, `83`, `92`, `98`, and the `85`, `86`, `93`, `94`, `95` parameters (`93` looks like a custom character definition, and the panel model does nothing with it).
- **The panel bytes `0xBC` to `0xFF`**, which act as Bank and Screen codes in runs (not separate controls), and "event 133" (unknown button event), which no byte form raised.
- **The memory card** is not emulated (design item E). The card window at 0x2E0000 reads 0, and the card's own battery warning does not exist.
- **Sysex.** `MidiIn` drops system messages (0xF0 and up), so a sysex dump cannot reach the firmware. Sysex dumps are not a goal of this plan.
- **Live MIDI timing** is quantised to one audio period (about 10 ms), and the audio callback does a few small allocations (the MIDI-out swap, the press schedule). `play` has not run long enough on a Q25 to say whether either is audible.
- **MIDI out** from the firmware is counted and not forwarded.
- **Voice Muting** (ON by default) mutes a note that arrives within about 30 ms of a program change. This is the firmware's behaviour. A sequencer or a player that sends a program change and a note together hears this.
- **The RAM image** after about 6 short power-ons reinitializes; the emulator does not protect against that.
- **Deferred review items.** Listed under "Deferred review items" below, because the ledger they came from is deleted when the plan finishes.
- **Nothing here was compared with MAME.** Every value that mentions MAME is a recollection, marked as unchecked.
- **The ROM was not studied.** By the user's ruling of 2026-10-23, the OS was run and its behaviour read from traces, never disassembled. So every "why" in the hardware document is an inference from behaviour.

### Deferred review items

Every minor item that the task reviews deferred, by task, one line each. "Behavioural" means it can change what the emulator does; the rest are comments, documents and tests. Items that were carried into a later task and fixed there are not listed (Task 2: non-copyable `Machine`, the DUART summary's even-byte filter and the K5 idle-spread test, commit b4c2cca; Task 4: the 93 glyph bytes in the panel test and the K7 marker at the out-of-range rule).

**Task 1 (DUART)**
- The reset comment and D4 say the C/T is in timer mode, but ACR = 0 is counter mode (`duart.cpp:22-31`). Text.
- CR "reset receiver" clears the overrun flag, where the data sheet (4.2.7.2) says the other registers are unaltered (`duart.cpp:341`). Behavioural, a model detail. The firmware resets the receivers during start-up with nothing pending, and no run counted a receive overrun.
- A rate change in the middle of a character can underflow `ticksLeft` and wedge the shifter (`duart.cpp:71-85`). **Behavioural, but OS v3.01 does not reach it in the runs traced.** The rate (`tick16`) changes only when CSR, ACR[7] or, for a timer clock, the C/T preload changes while a character is in flight. Traced for the boot and then 12 s with eight button presses (Select Sound, Edit Sound, System/MIDI, Edit Seq/Preset and the Bank 9 service screen): CSRA = 0xEE once at 221 ms, CSRB = 0xEE at 221 ms and again at 598 ms (the same value, so the rate does not change), ACR = 0x30 then 0x60 twice (bit 7 always 0), and no THR write before 549 ms, after every rate change. With CSR = 0xEE the rate comes from an IP-pin clock, which does not depend on ACR or the C/T. Not traced: runs under MIDI load, and screens not visited. A firmware path that writes CSR or ACR[7] with a character in flight would reach it.
- Disabling the transmitter drops a character waiting in THR, with no DECISION marker (`duart.cpp:359-362`; D4's marker is only at reset). Behavioural as the data sheet describes it (3.1: a disabled transmitter cannot be loaded); what is missing is the marker. Task 4 saw it in the panel stream, and the panel output was not affected.
- ISR[7] is not re-evaluated on an ACR write (`duart.cpp:247-253, 390-394`). Behavioural, the input-change interrupt only; the firmware enables none (ACR 0x30 and 0x60).
- `x1Hz()` is unused beyond its accessor (`duart.h`). Code.
- `duart_leaving_counter_mode_stops_it` is vacuous: the stop-command read already clears `ctRunning_` (`tests/test_duart.cpp:168-181`). Test.

**Task 2 (machine skeleton)**
- No test pins the DUART X1 rate per slot. Test.
- Wrong-width OTIS byte reads and the ESP window paths are untested. Test.
- `ks32emu boot` lists only the first 16 unmapped runs. Tool; the K1 map has none.
- The `BusEvent` value of a word write to the ESP or DUART is the whole word; it should be said at `machine.h:467`. Comment.

**Task 3 (firmware boot)**
- D2's text still says the external IP clock receives nothing (`decisions.md`, D2). Text.
- Table 4-2's footnote (IP2 is the C/T input when ACR[6:4] is 000 or 100) is not modelled or cited. The firmware's ACR values are 0x30 and 0x60, which are 011 and 110, so it does not apply.
- `setExternalClock` silently clamps rates above X1 to 0 (`duart.cpp:62`). Behavioural for a caller that passes such a rate; the machine's 500 kHz is far below X1.
- `loadKs32` reports a bad ROM set (create fails) as "skip: ROMs not found" (`tests/ks32_firmware.h:28`). Test helper.
- `docs/m68k.md:113` still describes the deleted `m68k_ks32_firmware_smoke` test. Text, still present.
- The suite was 16.4 s against a 20 s budget and could share one booted machine; it is now about 28 s. Test time.

**Task 4 (panel display)**
- `8C` clears `s_.blink` but not `blinkOn_` (`panel.cpp:487-491`). Behavioural, the blink marking only.
- The last-byte time is 1917 ms in one place and 1916 ms in others (hardware doc, `machine.h`). Text, still present.
- `bootedKs32()` returns a mutable `Machine*`; it wants a read-only comment. Test helper.

**Task 5 (buttons)**
- The evidence text of `85 pp` uses reasoning from before `88`/`89` were found (hardware doc). Text.
- The Medium-confidence names in the `Button` enum carry no `DECISION K7` marker (`panel.h:492-503`). Code comment.
- `release_ends_the_hold` asserts only an absence; it should also assert "STORE SEQUENCES". Test.
- The `89 00` slot-0 test coincides with the natural cursor, so it cannot fail (`test_ks32_panel.cpp:776-781`). Test.
- `87 20` raises `unknown` during button use (`panel.cpp:448-449`). Behavioural, the counter only; the display is unaffected, and the hardware doc says so.
- A comment at `test_ks32_firmware.cpp:124` says the overruns are counted "over the two taps", but they are counted since reset. Test comment.

**Task 6 (analog inputs)**
- A hard-coded 7 at `machine.cpp:381`, and the array sizes should be `static_assert`ed against `Analog`. Code.
- The line-A and TRAP counts in the hardware doc are from Task 3's boot, which ended on the battery warning. Text, labelled as such.
- The cold-boot "EVENT" match is broad (`test_ks32_firmware.cpp:664`). Test.

**Task 7 (MIDI, render, play)**
- `ks32emu render` has no test of its own, because the test has a loop of its own; `sendMidi` and the render loop should be shared in a header. Test.
- `pcmHash` is duplicated verbatim in the test and the tool. Code.
- `--tail 0` drops the last event (`main.cpp`). Behavioural, a tool edge case.
- Live MIDI is quantised to one audio period (about 10 ms). Behavioural, by design.
- The audio callback makes small allocations (the `takeMidiOut` swap, the press schedule). Behavioural in principle (a real-time hazard); no underrun has been measured, and `play` has not been run on a Q25.

**Task 8 (battery RAM, card, keyboard)**
- The Task 8 report blamed a gap in the key peaks on Volume smoothing; it is more likely the sound (the rig was on the ROM 01 organ, not the ROM 00 piano). Report text.
- The comment on `Machine::key` should say the value is the strike value, not a MIDI velocity (`machine.h:419-420`); `docs/ks32emu.md` says it. It matters for Plan 7. Comment.
- `loadRamImage` does not reset the CPU's cycle total or exception counts, so `cycles()` is not "since power-on" after a load. Behavioural for a program that loads an image into a running machine; `ks32emu` loads at power-on.
- `ks32emu` uses the throwing overload of `std::filesystem::exists` (`main.cpp`). Behavioural: an unreadable `--ram` path ends the tool with an uncaught exception instead of an `error:` line.
- `ram_image_keeps_settings` never checks that Master Tune is restored to +00 on the shared rig. Test.
- The warm-boot warning check samples every 100 ms and matches "LOW" alone (`test_ks32_firmware.cpp:640-644`). Test.

**Task 9 (bench and documents)**
- `bench` ignores options it does not use (`--ms`, `--trace` and so on) without an error. Behavioural, the tool.
- `peak` in `bench` is one maximum over the whole run, so it shows that sound was made, not that every chord sounded. Tool.
- Some lines in `bench` are long. Style.
- Fixed in the fix round: the fused list item in `docs/ks32emu.md`, the `--seconds 0` guard in `bench`, the K1 wording "same outcome", and the OTIS-level wording in the hardware doc and in K2.

## User acceptance

The pass bar (design section 3, Plan 6 Step 5): the user plays `ks32emu play` with the Q25 and tries the factory sounds. They select sounds by program change and by typed buttons, and open the version screen.

User verdict: pending.

## Reproduce

```powershell
# builds (see docs/verification-plan5.md for the configure lines)
$env:PATH = "C:\winlibs\mingw32\bin;" + $env:PATH
cmake --build build; cmake --build build-msvc --config Release
.\build\ens_tests.exe                       # 355 passed; prints "boot trace hash" and "render hash"
.\build-msvc\Release\ens_tests.exe
ctest --test-dir build; ctest --test-dir build-msvc -C Release

$rom = "C:\Users\abart\Desktop\Ensoniq KS-32\ks-32"
.\build-msvc\Release\ks32emu.exe bench --roms $rom
.\build\ks32emu.exe bench --roms $rom
.\build-msvc\Release\ks32emu.exe render tests\data\player_smoke.mid smoke_msvc.wav --roms $rom   # pcm hash a479ba867298cc55
.\build\ks32emu.exe render tests\data\player_smoke.mid smoke_mingw.wav --roms $rom
(Get-FileHash smoke_msvc.wav).Hash; (Get-FileHash smoke_mingw.wav).Hash                             # equal
```
