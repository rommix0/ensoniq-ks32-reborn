# ks32play: KS-32 MIDI test player

`ks32play` plays MIDI through the emulated KS-32 sound chips (the OTIS wave chip, the ESP effects DSP and the delay DRAM) with a small stand-in for the KS-32 firmware's voice control. It is a test tool for the chip emulation, not a re-implementation of the KS-32 sequencer, so its choices (MIDI map, envelope, velocity law) are labelled decisions P1 to P15 in `docs/decisions.md`.

This page covers the offline commands, `render` and `waves`, and live `play` (WinMM MIDI in, WASAPI out, Windows only).

## What you need

- The ROM images in one directory (default `ks-32`, relative to the current directory): `sq1-u25.bin`, `sq1-u26.bin`, `rom2.u39`, `rom3.u38`. They are not in the repository. Run from the main checkout, or pass `--roms <dir>`.
- The derived data (default `docs/rom-investigation/data`): `sample_map.json`, `esp/esp_programs.json`, `esp/effect_defaults.json`. Pass `--data <dir>` to use another copy.
- Build: `cmake --build build` (MinGW) or `cmake --build build-msvc --config Release`. The executable is `ks32play` (`ks32play.exe` on Windows).

## Commands

```
ks32play render <in.mid> <out.wav> [--resampled <out2.wav>] [--rate <hz>] [--tail <s>] [common]
ks32play waves [--set internal|rom3]
ks32play waves --audition <set>:<n> <out.wav> [common]
ks32play play [--midi <index>] [--list] [--demo] [common]

common: --roms <dir>   --data <dir>   --effect <0-10>   --bus <0-2>   --wave <set>:<n>
        --adsr <attackMs,decayMs,sustainPct,releaseMs>
```

Options may come before or after the positional arguments. Every option takes a value, except `--list` and `--demo`. `ks32play --help` (or `-h`) prints the usage text and exits 0. An unknown command or a wrong number of arguments prints an error and the usage text, and exits 2.

### render

Reads a Standard MIDI File (format 0 or 1, PPQ timing), plays it through the chip emulation and writes a WAV.

- `<out.wav>` is the chip output: 16-bit stereo at 29762 Hz (the true rate is 30476180 / 1024 = 29761.9 Hz; the header holds the rounded value). It is deterministic: the same file, ROMs and options give a byte-identical WAV, and the MinGW and MSVC builds agree.
- `--resampled <out2.wav>` also writes a copy converted to `--rate` Hz (default 48000), 16-bit, rounded and clamped. The converter's filter delay is removed so the two files line up in time.
- `--tail <s>` (default 4) is the time rendered after the last MIDI event, so reverb and releases can die away. The total length is the last event plus the tail.

It prints one summary line to stdout, then the synth's log lines indented by two spaces:

```
frames 290178  duration 9.750 s  peak 26861  clips 0
  wave 0:0
  esp booted
  effect 1 loaded (id 01)
  wave 0:5
```

`peak` is the largest absolute sample of the chip-rate file. `clips` counts samples equal to 32767 or -32768. The log lists wave and effect selections, controller values that were out of range and ignored, and the ESP boot and effect loads.

The render is held in memory, so its length (last event plus tail) is capped at 4 hours; a longer file is rejected with `error: '<file>': render length N s is over the 4 h limit`.

Events in the first 0.07 s (about 2100 chip frames) fall during the ESP boot and the first effect load, before the effect output is faded in, and are mostly inaudible. `tests/data/player_smoke.mid` starts its first note at 0.75 s for this reason.

### waves

`waves` lists the sample map, one line per wave:

```
1:0  VIOLA SECT  3 zones  [0-68 root 59 bidir]  [69-107 root 68 bidir]  [108-127 root 111 one-shot]
```

- `set:number`: set 0 is the internal ROM (126 waves with zones), set 1 is the expansion ROM 3 (43 waves). Waves without zones are not listed. The two counts are printed at the end.
- The name (`-` when the sample map has none).
- The number of key zones, then each zone as `[lowest-highest key, root key, loop mode]`. The root key is round(-pitchWord / 256) (the pitch word is -256 per root semitone; see `docs/rom-investigation/findings.md`). The loop mode is `one-shot`, `loop` (forward) or `bidir` (bidirectional), from the low byte of the OTIS control register.
- `note:` carries any caveat the sample-map extraction recorded for that wave.

`--set internal` or `--set rom3` lists one set. Listing needs only the data directory, not the ROMs.

`waves --audition <set>:<n> <out.wav>` renders one wave: each zone's root key (rounded), 1 s per note, 0.5 s apart, after a 0.25 s lead-in that covers the ESP boot and effect load. A tail of up to 1 s follows (`--tail`, capped at 1). It uses the same render path and options as `render` and prints the same summary line. If a zone's root key lies outside the zone's own key range, the key is played by a neighbouring zone, and a line says so.

### play

`play` plays the chips live: MIDI from a WinMM input device, audio to the default Windows output device through WASAPI in shared mode. It uses the common options (`--wave`, `--effect`, `--bus`, `--adsr`, `--roms`, `--data`) as `render` does; the MIDI map below applies, so program change, CC 0, CC 102 and CC 103 change wave, effect and bus while playing.

```
ks32play play --list
ks32play play --midi 1 --wave 1:0
ks32play play --demo
```

- `--list` prints the MIDI inputs (`index: name`, or `no MIDI input devices`) and the default audio device's mix format, then exits 0. It needs no ROMs.
- `--midi <index>` picks the MIDI input (default 0). An index past the end fails with `error: MIDI input 99 not found (2 devices)`. With no MIDI inputs at all, `play` fails with `no MIDI input devices (use --demo to test audio)`. A device another program holds open fails with `midiInOpen error 4, in use by another program`.
- `--demo` needs no MIDI device: it plays a C-E-G-C arpeggio up and down (8 notes per second for 4 s, after a 0.1 s lead-in that covers the ESP boot and effect load) on `--wave`, then stops by itself 1 s after the last note.

