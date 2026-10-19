# OTIS + ESP Chip Emulator and MIDI Test Player: Design

Date: 2026-10-19
Status: approved 2026-10-19 (user: "proceed")

## 1. Intent

Build accurate software emulations of the two Ensoniq sound chips in the KS-32:

- **ES5505 "OTIS"**: 32-voice sample playback chip with per-voice filters.
- **ES5510 "ESP"**: the effects DSP.

Put them in a standalone C++ program that can be **played live from a MIDI keyboard** using the real KS-32 sample ROMs and the real KS-32 effect programs.

**Why.** MAME emulates both chips, but the KS-32 and SQ-2 are flagged "not working" there, and its chip code has known bugs: reverb tails that last about 45 s instead of about 2 s, an unclamped MAC, a guessed wave-ROM format and more (see `docs/reference/mame-otis-esp-audit.md`). The aim is to "iron out the bugs of the driver" by following the datasheets rather than copying MAME. The chips are the foundation for a later KS-32 test program (a 68000 running the real firmware) and, after that, a VST.

## 2. Goals

1. **OTIS emulator** that follows the ES5505 datasheet. It must cover:
   - the host register interface
   - the 32-voice timing and the ACT register
   - the 29-bit accumulator with linear interpolation
   - all loop modes and the IRQ/IRQV protocol
   - the two-pole LP/HP filter chain with the datasheet's coefficient scaling
   - the exponent/mantissa volume
   - the 4 output channel pairs with guard bits and saturation
   - the serial output format
2. **ESP emulator** that follows the ES5510 datasheet. It must cover:
   - the 45-bit instruction format and all 16 ALU ops, with the documented flags
   - CCR/CMR conditional skip
   - the 24×24 MAC with the SIGREG shift, guard bits and saturation on read-out
   - the 3-clock instruction timing and the documented pipeline offsets (ALU one step behind the MAC, RAM reads issued 2 steps early, the 2-entry DOL FIFO)
   - delay-line, table and I/O address generation with MEMSIZ masking and DBASE decrement at END
   - the 4 stereo serial ports
   - the full host interface: GPR and instruction reads/writes while running and halted, HALT, single-step and the read-backs
   - a disassembler
3. **Each MAME bug in the audit is either fixed or explained.** That means it is shown to be correct behaviour, or recorded as undecidable without hardware.
4. **A MIDI test player.** A stand-in for the firmware turns MIDI notes into OTIS register writes and runs a real KS-32 effect program on the ESP. You play it live from a MIDI keyboard on Windows and hear the result in real time.
5. **Correct sound ROMs.** The KS-32 wave ROMs (`sq1-u25.bin`, `sq1-u26.bin`, `rom2.u39`, `rom3.u38`) play correctly: the right sample format, start and loop points, and pitch.
6. **An offline render mode.** It takes a MIDI file and produces a WAV, the same output bit-for-bit every run, for repeatable testing and MAME comparison.

## 3. What "passed" means (the user's bar)

This stage is done when the user can **play the program from a MIDI keyboard and hear the KS-32 sound ROMs working correctly**, through real KS-32 effect programs running on the emulated ESP. Supporting evidence a builder must also deliver:

- Automated tests for the datasheet rules: every documented arithmetic, flag, address and timing rule has a test.
- The KS-32's effect programs, extracted from its own ROM, load and run, and reverb tails decay in seconds, not tens of seconds.
- A written comparison against MAME where every audible difference is either an intended fix or flagged.

## 4. Non-goals (this stage)

- **No 68000 CPU emulation.** The real firmware does not run yet; that is the next project.
- **No factory presets.** Layered presets with the firmware's envelopes, LFOs and modulation come with the KS-32 test program. The test player plays raw waveforms with a simple envelope.
- **No VST packaging, no GUI.** It is a command-line program.
- **No other chips:** not the ES5506 "OTTO" nor the ESP2. The ESP2 document is a cross-reference only.
- No performance work beyond comfortable real time on the user's PC.

## 5. Anti-goals: failure even if it "works"

