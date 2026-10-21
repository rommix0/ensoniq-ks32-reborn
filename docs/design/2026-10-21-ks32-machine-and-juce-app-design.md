# KS-32 Machine Emulation and JUCE App: Design

Date: 2026-10-21
Status: approved by the user, 2026-10-21
Follows: `docs/design/2026-10-19-otis-esp-emulator-design.md` (the chip cores and the MIDI test player, done)

## 1. Intent

Run the real KS-32 operating system (OS v3.01) on an emulated KS-32 inside a typical JUCE synthesizer app.
The user plays the KS-32's own factory sounds from a MIDI keyboard. The real front panel is on screen:
the firmware draws the display and lights the LEDs, and reads the panel buttons and sliders. The app is
standalone first, and it is built so that it can become a VST3 plugin later.

The firmware is also disassembled and annotated, the same way the project already treats the ESP
programs: a reproducible listing, with every hardware-facing routine named and explained.

**Why.** The test player proved the chips and the ROM samples. Everything that makes a KS-32 sound like
a KS-32 lives in the firmware: presets, layering, envelopes, LFOs, modulation, effect parameters, the
sequencer. Running the firmware is the only faithful way to get them, and it removes the stand-in
decisions P5–P8 and P11.

## 2. Goals

1. **An annotated firmware disassembly.** Code and data separated for the whole of OS v3.01. Every
   routine that touches hardware (vectors, interrupts, DUART, OTIS, ESP, front panel, keyboard,
   MIDI, battery RAM, memory card, self-tests) is named and commented. A document describes the
   hardware interface the firmware expects.
2. **A 68000 CPU core**, written from Motorola's M68000 manuals like the chip cores were written from
   their datasheets: every instruction and addressing mode, exceptions and interrupts, and cycle
   timing accurate enough for the firmware's timing loops.
3. **A KS-32 machine.** It joins the 68000, the existing OTIS and ESP cores, an MC68681 DUART, RAM,
   ROM, the front panel, the keyboard and its controllers, MIDI in/out and the memory card slot, as
   wired on the real board. The firmware boots to its play screen and plays its factory sounds from
   MIDI.
4. **A JUCE app** (standalone), playable from the user's MIDI keyboard. It shows the KS-32 front panel
   with the firmware-driven display, buttons, LEDs and sliders, plus the pitch and mod wheels. It is
   built on JUCE's plugin architecture, so a VST3 build is a later configuration change rather than
   a rewrite.

## 3. What "passed" means (the user's bar)

The user starts the JUCE app, plays the KS-32's factory presets from the Q25, and uses the on-screen
front panel the way they would use a real KS-32. Supporting evidence a builder must also deliver:

- The firmware boots the same way every run, and a headless run reaches the play screen with the
  expected display text.
- Rule tests for the 68000 core, and the DUART and panel behaviour the firmware relies on.
- The decisions log records every behaviour the documents do not settle (same rule as before).

## 4. Non-goals (this stage)

- **No VST3 release.** The app is plugin-ready, but shipping and validating a VST3 is the next stage.
- **No sequencer polish.** The firmware's sequencer runs because the firmware runs. Song and sequence
  workflows get no extra app features (no song import or export beyond what the firmware does over
  MIDI SysEx).
- **No other Ensoniq machines.** The SQ-1, SQ-2 and SQ-R are near relatives and may work later, but
  only the KS-32 is in scope.
- **No bundled ROMs.** The user points the app at their own ROM files.

## 5. Anti-goals: failure even if it "works"

- **Copying MAME's machine behaviour to match it.** As before, MAME is a comparison point. The
  firmware, the service manual and the chip datasheets are the references.
- **A front panel taken from a photo.** The user's ruling: the layout comes from the service manual
  (Figure 2, page 1) and from the firmware code that reads the panel. The top-view photo is
  orientation only. `docs/reference/ks32-front-panel.md` must be revised to say so.
- **JUCE inside the emulation.** The CPU, DUART, panel, machine and chip code stay plain C++17,
  JUCE-free and OS-free, building on both toolchains. Only the app layer uses JUCE.
- **A firmware listing committed to the repository.** The ROMs are Ensoniq's. The repository holds
  only our annotations (labels, comments, data types) and the tools that apply them to the user's own
  ROM. That is how the ESP program data is already handled, as small extracted facts.
- **Silent guesses**, as before: every judgment call is marked in the code and logged.

## 6. Constraints

- **Platform.** Windows 10, the user's PC. There is no real hardware, so ground truth comes from the
  firmware, the service manual and the datasheets.
- **Language and build.** C++17 and CMake, as before. The emulation libraries build with MSVC x64 and
  MinGW i686 and stay bit-identical across them. *My call:* the JUCE app builds with MSVC x64 only,
  because that is JUCE's and the future VST's toolchain.
- **Libraries.** *My call:* JUCE 8 is the project's one third-party library, at the user's request. It
  is fetched by CMake at a pinned tag, not committed. JUCE 8 is dual-licensed (AGPLv3 or a commercial
  licence), which is fine for personal use; the user should note it before distributing anything.
  Everything else is still our own code.
- **Downloads.** Each needs the user's explicit permission when the plan reaches it:
  - JUCE (CMake fetch);
  - reference documents not yet in `datasheets/`: the Motorola M68000 Family Programmer's Reference
    Manual, the M68000 user's manual (for cycle timings), and the MC68681 DUART data sheet;
  - test data for the CPU core: *my call*, the public per-instruction 68000 test suite
    ("ProcessorTests" by Tom Harte, MIT licence), used as test vectors, not code.
- **Determinism.** The machine is integer-only and deterministic. Given the same ROMs, battery RAM
  contents and input events, it produces bit-identical audio and display output. A headless runner
  makes this testable without the JUCE app.

