# Plan 3 verification

Closing check of Plan 3 (the MIDI test player): the test suite on both required toolchains,
cross-toolchain identity of the chip-rate render, a sweep of all 11 effect programs on the smoke
file, the live-play device and demo figures, the open decisions, and what the user should listen
for. The user's own acceptance run (the pass bar) is still pending; see the last section.

## Test counts

| Toolchain | Configure | Result |
|---|---|---|
| MinGW-w64 g++ 14, i686, Ninja, Release | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_ROM_DIR=...` | `build/ens_tests`: **212 passed, 0 failed**; `ctest --test-dir build`: 1/1 passed |
| MSVC 19.44 (Visual Studio 2022), x64, Release | `cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DENS_ROM_DIR=...` | `ctest --test-dir build-msvc -C Release --output-on-failure`: 1/1 passed; `build-msvc/Release/ens_tests.exe` reports **212 passed, 0 failed** |

Both builds were configured with `-DENS_ROM_DIR="C:/Users/abart/Desktop/Ensoniq KS-32/ks-32"`, so
the ROM-dependent tests run and are not skipped (for example `synth_rom_note_sounds_and_decays`
prints `ROM note: peak 7913, final AC RMS 0.000, DC 9.000`). ctest registers the whole
`ens_tests` binary as one test, so it counts 1; the 212 are the individual `TEST` cases (Plan 1
ended at 149; the final-review fix round added 3, and the delay-wrap fix, decision E31, added 3 more). Both builds compile without warnings.

## Cross-toolchain identity

`tests/data/player_smoke.mid` was rendered with each build's `ks32play` (default `--effect 1`,
`--tail 4`, ROMs from the main checkout). Both print `frames 290178  duration 9.750 s  peak 26853
clips 0`. (Re-taken after the render warm-up, decision P18: WAV frame 0 is now MIDI time 0 with the
effect already faded in, so the hashes differ from the first verification run.) SHA-256 of the
output files:

| File | MinGW i686 | MSVC x64 | Identical |
|---|---|---|---|
| chip-rate WAV (29 762 Hz, 1 160 756 bytes) | `A28A5BB3A0F6CC075A3AEEBC1EE4837FAE3D462B53CDE3E6AD356AE90CE28573` | `A28A5BB3A0F6CC075A3AEEBC1EE4837FAE3D462B53CDE3E6AD356AE90CE28573` | **yes** |
| `--resampled` 48 kHz WAV | `1FC783AF34137966904A58E6B303F2565AAED895DCFDBDA08DE75679D4D5EB9A` | `1FC783AF34137966904A58E6B303F2565AAED895DCFDBDA08DE75679D4D5EB9A` | **yes** |

The chip-rate output is bit-identical between 32-bit MinGW and 64-bit MSVC, as the Global
Constraints require. (The resampled file uses floating-point filtering, and it is also identical
here, but only the chip-rate file is a requirement.)

## Effect sweep (`--effect 0` to `10`, smoke file)

Each row is `ks32play render tests/data/player_smoke.mid out.wav --effect N --tail 12` on the
MinGW build (528 273 frames, 17.75 s). The last note-off is at 5.75 s (total length 9.750 s minus
the default 4 s tail). Measures (both channels, L / R):

- **Peak** is the largest absolute sample. **Clips** is the count printed by `render`.
- **-60 dB raw**: the end of the last 0.1 s window (2976 frames, counted from frame 0) whose RMS
  is at or above -60 dBFS (32.768 LSB), the same definition as `tools/mame_ref/analysis/decay_time.py`,
  given as seconds after the last note-off. "(never)" means the last window of the file is still
  at or above the threshold.
- **-60 dB AC**: the same on the AC RMS of each window (the window mean removed). The effect
  outputs settle on a small DC (decision P16; first seen in the Task 5 decay test), so the AC figure is the decay of the sound
  itself; it is the one to read.
- **DC / AC** of the final 0.1 s window (11.9 s to 12.0 s after the note-off).

| Effect | Peak L / R | Clips | -60 dB raw (s) | -60 dB AC (s) | Final DC L / R (LSB) | Final AC RMS L / R (LSB) |
|---|---|---|---|---|---|---|
| 0 | 29452 / 30523 | 0 | 2.6 / 2.7 | 1.2 / 1.1 | -8.1 / -9.6 | 0.435 / 0.509 |
| 1 | 23034 / 26853 | 0 | 1.7 / 1.4 | 0.8 / 0.8 | -11.0 / -7.0 | 0.000 / 0.000 |
| 2 | 25687 / 28684 | 0 | 2.2 / 1.8 | 0.8 / 0.8 | -12.0 / -8.0 | 0.000 / 0.000 |
| 3 | 20833 / 22978 | 0 | 0.6 / 0.6 | 0.6 / 0.6 | -3.9 / -3.1 | 0.341 / 0.322 |
| 4 | 27958 / 22799 | 0 | 0.2 / 0.2 | 0.2 / 0.2 | 0.0 / 0.0 | 0.000 / 0.000 |
| 5 | 29106 / 29835 | 0 | 3.6 / 3.8 | 1.3 / 1.4 | -8.9 / -10.2 | 0.621 / 0.655 |
| 6 | 32466 / 31894 | 0 | 3.6 / 3.8 | 1.4 / 1.4 | -10.9 / -12.8 | 0.466 / 0.432 |
| 7 | 26101 / 24671 | 0 | 5.5 / 5.8 | 2.1 / 2.2 | -14.4 / -16.5 | 0.548 / 0.527 |
| 8 | 23811 / 24074 | 0 | 0.2 / 0.2 | 0.2 / 0.2 | -1.0 / 0.0 | 0.000 / 0.000 |
| 9 | 22542 / 21275 | 0 | 7.2 / 7.8 | 1.9 / 2.0 | -19.0 / -22.0 | 0.546 / 0.584 |
| 10 | 9328 / 7965 | 0 | **12.0 (never) / 12.0 (never)** | 0.7 / 0.7 | **-158.0 / -72.1** | 0.501 / 0.385 |

The table was re-taken after the render warm-up (decision P18), which starts every effect fully
faded in at frame 0, with a rewritten measuring script (same definitions, times rounded up to the
next 0.1 s). On the pre-warm-up WAVs that script reproduces the first run's rows for effects 1 and
10 to within one 0.1 s step. A negative time means the last window over -60 dB ended before the
last note-off.

Flags (clips, or a raw tail over 10 s):

- **Clips: none.** The largest peak is 32466 (effect 6 left), below 32767.
- **Effect 10** is flagged for its raw time only. The row was re-taken after the delay-wrap fix
  (decision E31, 2026-10-21); the other rows are unchanged by it (their programs never reach the
  changed case, and effect 1's WAV is byte-identical before and after). Before the fix, effect 10
  cut off held notes (AC -60 dB time -3.5 s, before the last note-off; row 8839 / 7797, raw never /
  -2.3, DC -58.5 / -26.3), because program 10 starts with the ESP's delay pointer (DBASE) far above
  its short delay ring and its delay writes overwrote its DRAM lookup tables (P16). Now its sound
  decays like the other reverbs (AC 0.7 s). Its raw time never ends because the program idles on a
  constant -158 / -72 LSB DC (-46 dBFS) from the moment it is loaded. That DC was traced to the
  program's own sine waveshaper, which is indexed half a table entry below the table's zero (P16), so
  it is a property of the firmware's program and not of the core. The KS-32's outputs are AC-coupled.
  The peak (about 9300 against 20 000 to 32 000 for the others) is the program's level with the
  default parameters (P11); it was about the same before the fix.
- Effects 7 and 9 have the longest real tails: 5.5 to 7.8 s raw (about 2 s on the AC measure). Their DC
  was still moving at 12 s (effect 9: -19.6 / -22.9 at 12 s, -19.0 / -22.0 in the last window),
  so the raw time depends on the DC and not only on the reverb.
- Effects 4 and 8 have almost no tail (0.2 s) and settle at 0 DC.

Raw numbers are stated first because that is what a plain `decay_time.py` run would print; the
AC column is the one that describes the sound.

## Live play: devices and demo run

`build-msvc/Release/ks32play.exe play --list` (exit 0):

```
MIDI inputs:
  0: theLooper
  1: Q25
