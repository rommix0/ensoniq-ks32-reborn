# ks32emu: the emulated KS-32

`ks32emu` runs the user's KS-32 OS ROM (v3.01) on the emulated machine: the 68000 core, the MC68681 DUART, the OTIS wave chip, the ESP effects DSP and the delay DRAM, wired as the board is (`src/ks32/machine.*`). The firmware makes the sound, so its own sound selection, MIDI handling, effects and panel screens are all there. Nothing is a stand-in, unlike `ks32play` (`docs/player.md`), which has a small voice control of its own.

The wiring facts, each with the evidence it was found by, are in `docs/ks32-hardware-interface.md`. The decisions (D1 to D7, K1 to K16) are in `docs/decisions.md`. The verification report is `docs/verification-plan6.md`.

This page covers `boot`, `render`, `play` and `bench`.

## What you need

- The ROM images in one directory. `--roms <dir>` names it, and the default is `ks-32`, relative to the current directory, so run from the main checkout or pass `--roms`. The directory holds `sq1-u25.bin`, `sq1-u26.bin`, `rom2.u39` and `rom3.u38`, and under `ks32/` the OS images `ks32v301upper.bin` and `ks32v301lower.bin`. The repository's `ks-32` folder holds them.
- Build: `cmake --build build` (MinGW) or `cmake --build build-msvc --config Release`. The executable is `ks32emu` (`ks32emu.exe` on Windows). `play` needs Windows (WinMM and WASAPI); the other commands run anywhere.

## Commands

```
ks32emu boot   [--ms <n>] [--trace <devs>] [--from <cycle>] [--count <n>] [--last <n>] [--display-only]
               [--press <ms>:<name>[+<name>...]]... [--analog <name>=<raw>]... [--ram <file>] [--roms <dir>]
ks32emu render <in.mid> <out.wav> [--tail <s>] [--press ...]... [--analog ...]... [--ram <file>] [--roms <dir>]
ks32emu play   [--midi <index>] [--list] [--analog ...]... [--ram <file>] [--roms <dir>]
ks32emu bench  [--seconds <n>] [--roms <dir>]
```

Every option takes a value except `--display-only` and `--list`. A wrong option, value or argument count prints `error: <msg>` to stderr and exits 2. A successful run exits 0.

All four commands power the machine on and run it in frames of one OTIS voice cycle, 29 762 per second of emulated time (the true rate is 30476180 / 1024 = 29761.9 Hz). The boot takes `kBootMs` = 3200 ms of emulated time, which is the last panel byte of the start-up (at 2121 ms) plus 50 %. After it the display shows the play screen: row 0 `ROM   00        `, row 1 ` KS Grand Piano `, with LED 8 (Select Sound) lit.

### boot

Runs `--ms` (default 3000) of emulated time from power-on and prints what happened. It is the tool for looking at the firmware: the panel display, the device accesses, the interrupts.

- The panel display is printed each time it changes, as `<ms> |<row>|` (one line per row), with each byte outside 0x20 to 0x7E as `\xNN`. The LEDs are printed the same way, as `<ms> LEDs 8` (the lit LED numbers, `*` after a blinking one, `LEDs off` when none is lit).

  ```
  861 | E              |
  861 |                |
  ...
  2116 |ROM   00        |
  2116 | KS Grand Piano |
  ```

