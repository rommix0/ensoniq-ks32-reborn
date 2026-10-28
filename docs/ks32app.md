# KS-32 app: the emulated KS-32 with its front panel

The KS-32 app runs the user's KS-32 OS ROM (v3.01) on the emulated machine of Plan 6 (`docs/ks32emu.md`) and draws the instrument's front panel around it: the 2 x 16 display, the 15 LEDs, every button, the Volume and Data Entry sliders, the two wheels and the 76 keys. The firmware drives the display and the LEDs and reads the controls, so every screen is the firmware's own. The app plays from a MIDI keyboard and from the on-screen keys.

It is built in two formats from the same code: a Standalone program (`KS-32.exe`) and a VST3 instrument (`KS-32.vst3`). Windows x64 only.

The design is `docs/design/2026-10-28-ks32-juce-app-design.md`, the decisions are `A1` to `A14` in `docs/decisions.md` (`# KS-32 app`), and the verification report is `docs/verification-plan7.md`.

## Building

The app is behind the CMake option `ENS_BUILD_APP` (default OFF), so the default builds (`build/`, `build-msvc/`) never need JUCE. It builds only with MSVC x64:

```powershell
cmake -S . -B build-app -G "Visual Studio 17 2022" -A x64 -DENS_BUILD_APP=ON -DENS_ROM_DIR="C:/path/to/ks-32"
cmake --build build-app --config Release --target KS32_Standalone KS32_VST3 KS32_AppTests
```

- The first configure fetches JUCE 8.0.16-security (git, about 150 MB) into `build-app/_deps`. Nothing is fetched into the source tree.
- `ENS_ROM_DIR` is used only by the checks in `KS32_AppTests`; the app itself has no compiled-in ROM path.
- The outputs are `build-app\app\KS32_artefacts\Release\Standalone\KS-32.exe` and the bundle folder `build-app\app\KS32_artefacts\Release\VST3\KS-32.vst3`. The build does not install the VST3; to use it in a DAW, copy the `KS-32.vst3` folder to `C:\Program Files\Common Files\VST3` (or a folder the DAW scans).
- Checks: `build-app\app\KS32_AppTests_artefacts\Release\KS32_AppTests.exe` (or `ctest --test-dir build-app -C Release -R app_tests`). It checks the ROM folder and the memory file, and loads the built VST3 through JUCE's VST3 host: boot, a note, sound on both channels, and the editor (it skips that part without the ROMs).

**JUCE licence.** JUCE is dual-licensed: AGPLv3, or a commercial JUCE licence. A build of this app links JUCE, so giving the app to anyone else is under the AGPLv3's terms unless you hold a commercial JUCE licence. Building and using it yourself needs neither.

## First start and the ROMs

The ROMs are not in the repository. The app needs six files in one folder:

| File | What it is |
|---|---|
| `ks32/ks32v301upper.bin`, `ks32/ks32v301lower.bin` | The OS v3.01 (in a `ks32` subfolder) |
| `sq1-u25.bin`, `sq1-u26.bin`, `rom2.u39`, `rom3.u38` | The wave ROMs |

On the first start no folder is chosen. The display reads `No ROM folder:` / `open Settings`. Click the gear under the "ROM v3.01" and "MIDI" lines on the left, then **Choose folder…** in ROMS. The list marks each file found, missing or wrong size. When all six are good the machine starts, and the firmware boots in real time, as the instrument does: the start-up screens for about two seconds, then `ROM   00` / ` KS Grand Piano` with the Select Sound LED lit. If a file is missing or the wrong size, the display names it (for example `ROM missing:` / `sq1-u26.bin`). With the ROMs good but no audio device open (the machine only runs with one), it reads `No audio device`. Choosing another good folder later restarts the machine and keeps its memory.

## Where the settings and the memory live

- **`%APPDATA%\KS-32\settings.xml`**: the ROM folder and the memory file. Each change is saved at once. The Standalone and the VST3 share it.
- **`%APPDATA%\KS-32\ks32-memory.bin`** (the default; **Change…** on the settings page picks another file): the battery memory, the whole 64 KB RAM, so the firmware's settings, edited sounds and sequences survive a restart. It is loaded at start (a file of the wrong size is reported on the settings page and ignored, and the machine starts cold). It is saved on close and every 60 s, but only once the machine has booted (A11). Each save writes `<file>.tmp` and then replaces the file, so a crash in the middle of a save keeps the old image. **Change…** refuses a file in the ROM folder (`ks32v301lower.bin` is 64 KB too) and one that cannot be written; a save that fails is reported on the settings page (`Memory not saved: <file>`).
- **Erase memory…** asks first, then writes 64 KB of zeros to the memory file at once and restarts the machine from it: the firmware rebuilds its memory as it boots (A11, ruling R7). If the file cannot be written, nothing is erased, the machine keeps running and the settings page says so.
- **`%APPDATA%\KS-32\KS-32.settings`**: the Standalone wrapper's own file (JUCE's), with the audio device, buffer size and MIDI inputs.
- **Several VST3 instances share one memory file.** Each loads it at start and saves it on close and every 60 s, with no lock, so the last one to write wins. Open one instance per project if the memory matters.