At start it prints the wave, effect and bus, the audio format, the MIDI device and `Press Enter to quit.`, then the synth log lines (indented) as they come. Enter or Ctrl+C stops it. On the way out it prints one line about the run:

```
audio: 513 periods, 246816 frames (5.14 s), 0 underruns, peak 0.239 (-12.4 dBFS), 0 clipped, slowest refill 2.29 ms; demo 64 events
```

- `periods`: buffer refills (one per device period, normally 10 ms); `frames`: device frames written.
- `underruns`: refills that found the device buffer empty, which means an audible gap. It should be 0.
- `peak`: the largest output sample (1.0 = full scale); `clipped`: samples that were outside [-1, 1] and were clamped.
- `slowest refill`: the longest time spent rendering one refill. It must stay well under the device period.
- Live mode ends with `midi N messages, M dropped` instead of the demo count (dropped means the 1024-message queue was full).

How it works:

- Audio: the default render device, shared mode, its mix format (32-bit float or 16-bit PCM, plain or `WAVE_FORMAT_EXTENSIBLE`; any rate; 2 or more channels, the synth on front left and right and silence on the others), event-driven, a 20 ms buffer requested. Any other mix format fails with `error: unsupported audio format (...)`. The render thread joins the MMCSS "Pro Audio" class.
- Each refill (on the render thread): the queued MIDI messages go to the synth, then the chips run in blocks of up to 64 frames through the resampler (chip rate to the device rate) until the refill is covered. MIDI timing is therefore quantised to the device period (about 10 ms), plus the 20 ms buffer and about 1 ms of resampler delay.
- MIDI: the WinMM callback only queues complete channel messages; system messages (clock, active sensing, sysex) are dropped. Data bytes are masked to 7 bits. The synth runs only on the render thread.
- The code is in `tools/ks32play/audio_out_wasapi.*` and `tools/ks32play/midi_in_win.*`; it uses only Windows libraries (winmm, ole32, avrt).
- Both the MSVC and the MinGW builds have `play`. Device names are printed as UTF-8, so a name with non-ASCII characters may show garbled in a console that is not set to UTF-8 (`chcp 65001`).

## Defaults and where each decision lives

| Setting | Default | Decision | Where |
|---|---|---|---|
| `--effect` | 1 | P11 to P14 | `src/player/esp_loader.cpp` |
| `--bus` | 1 (OTIS channel 2) | P3 | `src/player/voices.cpp` |
| `--adsr` | 5 ms, 0 ms, 100 %, 250 ms | P5 | `src/player/voices.h` |
| `--wave` | `0:0` | P10 | `src/player/synth.cpp` |
| Pitch bend range | +/-2 semitones | P9 | `src/player/voices.cpp` |
| FC (pitch) table | formula | P2 | `makeFcTable()` |

Further decisions: P1 (ESP slots per frame), P4 (filters wide open), P6 (velocity and CC 7 law), P7 (note end), P8 (stealing and retrigger), P12 (effect record write order), P13 (20 ms output fade), P14 (ESP boot), P15 (SMF parsing). All are in `docs/decisions.md` with their options and evidence.

The FC table is always computed by `makeFcTable()`. The Task 2 survey compared the firmware's table (at 0xC09F64) with the formula and found them identical, so no separate table file is read.

### Effects

`--effect` is an ESP program index from 0 to 10. Several firmware effects share one program: the mapping from firmware effects to programs is in `docs/rom-investigation/effect-params.md`. The effect names are unknown, so the numbers are the only handle. The mix levels come from the firmware's own default parameters for each program (P11); they are not tuned by ear. Program 1 is the default.

## MIDI map (P10)

Omni: every channel plays, and the channel only keeps its own sustain, pitch bend and volume state.

| Message | Effect |
|---|---|
| Note on / note off | Plays or releases a key. Note on with velocity 0 is a note off. |
| Pitch bend | 14-bit, +/-2 semitones at the ends (P9) |
| Program change | Selects wave number `n` in the set last chosen by CC 0 |
| CC 0 | 0 = internal set, 1 = expansion ROM 3; takes effect at the next program change |
| CC 7 | Channel volume (P6) |
| CC 64 | Sustain pedal (64 and above = down) |
| CC 102 | Effect 0 to 10: queues an ESP effect load (fade out, halt, load, resume); other values are ignored and logged |
| CC 103 | Bus 0 to 2; others are ignored and logged |
| CC 120 | All sound off |
| CC 123 | All notes off |

Everything else is ignored. A program change to a wave with no zones is logged and the previous wave stays selected.

## The smoke file

`tests/data/player_smoke.mid` is 6 s at 120 bpm on channel 0: a C-major arpeggio, a four-note chord, a program change to wave 5, a held note under a pitch bend sweep, and the chord again. It uses no CC 102, so the effect stays at `--effect`. It is generated by `python -I tools/ks32play/make_test_mid.py tests/data/player_smoke.mid` (pure Python, deterministic) and committed.

```
ks32play render tests/data/player_smoke.mid smoke.wav --resampled smoke48.wav
```

## Errors

Any error prints one line to stderr and exits with status 2, for example:

```
error: cannot open ROM 'nowhere/sq1-u25.bin'
error: cannot open MIDI file 'nosuch.mid'
error: bad value '11' for --effect
error: MIDI input 99 not found (2 devices)
```

Usage errors (unknown command, wrong argument count) are followed by the usage text.

A successful run exits 0.