- **Copying MAME's behaviour to match it.** MAME is a comparison point, not the reference. The datasheets are the reference.
- **Silent guesses.** Every behaviour the datasheet does not settle must be marked in the code at the point of use and listed in a decisions log (`docs/decisions.md`). Each entry gives the options, the choice, the evidence and its confidence. There is no real hardware, so this log is how the user knows which parts are documented fact and which are judgment.
- **Chip code that cannot go into a VST.** That means no global state, no OS calls, no threads and no file I/O inside the chip code. Each chip must be an object you can create many times.

## 6. Constraints

- **Platform:** Windows 10, the user's PC. There is no real KS-32 or other Ensoniq hardware available, so ground truth comes only from the documents and the firmware.
- **Language and build:** C++17 with CMake. It must build with Visual Studio 2022 (x64), which is the future VST toolchain, and with the installed MinGW g++ 14 (i686). *My call.*
- **No outside libraries.** For MIDI input and audio output, use Windows' own APIs (WinMM MIDI in, WASAPI or WinMM wave out). A single vendored header-only test framework is acceptable; that choice is left to the builder.
- **Deterministic chip core.** The same register writes and inputs must give bit-identical output. *My call:*
  - Chip code is integer-only, so chip-rate output is bit-identical across the MSVC x64 and MinGW i686 builds.
  - `render` always writes a native chip-rate WAV, which is the comparison artifact, and can also write a resampled one.
  - The resampled output only has to be identical within the same build.
- **Which builds deliver what.** *My call:*
  - The chip libraries, tests and `render` must work in both builds.
  - Live `play` (WASAPI/WinMM) is required on MSVC x64 and best-effort on MinGW.
  - The user plays the MSVC x64 build.
- **Licensing.** The relevant MAME files are BSD-3-Clause. Reading them for comparison is fine. Any copied code must keep its notice, but the intent is our own implementation.

## 7. Shape of the system

Four layers. Only the top layer touches the OS.

1. **`otis` library (ES5505).**
   - Host side: 16-bit register reads/writes with the page register, an IRQ output line and IRQV reads.
   - Wave memory comes through an interface: the board supplies a function that reads a 16-bit word from bank BS0 or BS1. The chip never owns ROM files.
   - Timing: it advances by master clocks or by whole sample frames. Each frame produces the 4 serial output channels at the datasheet's word width, before any host-side conversion.
2. **`esp` library (ES5510).**
   - Host side: the documented host register map.
   - Serial ports in and out on 4 stereo lanes.
   - Delay DRAM through an interface, with a size the board configures.
   - It runs one sample at a time, as an instruction-level model of the documented pipeline, inside an instruction budget derived from its clock.
   - It also includes a disassembler.
3. **`ks32` board layer.** It holds what is specific to the KS-32, not to the chips:
   - wave ROM loading and the mapping from ROM files to the chip's bank and word addresses (see open item A)
   - clocks: OTIS at 30.47618 MHz/2 per MAME, to be confirmed. The ESP clock is open item B.
   - routing of OTIS outputs to the ESP serial inputs and of ESP outputs to the main outputs (open item C)
   - the delay DRAM size
4. **`player` firmware stand-in and `app` front end.**
   - **Player:** handles MIDI note on/off, velocity, pitch bend, sustain and program change. Program change selects a waveform from the sample map. Pitch comes from the note relative to the waveform's root key. A simple ADSR rewrites OTIS volume, and voices are allocated and stolen across 32 voices. It loads a chosen KS-32 effect program into the ESP, the way the firmware would, through the ESP host interface.
   - **App commands:**
     - `play`: live MIDI in to audio out.
     - `render`: MIDI file to WAV.
     - `esp-dis`: disassemble an ESP program.
     - `waves`: list and audition the sample map.
     - `test`: run the rule tests.
   - Converting the chip's roughly 29.76 kHz output to the sound card's rate happens here, with a good-quality resampler that the chips never see.

## 8. Reverse-engineering inputs the player needs

Two things live inside the KS-32 firmware ROM and have to be found:

- **Sample map.** For each waveform: bank, start, loop start and end, loop mode and root key. These tables are in the 68000 OS ROM.
- **ESP effect programs.** The microcode and initial GPR values for each effect, also in the OS ROM.