## Audio and MIDI

- **Standalone.** The settings page's AUDIO AND MIDI section is JUCE's device selector: output device, buffer size and MIDI inputs. Enable the keyboard (for example the Q25) there. Under it a line gives the rates ("The KS-32 runs at 29 762 Hz and is resampled to … Hz") and the underrun count since start. The wrapper's own Options menu also stays.
- **VST3.** The host owns the devices, so that section is hidden. MIDI comes from the host's track, every byte as the MIDI IN jack would receive it, sysex and real-time included (A12). The output is one stereo bus. The plugin has no saved state of its own: its memory is the memory file.
- **MIDI out** from the firmware is counted and not sent anywhere.

## Using the panel

- **Buttons.** A press is sent on mouse-down and its release on mouse-up, wherever the mouse is (A9). Space presses the focused button while held. Every control has a tooltip.
- **The chord latch** (A9). Right-click or Shift-click latches a button down, drawn pressed, until it is clicked again. That is how two buttons are held with one mouse. The version screen: click **Edit Seq/Preset**, right-click **Bank 9**, click **Screen 1**, then right-click **Bank 9** again to release it; the display reads `ENSONIQ KS-32` / `OS Version 3.01` (the firmware's own text). The firmware wants the mode button released before Bank 9 is pressed, which a plain click does. When the app stops being the foreground program (Alt-Tab), every button held by the mouse is released; latched buttons stay down.
- **Sliders and wheels** drag vertically. Volume starts at maximum and Data Entry at mid. The pitch wheel springs back to centre on mouse-up; the mod wheel stays where it is left.
- **Keys.** Click near the front of a key for loud and near the back for soft (A10: strike 1 to 127 along the key, which the firmware reads through its own touch curve). Dragging across the keys plays each key entered and releases the one left. Notes arriving on MIDI are not drawn on the keys.
- **Display codes.** Right-click the display for **Show display codes**: a hex overlay of every cell's code.
- **Size.** The window opens at 1360 x 480 and resizes from 1020 x 360 up, keeping the panel's shape (A13).

## Two firmware behaviours to know

These are OS v3.01's own behaviour (found in Plan 6). The app does not work around them.

- **It always powers on to ROM 00.** Whatever sound was selected when the app closed, it starts on `ROM 00 KS Grand Piano`. The rest of the memory (Master Tune and the like, edited sounds, sequences) is kept.
- **About six start-ups in a row of under about 2 s each make the firmware reinitialize its memory.** That is the manual's system-reset sequence. The app saves memory only after the boot has finished (about 3 s), so a quick open and close leaves the memory file as it was and cannot set this off from the file. Erase memory… is the deliberate way to reset.

## Transport buttons

- **Play** (code 09) makes the firmware send MIDI Start (`FA`), and **Stop/Continue** (code 29) MIDI Stop (`FC`) (A14). With an empty sequence neither changes the display or the LEDs, and MIDI out is not forwarded, so pressing them shows nothing. How Continue is given (a chord or a second press) was not tested.
- **Record** has no code: the hunt found none. It is drawn dimmed, does nothing, and its tooltip says "Not available: code not found".

## Debug hooks

Two environment variables, read when the app starts. Neither is needed for normal use.

- **`KS32_SETTINGS_DIR=<folder>`** uses that folder instead of `%APPDATA%\KS-32` for `settings.xml` and the default memory file. The checks use it so they never touch your own settings and memory; it is handy for trying a fresh first start too. (The Standalone wrapper's `%APPDATA%\KS-32\KS-32.settings` is not moved.)
- **`KS32_SNAPSHOT=<file.png>`** makes the panel editor write a PNG of itself 3 s after it opens. It is how the layout was checked without a person looking.

## Open items

- **Transport codes (A14).** Record is not found. Play and Stop/Continue are named from the MIDI bytes they send and the panel's labels (Medium confidence), and Continue is untested.
- **Display glyphs (A7).** Codes 0x00 to 0x1F and 0x7F have no known glyphs, so they are drawn blank. Show display codes shows them in hex, so their meaning can be found by watching. The firmware also defines one custom character at boot (`93`, which looks like a note), and which code it is was not found.
- **Blink rate (A8).** 2 Hz at 50 % duty for blinking cells and LEDs, a choice, not a measurement.
- The full list, with what is left for the user's acceptance pass, is in `docs/verification-plan7.md`.