- `--display-only` stops there. Without it a summary follows:

  ```
  ran 20833 frames (700 ms), 10666538 CPU cycles
  exceptions: v10=162 v34=89 v35=3 ... v64=163
  unmapped addresses (0 distinct): none
  DUART last write per register: r0=07 r1=ee ...
  OTIS active voices: 32
  ESP halted: yes
  DUART A transmitted: 42 bytes
      hex: b2 00 00 20 ...
      txt: ... .H@G.B.@...
  panel: LEDs off, unknown bytes 0
  DUART unsupported writes: 0
  ```

  `exceptions` counts the vectors taken (line A, v10, and the TRAPs are the firmware's calls, not faults; see the hardware doc). `unmapped addresses` are the distinct addresses outside the K1 map, merged into runs, first 16 runs. `DUART last write per register` is the last value the DUART actually received in each register; ignored even-byte writes are left out. `DUART A transmitted` is channel A's output, the MIDI out, with the first 64 bytes. Channel B's bytes go to the panel and are not listed. `unknown bytes` is how many panel commands the panel model does not know. `unsupported writes` counts writes to DUART features that are not modelled (D3).
- `--last <n>` also prints the last n instructions executed, disassembled.

#### The trace

`--trace <devs>` prints each bus access to the listed devices as it happens. `<devs>` is a comma list of `otis`, `esp`, `duart`, `unmapped` and `iack` (an interrupt acknowledge). Each line is

```
<cycle> <DEVICE> <R|W><size> <address> <value>
```

```
3134540 DUART W1 28001f 40
3197068 DUART W1 28000d 09
```

- `cycle` is the 68000 clock count since power-on (15 238 090 clocks per second).
- `DEVICE` is `OTIS`, `ESP`, `DUART`, `UNMAPPED` or `IACK`.
- `R` is a read and `W` a write. `size` is 1, 2 or 4 bytes, the width of the 68000 access.
- `address` is the 24-bit address in hex, `value` is the data in hex, `size * 2` digits. For `IACK` the address is the interrupt level (3 for the DUART) and the value is the vector the device answered with (0x40).
- `--from <cycle>` skips lines before that cycle, and `--count <n>` stops after n lines.

The trace goes to stdout ahead of the summary. It is a text listing for reading, not the `docs/trace-format.md` chip-test script; `ens_trace` reads that one.

#### Buttons: --press

`--press <ms>:<name>[+<name>...]` presses the named buttons in order at `<ms>`, holds them for 100 ms and releases them in reverse order. The option may be repeated. In `boot`, `<ms>` counts from power-on; in `render` it counts from the end of the boot, which is the start of the WAV.

The names, case-insensitive:

| Group | Names |
|---|---|
| Banks | `BANK0` to `BANK9` |
| Screens | `SCREEN0` to `SCREEN9` |
| Tracks | `TRACK1` to `TRACK8` |
| Value and cursor | `UP_YES`, `DOWN_NO`, `LEFT`, `RIGHT`, `COMPARE`, `ENTER_SAVE` |
| Modes | `EDIT_SOUND`, `BANK_SET`, `SELECT_SOUND`, `EDIT_SEQ_PRESET`, `EDIT_TRACK`, `SELECT_SEQ_PRESET`, `REPLACE_TRACK_SOUND`, `MAKE_DEFAULT_PRESET`, `SYSTEM_MIDI` |

Record, Stop and Play are not here: no code for them was found (`docs/ks32-hardware-interface.md`, "Front panel: buttons").

Two notes on how combinations work, because the firmware follows the real panel's rules:

- Pressing two names joined by `+` presses them together. A mode button and a Bank or Screen button pressed together are not the same as the manual's "press the mode, then hold Bank 9 and press Screen 1". The manual's order is two presses:

  ```
  ks32emu boot --ms 4300 --press 3500:EDIT_SEQ_PRESET --press 3900:BANK9+SCREEN1 --display-only
  ```

  shows the version screen, `ENSONIQ KS-32` over `OS Version 3.01`. Holding Edit Seq/Preset through the combination gives the Locate page instead.
- A press is held for 100 ms. Down/No repeats while it is held, so a long hold steps the sound more than once.

Examples:

```
ks32emu boot --ms 3300 --press 3000:UP_YES --display-only     (ROM 00 to ROM 01)
ks32emu boot --ms 3600 --press 3300:SYSTEM_MIDI --display-only (the System/MIDI page)
```

#### Analog inputs: --analog

`--analog <name>=<raw>` sets an analog input from power-on. `<raw>` is the converter's raw 10-bit reading, 0 to 1023, in decimal. The option may be repeated. The names are `PITCH_WHEEL`, `MOD_WHEEL`, `CV_PEDAL`, `VOLUME`, `DATA_ENTRY`, `PRESSURE` and `BATTERY`.

Unset inputs rest at the K13 values: pitch wheel 512 (centred), mod wheel 1023 (down), CV pedal 0 (unplugged), volume 1023 (maximum, so renders are audible), data entry 366 (middle), pressure 0 and battery 664 (nominal). The pitch wheel's centre is whatever it reads at power-on, so setting it here sets the centre.

```
ks32emu boot --ms 3300 --analog BATTERY=480       (the low-battery warning appears)
```

A battery raw reading below about 500 (0x1F8) gives the warning: 480 does, 512 does not. The Analog Tests page (Edit Seq/Preset, then Bank 9 and Screen 0, as in the version-screen example) shows the readings the firmware sees: `PW=064 Mod=000` and so on.

#### Battery RAM: --ram

`--ram <file>` is the battery RAM, 64 KB, raw. If the file exists it is loaded at power-on, which is a warm start with the settings kept. Otherwise the RAM starts blank, a cold start. It is written back on a clean exit: `boot` and `render` at the end, `play` when you type `q` (not on Ctrl+C). A file of the wrong size gives `error: RAM image: expected 65536 bytes, got n` and exit 2.

Three things to know about it:

- OS v3.01 powers on to ROM 00 from a cold or a warm start, whatever sound was selected when the image was saved. What the battery keeps is the settings (for example Master Tune), not the selected sound. This is the firmware's behaviour.
- About 6 short power-ons in a row (each under about 2 s) reinitialize the RAM, as on the real unit (the service manual's system reset sequence, PDF p5). So take care with repeated short `--ram` runs: many `boot --ms 1000 --ram x.ram` runs in a row will wipe the file's settings. A run of 2 s or more clears the count. There is no workaround in the emulator, on purpose.
- The `--roms` directory is not part of the image, and neither are the `--analog` values: those are the player's controls and are set again each run.

### render

Reads a Standard MIDI File (format 0 or 1, PPQ timing), boots the machine, plays the file into MIDI in and writes a WAV.

```
ks32emu render tests/data/player_smoke.mid smoke.wav --roms "C:/path/to/ks-32"
103 MIDI events, 230654 frames (7.750 s at 29762 Hz), peak 12781, MIDI out 0 bytes
pcm hash a479ba867298cc55
```

- The boot (3200 ms) is run first and not written, so WAV time equals MIDI time.
- Each file event goes to MIDI in at its frame, and the machine runs until the last event plus `--tail` (default 2 s, 0 to 3600), so releases and effects die away.
- The WAV is 16-bit stereo at 29762 Hz, the chip output without resampling. It is deterministic: the same file, ROMs and options give the same WAV, and the MinGW and MSVC builds agree (`docs/verification-plan6.md`).
- The summary line gives the event count, the frame count, the peak sample, the bytes the firmware sent on MIDI out, and an FNV-1a 64 hash of the PCM (each sample as two little-endian bytes).
- `--press`, `--analog` and `--ram` work as in `boot`.
- Only channel messages are sent. The firmware receives on every channel (omni, base channel 01).

### play

`play` runs the machine live: MIDI from a WinMM input device, audio to the default Windows output device through WASAPI in shared mode. Windows only.

```
ks32emu play --list
ks32emu play --midi 1
```

- `--list` prints the MIDI inputs (`index: name`, or `no MIDI input devices`) and the default audio device, then exits. It needs no ROMs.
- `--midi <index>` picks the input. An index past the end fails with `error: MIDI input 99 not found (2 devices)`. With no device, `play` runs with typed buttons only.
- It boots first (`booting (3200 ms emulated)...`; the boot runs faster than real time, a second or less), then prints the audio format, the MIDI device and the display, and reprints the display and LEDs whenever they change.
- Type button names joined by `+` and Enter to press them (for example `bank9+screen1`), as `--press` does. `q` and Enter quits and writes `--ram`. An unknown name prints a message and does nothing.
- On quitting it prints one line: audio frames and seconds, underruns (should be 0), peak, clipped samples, the slowest refill, MIDI messages received and dropped, and MIDI out bytes.

How it works and what to expect:

- The machine runs on the audio render thread, in blocks of 64 frames through the resampler (29 762 Hz to the device rate) until the device's request is covered. MIDI is sent to the machine at the start of each refill, so live MIDI is quantised to one audio period (about 10 ms), plus the device buffer and about 1 ms of resampler delay.
- `MidiIn` (`tools/ks32play/midi_in_win.h`) takes channel messages only. It drops system messages, status 0xF0 and up: sysex, clock and active sensing. A sysex dump cannot reach the firmware.
- The firmware's MIDI out is counted and not forwarded anywhere.
- Voice Muting is ON by default (System/MIDI Screen 3). With it a note that arrives within about 30 ms of a program change stays silent. This is the firmware's behaviour, not a lost message. Set Voice Muting to OFF on that screen to hear such a note.
- Selecting sounds: send a program change (`C0 n` selects ROM n), or press the buttons: `up_yes` / `down_no` step through the sounds, and in Select Sound mode Bank n and Screen n change the sound number's digits (from ROM 55, Bank 1 gives ROM 15 and Screen 1 gives ROM 51).
- The version screen: type `edit_seq_preset`, wait a moment, then `bank9+screen1`.

### bench

```
ks32emu bench [--seconds <n>] [--roms <dir>]
```

Boots, then plays a fixed load from MIDI in for `n` emulated seconds (default 20, 0 to 3600) with the audio thrown away, and prints how fast it ran:

```
boot: emulated 3.200 s, host 0.521 s, realtime x6.14
emulated 20.000 s, host 4.090 s, realtime x4.89
peak 21299 (a silent run would be 0)
```

- The load is an 8-note chord, C2 G2 C3 E3 G3 C4 E4 G4 (MIDI notes 36, 43, 48, 52, 55, 60, 64, 67) at velocity 100. It goes on at the start of every emulated second and off 750 ms later, so up to 8 notes plus their releases are always in play.
- The boot is excluded from the headline figure and printed on the first line. The second line is the chord loop only.
- The host time is `std::chrono::steady_clock` around the loop. `realtime x4.89` means 20 s of emulated time took 4.09 s of host time. The figure is at least 1 for a live `play` to keep up; the margin is what the audio thread has left for the resampler and the host.
- `peak` is a check that the load made sound.
- The figure depends on the machine, the build and what else is running. The measured values for both builds are in `docs/verification-plan6.md`.

## Where the pieces are

| Thing | Where |
|---|---|
| The machine: bus, scheduler, devices, analog inputs, RAM image | `src/ks32/machine.{h,cpp}` |
| The panel: display, LEDs, buttons, keys | `src/ks32/panel.{h,cpp}` |
| The DUART | `src/duart/duart.{h,cpp}` |
| The tool | `tools/ks32emu/main.cpp` (file I/O lives here; `src/ks32` stays pure) |
| Live MIDI and audio | `tools/ks32play/midi_in_win.*`, `tools/ks32play/audio_out_wasapi.*` |

## For code that drives `Machine`

- `Machine::midiIn(byte)` sends one byte; a channel message is its bytes in order. A program change (0xC0) and channel pressure (0xD0) have one data byte, the others two.
- `Machine::button(Button, down)` queues a press or a release (`code | 0x80, 00`, and `code, 00`).
- `Machine::key(note, down, velocity)` plays the local keyboard (K12). `velocity` is the keyboard's strike value as the firmware reads it, 1 to 127, not a MIDI velocity: the firmware's touch curve maps it to the MIDI velocity it sends out (a strike of 64 comes out as 20, and 127 as 127). Notes outside 28 to 103 are ignored.
- `Machine::setAnalog(Analog, raw)` sets an input's raw 10-bit reading.
- `Machine::ramImage()` and `Machine::loadRamImage(img, err)` are the battery RAM. Loading an image is a power cycle with the battery connected: the CPU and all devices reset, and the RAM and the analog inputs are kept.

## Verification

Test counts, the speed figures, the cross-build identity check, the open items A to F, the decision table with confidences, the list of judgment calls and what is left open are in `docs/verification-plan6.md`.