## 7. Shape of the system

New layers sit next to the existing ones. Only the top layer touches the OS or JUCE.

1. **`m68k` library.** The CPU core and a disassembler. A bus interface supplies reads, writes and
   interrupt acknowledge, the same pattern as `WaveMemory` and `EspBus`. It runs for N cycles at a time
   and reports cycles used.
2. **`duart` library.** An MC68681 model covering what the firmware uses: two serial channels with
   baud-rate timing, the counter/timer, the input and output ports, and interrupt generation.
   Output port bit 6 is ESPHALT (findings B).
3. **`ks32` machine** (the existing board layer, extended). It holds:
   - the memory map: ROM at 0xC00000 (upper EPROM = even bytes, lower = odd), RAM (the service manual
     says 64 KB; the firmware uses 0xFF8000 and up), OTIS at 0x200000, ESP at 0x260001 on odd bytes,
     DUART at 0x280001 (findings, "What was tried");
   - interrupt wiring and the scheduler. *My call:* the CPU runs in slices of one OTIS voice slot, so
     OTIS interrupts and the firmware's stopped-voice sample reads land at the right time;
   - clocks: OTIS and the 68000 at 30.47618 MHz/2 = 15.238 MHz and the DUART at 4 MHz (MAME's KS-32
     values, to confirm from the firmware's timing code), and the ESP at 8 MHz (findings B);
   - the front panel, emulated at the protocol level the firmware sees (see §8);
   - the keyboard, wheels, pressure, pedals and sliders as input events;
   - MIDI in and out as bytes on a DUART serial channel at MIDI timing;
   - battery RAM as a saveable image, and the memory card slot (optional at first).
4. **`ks32emu` headless runner** (the existing `tools/` style). It boots the firmware, prints the
   display as text, accepts a MIDI file or live MIDI, and renders WAV. It is the regression tool and
   the place firmware bugs get debugged.
5. **JUCE app.** An `AudioProcessor` that owns one machine. MIDI from the host goes to the machine's
   MIDI input, the audio is resampled to the host rate (the existing resampler), and battery RAM is
   kept in the app or plugin state. The editor draws the front panel with the display text and LEDs
   coming from the machine, and turns mouse presses and releases into panel input events, so
   two-button combinations work. The app also has a settings page for choosing the ROM folder and the
   MIDI device.

## 8. Reverse-engineering inputs the machine needs (from Plan 4)

The firmware has to tell us the following. Each is answered with evidence in the firmware notes:

- **The interrupt map.** Which device sits on which 68000 interrupt level, and what each handler does.
- **The front-panel interface.** The service manual says the KS-32 has a separate keypad/display board.
  MAME models the SQ-1 family's panel as a device that talks to the 68000 over DUART channel B, with
  button codes in and display, LED and cursor commands out. *To confirm or refute from the firmware.*
  We need the button codes for every Figure 2 button, the LED bits, the display commands, the
  character set and geometry (it looks like 2 × 16 characters), and how the sliders and analog
  controls arrive.
- **The keyboard path.** How key velocity, release and mono pressure reach the 68000.
- **MIDI.** Which DUART channel carries MIDI, and its baud setup.
- **Battery RAM and card.** The address ranges, and how the "low battery" check works (service manual,
  page 9).
- **Timing dependencies.** The boot clock self-test (findings B), the DUART timer use, and any delay
  loops the CPU timing has to satisfy.

The panel hardware's own microcontroller (if there is one) has no dumped firmware. So the panel is
emulated from the 68000 side of its protocol, plus the service manual's keypad and display self-tests.

## 9. Order of work (proposed plans)

Each plan ends in something runnable and is planned, approved and built separately:

| Plan | Delivers | Needs |
|---|---|---|
| **4. Firmware map** | The annotation format and listing generator (using the Plan 5 disassembler, or a stand-alone first version); the annotated OS v3.01 hardware-facing code; `docs/firmware/ks32-hardware-interface.md` answering §8 | Ghidra for analysis (as in Plan 2) |
| **5. 68000 core** | The `m68k` library and disassembler, passing the per-instruction test suite with cycle counts | the Motorola manuals, the test data |
| **6. KS-32 machine** | The DUART and panel models, the machine and `ks32emu`: the firmware boots to its play screen and plays factory sounds from a MIDI file and from live MIDI | Plans 4 and 5 |
| **7. JUCE app** | Standalone app: MIDI in, audio out, the front-panel editor, ROM and MIDI settings; VST3 target configured but not released | Plan 6 (a shell using the existing test-player engine can start earlier) |

*My call on parallelism:* Plans 4 and 5 are independent and can run at the same time. The JUCE shell
(app skeleton, MIDI, audio, resampling, using the current test-player engine) can also start early,
so the app side is proven before the machine exists. The panel editor waits for Plan 4's panel map.

## 10. Open items, decided by evidence during the build and logged

- **A. CPU and DUART clocks.** Confirm 15.238 MHz and 4 MHz from the firmware's timing code
  (the boot clock test, MIDI baud setup).
- **B. The front-panel protocol** (§8), including the display geometry and character set.
- **C. The interrupt map** and the timing precision the firmware needs.
- **D. Battery RAM.** Its range and initial contents (the service manual's reinitialization path
  rebuilds it).
- **E. Memory card.** Whether the app emulates the card slot in the first version.
- **F. How much of the 68000's bus timing matters** (wait states, read-modify-write cycles).

## 11. Left to the builder

- Internal structure, class names and file layout within each layer.
- The annotation file format (*suggested:* plain text or JSON of address → label, comment, type).
- The panel's visual style (vector-drawn from Figure 2's arrangement, not photo-real), as long as
  every control and its label match the service manual and the firmware.
- How save states and battery RAM files are stored.
