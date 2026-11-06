# AGENTS.md

Guidance for coding agents working in this repository. Read [README.md](README.md) first for what the
project is; this file is how to work on it.

## The project in one paragraph

An emulator of the Ensoniq KS-32 synthesizer that runs the instrument's own OS v3.01 from the ROMs in
`ks-32/`. Plain C++17 chip cores (68000, ES5505 OTIS, ES5510 ESP, 68681 DUART) make up a KS-32
machine (`src/ks32`), a real-time engine drives it (`src/app`), and a JUCE 8 app wraps that engine as
a Standalone and VST3 (`app/`) for Windows x64 (MSVC) and Linux x64 (GCC).

## Rules that are not negotiable

- **Run the firmware; never study it.** The OS is run as-is in emulation. Do not disassemble,
  decompile, annotate or map the firmware, and do not propose it. Hardware behaviour comes from the
  service manual, the chip datasheets (`docs/reference/`), and bus traces of the running OS.
- **MAME is a comparison point, never a reference.** Do not copy MAME's behaviour to match it.
  `tools/mame_ref/vendor/` holds unmodified MAME sources (BSD-3-Clause) for an optional comparison
  tool only; keep them unmodified and out of the app.
- **No JUCE, OS calls or file I/O under `src/`.** The cores, the machine and the engine stay plain
  C++17 and build on every toolchain (MinGW-w64 i686, MSVC x64, GCC x64 on Linux). File I/O belongs in `tools/`,
  JUCE only in `app/`.
- **The audio path is real-time safe.** Nothing in `Engine::render` and below may allocate, lock or
  block after the boot. `ens_rt_tests` counts allocations and must report 0.
- **Ensoniq material.** The six ROMs the app needs are in `ks-32/`; add no other ROMs, service
  manual images or datasheets to git (`datasheets/` is ignored on purpose). A dist built with
  `-RomFolder` contains the ROMs: never publish one.
- **Tests never touch the user's own settings or memory.** Point the app at a temp folder with
  `KS32_SETTINGS_DIR`.

## Building

PowerShell or Git Bash on Windows. Build folders are `build*/` (ignored).

```powershell
# Core, MinGW-w64 i686 + Ninja (the compiler is in C:\winlibs\mingw32\bin; put it on PATH first)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Core, MSVC x64
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release

# The JUCE app (MSVC x64 only). JUCE is fetched into the build folder; to reuse the local copy
# add -DFETCHCONTENT_SOURCE_DIR_JUCE="<repo>/build-juce/juce-src"
cmake -S . -B build-app -G "Visual Studio 17 2022" -A x64 -DENS_BUILD_APP=ON
cmake --build build-app --config Release --target KS32_Standalone KS32_VST3 KS32_AppTests

# A release folder in dist\KS-32-<version> (static C++ runtime, its own build-dist folder)
powershell -ExecutionPolicy Bypass -File tools\dist.ps1 [-RomFolder ks-32]
```

On Linux (Ubuntu in WSL works; JUCE's apt packages are listed in the README):

```bash
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake -S . -B build-linux-app -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_BUILD_APP=ON
cmake --build build-linux-app --target KS32_Standalone KS32_VST3 KS32_AppTests
tools/dist.sh [--rom-folder ks-32]   # dist/KS-32-<version>-linux
```

## Testing

| What | Command | Notes |
|---|---|---|
| Core suite | `build\ens_tests.exe`, `build-msvc\Release\ens_tests.exe` | Must pass on **both** toolchains. Firmware tests read `ENS_ROM_DIR` (default `ks-32/`). |
| Real-time | `build\ens_rt_tests.exe` | Allocations after the boot must be 0. |
| 68000 suite | `ctest --test-dir build -R m68k_suite` | Needs Tom Harte's tests in `testdata/` (ignored; see `docs/m68k.md`). |
| App | `build-app\app\KS32_AppTests_artefacts\Release\KS32_AppTests.exe` | ROM folder, memory file, parameters, plugin state, and the built VST3 loaded headlessly. |

**The reference hashes must not change** unless a change is meant to alter emulation output. They are
printed, not asserted, so compare them by hand: `ens_tests` prints `boot trace hash d2afd0fed8861cbd`
and `render hash 2bae46b3acc34cd0`, and
`ks32emu render tests\data\player_smoke.mid out.wav --roms ks-32` prints `pcm hash a479ba867298cc55`
on every toolchain (Linux GCC included). If one changes on purpose, say why and update the verification report.

Tests use the tiny registry in `tests/check.h` (`TEST(name) { CHECK(...); CHECK_EQ(a, b); }`). A new
test file is one line in `ENS_TEST_SOURCES` in `tests/CMakeLists.txt`. For bugs, write the failing test
first, then fix.

Things only a person can check (sound, a MIDI keyboard, a DAW) go in the current verification report
as "user to run", not claimed as passed. `KS32_SNAPSHOT=<file.png>` makes the editor save a PNG of
itself 3 s after opening, for layout checks without a person.

## Where things are written down

- `docs/decisions.md`: every choice the datasheets or the firmware leave open, as an entry with
  Options / Choice / Evidence / Confidence / Status (template at the top). Ids by area: `O` OTIS,
  `E` ESP, `D` DUART, `M` 68000, `TR` trace runner, `P` MIDI test player, `K` machine, `A` app. Mark the code at the point of use with
  `// DECISION <id>: <one line>`. A new behaviour choice gets a new entry, not just a comment.
- `docs/ks32-hardware-interface.md`: what is known about the KS-32 hardware and how it was found.
- `docs/design/`: one design per stage. `docs/superpowers/plans/`: the implementation plans.
- `docs/verification-planN.md`: the closing check of each plan (both toolchains, hashes, manual
  checks, what is left open). Treat old plans and reports as history; correct live docs instead.
- User-facing guides: `docs/ks32app.md` (the app), `docs/ks32emu.md` (the command-line machine),
  `docs/player.md` (the MIDI test player).

When behaviour changes, update the live guide and the decision entry in the same change.

## Style

- C++17, 4-space indent, lines up to about 120 columns, warnings clean at `/W4` and `-Wall -Wextra`.
- Comments say why, and cite their source (datasheet section, service manual page, decision id).
- Docs are plain, short sentences; no marketing.
- Commits: `feat(app): …`, `fix(esp): …`, `test(app): …`, `docs: …`, scoped by folder. Work on a
  plan happens on a `planN-…` branch merged into `main` with a merge commit.