audio output (default device): 48000 Hz, 2 channels, 32-bit float, device period 10.0 ms
```

The user's keyboard is `Q25`, index 1. Index 0, `theLooper`, is a virtual port. The default
output device runs at 48 kHz.

The `--demo` run from Task 10 (the report at `.superpowers/sdd/2026-10-20-midi-test-player/task-10-report.md`,
before this verification; not repeated here because it plays on the speakers):

```
audio: 511 periods, 245856 frames (5.12 s), 0 underruns, peak 0.239 (-12.4 dBFS), 0 clipped, slowest refill 2.49 ms; demo 64 events
```

That is 0 underruns and a worst refill of 2.49 ms against a 22 ms buffer, with a peak 12.4 dB
below full scale.

## Open and provisional decisions (`docs/decisions.md`, `## Player`)

| Id | Topic | State | What the user can judge by ear |
|---|---|---|---|
| P11 | Effect parameters | Provisional hand-set inputs: the block is each program's default (not a particular factory sound's), the master volume `$DD72` is hand-set to 127 (GPR 7F = 0x7FFF00), `$DE8A` = 0 where the program never sets it, modulation sources at rest. `--effect N` is an ESP program index; programs 7 and 9 are each shared by two firmware effects (6/7 and 9/10) and the lower effect's parameters are used. Confidence High for tables, Medium for handler values. | Effect level against the dry sound; whether the wet/dry balance is plausible for each program |
| P17 | Effect changes while a load runs | At most one change waits behind the load in progress; a later CC 102 replaces it, so a knob sweep loads the first and the last value. Confidence Medium. | A fast CC 102 sweep should end on the last value after about two fades, not cycle through every effect |
| P13 | Output fade | The ramp length (666 frames, 22.4 ms) is a cycle-count estimate (1 to 3 frames per host transfer are possible). The player ramps linearly in one write per frame. Confidence Low-medium. | Clicks or pumping at effect switches (CC 102) |
| P5 | Envelope | Linear in the volume byte, stand-in for the firmware's software envelope. Confidence Medium. | Attack and release feel |
| P6 | Velocity and CC 7 law | Stand-in, not measured. Confidence Low. | Dynamics |
| P7 | Note end | A short fade then mute. Confidence Medium. | Clicks at note end |
| P8 | Stealing and retrigger | A guess at polyphony practice, not the firmware rule. Confidence Low. | Behaviour when more than 32 notes sound |
| P16 | ESP output DC residue; program 10 | Accepted (option a) for programs 0 to 9 (Medium). Program 10: the cut-off of held notes is fixed by E31, a full-modulo delay wrap in the ESP core (Low-medium; the hardware may instead do one subtraction and rely on the firmware's halted time). Its idle DC (-158 / -72 LSB) is traced to the program's own waveshaper constants and accepted (High). | Effect 10 should now sustain held notes and later notes like the other reverbs; a cut-off about 1.5 s after the load would point back at E31 |

P1 to P4, P9, P10, P12, P14, P15 and P18 (render warm-up) are settled to Medium or better and need
no listening judgement.

The Plan 1 decisions rated Low or Low-medium (`docs/verification-plan1.md`) still apply to the
chip cores; in particular E24 (a halt-enable write mid-program discards the in-flight pipeline) is
what an effect change exercises, so clicks at CC 102 may also be related to it.

## What to listen for

- **Waves.** Each program change selects a wave. Compare the pitch and timbre of several waves
  (internal set 0 and ROM 3 via CC 0 = 1). Pitch should track the keyboard across zones with no
  jump at the zone edges beyond what the sample root differences give.
- **Effects.** CC 102 from 0 to 10: the sound should fade out, switch and fade back in over about
  22 ms. Reverb tails should sound smooth and decay to silence. Effects 4 and 8 are short, the
  rest are long. Effect 10 should sustain held notes and play later notes like the others. A cut-off about 1.5 s after the load would point back at decision E31. Its peak
  is lower (about 9300 against 20 000 to 32 000 for the others).
- **No clicks or pumping** at note start, note end, effect switch and pedal.
- **Silence at rest.** With nothing held, the output should be silent (a faint DC is inaudible).
- **Level.** The overall level should be comfortable with the default CC 7 (100) and full-velocity
  keys; a loud, distorted or clipping sound is worth reporting along with the effect and wave.
- **Latency.** Keys should feel responsive. The device period is 10 ms and the output buffer is
  about 22 ms.

## User acceptance: pending

Not done yet. The user runs the player with their own keyboard and records the verdict here. From
the worktree root, with the MSVC build:

```powershell
build-msvc\Release\ks32play.exe play --demo --roms "C:/Users/abart/Desktop/Ensoniq KS-32/ks-32"
build-msvc\Release\ks32play.exe play --midi 1 --roms "C:/Users/abart/Desktop/Ensoniq KS-32/ks-32"
```

The first plays the built-in arpeggio on the default output (about 5 s, wave `0:0`, effect 1) and stops by itself. The
second connects the Q25 (MIDI input 1) for live play. It prints `Press Enter to quit.`; Enter or Ctrl+C stops it and prints the stats line.

MIDI map (details in `docs/player.md`, decision P10):

| Message | Effect |
|---|---|
| Program change `n` | Selects wave `n` in the set last chosen by CC 0 |
| CC 0 | 0 = internal set, 1 = expansion ROM 3; takes effect at the next program change |
| CC 102 | Effect 0 to 10 (an ESP effect load with a fade) |
| CC 103 | Bus 0 to 2 |
| CC 7 | Channel volume |
| CC 64 | Sustain pedal |
| CC 120 / CC 123 | All sound off / all notes off |

`ks32play waves --set internal` and `waves --set rom3` list the wave numbers.

Verdict: _(to be recorded after the user's run)_

## Reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_ROM_DIR="C:/Users/abart/Desktop/Ensoniq KS-32/ks-32" && cmake --build build
ctest --test-dir build --output-on-failure
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DENS_ROM_DIR="C:/Users/abart/Desktop/Ensoniq KS-32/ks-32"
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

Cross-toolchain identity (PowerShell, from the worktree root):

```powershell
$r = "C:/Users/abart/Desktop/Ensoniq KS-32/ks-32"
build\ks32play.exe render tests/data/player_smoke.mid mingw.wav --roms $r
build-msvc\Release\ks32play.exe render tests/data/player_smoke.mid msvc.wav --roms $r
Get-FileHash mingw.wav, msvc.wav
```

Effect sweep, for N in 0 to 10 (then measure the WAV as described above; 29 762 Hz, 16-bit
stereo):

```powershell
build\ks32play.exe render tests/data/player_smoke.mid effN.wav --effect N --tail 12 --roms $r
```

The `-60 dB` measure is the one in `tools/mame_ref/analysis/decay_time.py` (0.1 s windows of 2976
frames, RMS at or above 32.768 LSB), applied to each WAV channel and to the same channel with each
window's mean removed. The sweep used a small throwaway Python script that is not committed.
