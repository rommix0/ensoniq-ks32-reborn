# ks32play: KS-32 MIDI test player

`ks32play` plays MIDI through the emulated KS-32 sound chips (the OTIS wave chip, the ESP effects DSP and the delay DRAM) with a small stand-in for the KS-32 firmware's voice control. It is a test tool for the chip emulation, not a re-implementation of the KS-32 sequencer, so its choices (MIDI map, envelope, velocity law) are labelled decisions P1 to P15 in `docs/decisions.md`.

This page covers the offline commands, `render` and `waves`. Live `play` (WinMM MIDI in, WASAPI out) is described with the task that adds it.

## What you need

- The ROM images in one directory (default `ks-32`, relative to the current directory): `sq1-u25.bin`, `sq1-u26.bin`, `rom2.u39`, `rom3.u38`. They are not in the repository. Run from the main checkout, or pass `--roms <dir>`.
- The derived data (default `docs/rom-investigation/data`): `sample_map.json`, `esp/esp_programs.json`, `esp/effect_defaults.json`. Pass `--data <dir>` to use another copy.
- Build: `cmake --build build` (MinGW) or `cmake --build build-msvc --config Release`. The executable is `ks32play` (`ks32play.exe` on Windows).

## Commands

```
ks32play render <in.mid> <out.wav> [--resampled <out2.wav>] [--rate <hz>] [--tail <s>] [common]
ks32play waves [--set internal|rom3]
ks32play waves --audition <set>:<n> <out.wav> [common]

common: --roms <dir>   --data <dir>   --effect <0-10>   --bus <0-2>   --wave <set>:<n>
        --adsr <attackMs,decayMs,sustainPct,releaseMs>
```

Options may come before or after the positional arguments. Every option takes a value.

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
```

A successful run exits 0.