How to get them is the builder's choice. Candidates:
- static analysis of the merged 68000 OS image; Ghidra is available to this session through an MCP server
- logging the register writes MAME's KS-32 driver makes while it boots and plays, which works if it gets that far
- pattern-scanning the ROM for 6-byte ESP instruction words ending in END

Whatever is found goes into data files that the player loads. Nothing is hard-coded in the chips.

## 9. Open items: decided by evidence during the build, and logged

- **A. Wave ROM format.** It is unclear whether `sq1-u25/u26` hold 8-bit samples, the high bytes of 16-bit samples, or interleaved 16-bit pairs. The byte order of `rom2/rom3` is also unclear: MAME loads them unswapped, whereas the SD-1 swaps. Decide from the data itself (smoothness and spectrum of each interpretation) and from the addresses the firmware programs. This item gates goal 5.
- **B. ESP clock on the KS-32.** MAME declares 10 MHz but never uses it. The datasheet's note says "Sample Rate = MCLK/512" and "Instr Rate = MCLK/3". The longest real KS-32 effect program sets a lower limit on instructions per sample. Combine the two to choose the clock.
- **C. Serial routing.** MAME's routing (OTIS ch0 to Aux, ch1–3 to SER0–2, SER3 to Main) was copied from the VFX. Confirm it for the KS-32 from the firmware's channel-assignment values and the effect programs' I/O usage.
- **D. Delay DRAM size** for the KS-32. MAME guesses 1M words. Derive it from the MEMSIZ value the firmware writes.
- **E. Datasheet contradictions.** The transcriptions flag several:
  - OTIS: the double-added FC in the loop algorithm; reverse and bidirectional loop maths; what a stopped voice outputs; rounding; K2 scaling.
  - ESP: whether DIFF saturates; C-flag polarity; ABS and the Z flag; CCR width; the DLENGTH off-by-one; whether a skipped instruction still does its RAM/FIFO access.

  Resolve each using, in order: the datasheet's own examples, then the ESP2 document where its architecture plainly carried over, then what the KS-32 firmware and its ESP programs need in order to work, then MAME's PR history where real-chip tests are cited.

## 10. Reference material (in this folder)

- `docs/reference/es5505-otis-datasheet-notes.md`: full OTIS datasheet transcription
- `docs/reference/es5510-esp-datasheet-notes-part1.md` and `-part2.md`: full ESP datasheet transcription. Scanned page esp_NN is printed page NN−4.
- `docs/reference/mame-otis-esp-audit.md`: MAME's current implementation and its known bugs, ranked
- `docs/reference/esp2-full-text.txt`: the ESP2 document. Part I covers the chip, Part II the assembler, and Part III Dattorro's effect algorithms: interpolation, chorus, LFO, reverb, filters.
- `datasheets/`: original PDFs, and the KS-32 service manual, which has the error codes for ESP self-test failures
- `ks-32/`: the complete, checksum-verified MAME KS-32 ROM set (OS v3.00 and v3.01, both wave ROM pairs, SQ-2 v2.03)

## 11. Left to the builder

- Internal code structure, class names and file layout within the four layers.
- The test framework and the resampler algorithm.
- The exact firmware stand-in envelope shape and the MIDI CC mapping.
- How the sample map and ESP programs are extracted (section 8).
- How MAME comparison output is produced. See question below.

## 12. Decisions after review

- **MAME comparison (user: "You can fetch MAME").**
  - Fetch MAME's chip sources from the official GitHub: `es5506.*`, `es5510.*` and whatever minimal device headers they need. Pin a specific commit and record its hash.
  - Build them into a separate comparison harness, with shims replacing MAME's device framework.
  - Feed that harness the same register-write and serial-input trace our player produces, and diff the chip-rate outputs.
  - The whole of MAME is not built.
- **If firmware extraction stalls.** The user said "proceed" without choosing, so this is *my call*; the user can override it.
  - Extraction from the firmware is attempted first, time-boxed.
  - Where it can't yet give an answer, the test player may use hand-set ESP parameter values and heuristically detected loop points. Each one is logged in `docs/decisions.md` as provisional, and replaced when the KS-32 test program can read the real values.
  - Waveform start addresses and the ESP microcode must come from the ROMs, not be invented.
