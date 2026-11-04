# KS-32 VST3 Release (Plan 8): Design

Date: 2026-11-04
Status: implemented by Plan 8 (branch plan8-vst3-release, `docs/verification-plan8.md`), pending user acceptance. Approved by the user, 2026-11-04 ("Approve, subagents"); plan `docs/superpowers/plans/2026-11-04-ks32-vst3-release.md`.
Follows: `docs/design/2026-10-28-ks32-juce-app-design.md` (Plan 7, "the app design"), whose section 4
deferred plugin state, host automation and multiple instances to this stage. Plan 7 passed the
user's acceptance on 2026-11-04 (`docs/verification-plan7.md`); its V1 note 2 is the reason for this
plan: the VST3 loads and plays in FL Studio and Cakewalk, but a saved project does not bring it back.

How it was decided: the user answered two questions and then asked to skip the rest and proceed,
and later added automation. What the user said is marked as theirs; everything else marked *my
call* is the designer's, open to the user's correction when they read this.

## 1. Intent

Make the KS-32 VST3 behave like a proper instrument plugin in a DAW project: save the project, reopen
it, and every KS-32 on it comes back as it was left: its memory (settings, edited sounds,
sequences), the sound it was on, and its slider and wheel positions. Several instances on one
project each keep their own memory. The panel's analog controls and the sound number can be
automated from the host.

**The user's words.** "Usually one, but I would like others to be able to use more than one
instance." "If the instance is new start from the shared memory. If from the project file, load
memory states from that project file." "I do want automation actually."

## 2. Goals

1. **Plugin state in the project.** `getStateInformation` saves, per instance, the 64 KB battery
   RAM, the sound, and the parameter values (goal 3). `setStateInformation` restarts the machine
   from that RAM (a warm boot, as the real unit's battery gives) and, once the boot is done, selects
   the saved sound again (OS v3.01 always powers on to ROM 00, K11).
2. **Per-instance memory in the VST3** (the user's rule). A new instance, with no project state,
   starts from the shared memory file. An instance restored from a project loads the memory from
   the project. The VST3 never writes the shared file by itself, so instances no longer race on it.
3. **Host automation.** Five parameters: Volume, Data Entry, Pitch Wheel, Mod Wheel and Sound.
4. **The on-screen keys show notes arriving on MIDI** (*my call*, the optional item).
5. **The Standalone gets the sound and the sliders back too** (*my call*: same mechanism, through the
   JUCE wrapper's own saved state). Its memory stays in the memory file, as today.

## 3. What "passed" means

The user's bar (*my call*, from the V1 note): in FL Studio or Cakewalk,

- a project with one KS-32 on, say, ROM 55 with Master Tune at +02 and Volume lowered, saved and
  reopened, boots and comes back on ROM 55, Master Tune +02, Volume where it was;
- two KS-32s on one project, on different sounds and with different Master Tune, come back each as
  it was, and the shared memory file's time stamp has not changed;
- a new KS-32 added to a project starts with the shared memory file's settings;
- a Volume automation lane drawn in the host moves the on-screen slider and the level; a Sound lane
  changes the sound on the display; picking a sound on the panel moves the host's Sound value;
- notes played from the host's track light the on-screen keys.

Supporting evidence the builder delivers:

- engine tests in `ens_tests` (both toolchains, ROM tests skip without ROMs): the sound is tracked
  from the panel and from MIDI in, the firmware's own boot program change is not taken as the
  user's, a saved sound is selected after a warm boot, parameters reach the analog inputs only after
  the boot, the note mask;
- `ens_rt_tests` still finds no allocation in `render` with the new paths;
- `KS32_AppTests`, through JUCE's VST3 host: the state round trip between two instances, the
  shared memory file untouched, the five parameters and one automated value reaching the sound;
- the Plan 6 hashes unchanged (boot trace `d2afd0fed8861cbd`, render `2bae46b3acc34cd0`, smoke
  render `a479ba867298cc55`);
- the decisions log (A15 on) records each call.

## 4. Non-goals (this plan)

- **No full emulator save state** (*my call*). Freezing the CPU, chips and voices would skip the 2 s
  boot and bring back even the page on screen, but every emulator change would break old projects,
  so it would always need the RAM-plus-sound restore as its fallback. RAM plus sound is most of the
  value for a small part of the code.
- **No on-screen wheels following incoming MIDI** (*my call*). The wheels become the automatable
  parameters (section 6.3); drawing incoming MIDI bend on them would show two values on one control.
  The wheels show their parameter, which automation moves.
- **No sound names in the host.** The Sound parameter shows its number. The names live in the
  firmware's display, and the plugin does not read them out of ROM.
- **No MIDI out** to the host. Still counted and dropped (app design section 4).
- **No CPU work for many instances.** Each instance runs a whole KS-32. The readme says so.
- **No lock on the shared memory file.** The VST3 no longer writes it by itself (goal 2), so only
  the Standalone's saves and the explicit "Copy to shared memory…" write it.
- Everything else in the app design's section 4 stays out (memory card, pedals, other platforms).

## 5. Anti-goals

As in the app design, and these in particular:

- **Decompiling or annotating the firmware** (the user's ruling of 2026-10-23). The sound is learned
  from what the firmware sends on MIDI out and restored by sending it a MIDI program change, as a
  player would. Nothing is read from the ROM, and no RAM address is interpreted.
- **JUCE in the emulation.** `src/app` and below stay JUCE-free and OS-free.
- **Losing a project's memory.** An instance that cannot run (ROMs missing, wrong folder) still
  hands back, unchanged, the memory and sound it was given, so saving the project does not wipe them.
- **A save that makes a gap.** Saving the project while it plays must not stop the audio.
- **Chaining short boots.** The firmware reinitializes its RAM after about six short power-ons
  (K11, T8-2). A project saved during the boot saves the image it booted from, never a half-booted
  one, so reopening a project again and again cannot set this off.

## 6. The parts

### 6.1 The engine: the sound, the analogs and the notes (goals 1, 3, 4)

All in `ens::app::Engine` (`src/app/engine.*`), JUCE-free, on the audio side unless said.

- **Tracking the sound.** A small MIDI parser (running status, real-time bytes ignored) reads two
  streams:
  - the firmware's MIDI out, which the engine already drains every block (A2). After each sound
    picked on the panel the firmware sends its program change (`docs/ks32-hardware-interface.md`,
    "MIDI out": Up/Yes twice from ROM 00 sends `01`, then `02`);
  - the MIDI in bytes from the host, before they go to the machine (a host program change selects
    the sound, "Prog Change= ON" by default, and the firmware does not echo it, "MIDI Loop = OFF").

  The engine keeps the last program change seen, with its channel, plus whatever the hunt (6.2)
  shows the firmware sends with it for the bank. Bytes the firmware sends during the boot are not
  tracked: the boot always ends `C0 00` (K11), which is the power-on sound, not the user's.
- **Selecting a sound.** `setProgram(n)` (audio thread, called by the processor each block with the
  Sound parameter) sends a program change into MIDI in at the start of a block, before the host's
  bytes. *My call* on the rule: edge-triggered, not "whenever they differ", so a panel pick does not
  race the parameter that has not caught up with it yet. The engine keeps the current sound (the
  boot's end sets it to 0, ROM 00; a tracked program change sets it) and the last requested one. A
  request that changes is sent once, and only if it differs from the current sound; the boot's end
  re-checks the waiting request. Before the boot ends nothing is sent, and the request waits.
- **The current sound is published** in the snapshot (`program`, -1 until the boot ends), so the
  processor can move the Sound parameter when the panel or MIDI changes it.
- **Analog values from the audio side.** The processor applies the four analog parameters each block
  through an audio-thread call (*my call*: `setAnalog(Analog, raw)` on the engine, applied at the
  block start; the GUI's `post` path stays for buttons and keys). They are applied only once booted:
  the firmware takes the pitch wheel's power-on value as its centre (K13), so a project saved with
  the wheel off centre must not boot with it there. Until then the inputs stay at the K13 rest.
- **Notes from MIDI in.** The MIDI-in parser also keeps a 128-bit mask of notes held (note on with
  velocity > 0 sets, note off or velocity 0 clears, CC 120 and 123 clear all, any channel), published
  in the snapshot. A new engine starts with an empty mask.
- **Real time.** All of this is fixed-size state; nothing new allocates (section 7).

### 6.2 The hunt: which sounds a program change can bring back (goal 1)

Run the firmware and watch it, the Plan 6 way, as one bounded task with a fresh RAM image (never
the user's memory file). For each kind of selection: a ROM sound (ROM 01, ROM 55), an INT sound
(BankSet, then INT 34) and a preset (Select Seq/Preset, Loc=30):

1. record what the firmware sends on MIDI out when it is selected;
2. save the RAM, boot a new engine from it, send back what was recorded, and read the display.

*My call on the outcome:*
- a kind whose display comes back the same is supported, and the engine replays exactly the bytes it
  recorded (program change and any bank select before it);
- a kind that does not come back is not worked around: it is written in the interface doc, the
  decisions log and the readme as "comes back on ROM 00" (or whatever it shows), and the plan goes
  on. The Sound parameter covers program numbers 0-127 whatever the hunt finds.

The results go into `docs/ks32-hardware-interface.md` ("MIDI out", "Battery RAM") as facts, and the
experiment's harness is not committed.

Known limits, documented and not worked around: if the user sets "Prog Change= OFF" on System/MIDI
Screen 7, the firmware ignores the restore and the Sound parameter. In MULTI mode the program change
is sent on the channel it was seen on.

### 6.3 Parameters and automation (goal 3)

Five host parameters, through `juce::AudioProcessorValueTreeState` (*my call*):

| ID | Name | Range | Default | Reaches |
|---|---|---|---|---|
| `volume` | Volume | 0-1 | 1 (top) | analog Volume |
| `dataEntry` | Data Entry | 0-1 | the K13 rest | analog Data Entry |
| `pitchWheel` | Pitch Wheel | 0-1 | 0.5 (centre) | analog Pitch wheel |
| `modWheel` | Mod Wheel | 0-1 | 0 (bottom) | analog Mod wheel |
| `sound` | Sound | integer 0-127 | 0 | program change (6.1) |

- **A parameter's value is the control's position** as the editor draws it (0 bottom, 1 top). The
  position-to-raw mapping the editor uses today, including the wheels' inversion, moves to one
  shared function. The defaults are the K13 rest values, so a new instance sounds as Plan 7's does.
- **The editor drives parameters, not the engine.** A drag on a slider or wheel sets the parameter
  (with begin/end gestures, so the host records automation); mouse-up on the pitch wheel sets it
  back to centre. The editor draws each analog from its parameter. That also fixes the Plan 7 open
  item where a reopened VST3 editor drew the analogs at rest and the next drag jumped.
- **Sound, both ways.** The processor sends the Sound value to the engine every block (6.1). When the
  engine's published sound changes because of the panel or MIDI in, the processor sets the parameter
  to it on the message thread, so the host's Sound value follows. *My call:* a timer at about 30 Hz
  on the processor does this; the 60 s memory save moves to a counter on the same timer.
- If the host's automation is in read mode, a Sound lane overrides panel picks. That is how
  automation works in every host, and it is not worked around.

### 6.4 Plugin state (goal 1)

- **What is saved** (*my call* on the format): JUCE XML through `copyXmlToBinary`: the parameter
  tree (which carries Sound and the four analogs), a format version attribute, the bank and program
  bytes to replay (6.2), which decide the Sound parameter on restore, and the RAM as a base64 child (about 87 KB). In the Standalone the RAM is
  left out (goal 5).
- **Which RAM.** Once booted, the running machine's RAM. Before the boot ends, the image the machine
  was started from (anti-goals: chaining short boots). With no machine at all, the image the
  instance was given, unchanged (anti-goals: losing a project's memory).
- **Taking the RAM without a gap.** The audio thread copies the RAM when asked (`requestRam` /
  `takeRam`, Plan 7). `getStateInformation` asks and waits briefly for the copy; *my call* on the
  fallback: if no block runs in that time (the host is not calling `processBlock`, so the machine is
  not running either), it reads the machine directly under `suspendProcessing`. It never waits on
  the audio thread itself.
- **Restoring** (`setStateInformation`): the parameters are replaced, and in the VST3 a RAM image
  of the right size restarts the machine from it (the existing restart path, a warm boot), after
  which 6.1 selects the saved sound. A state with no RAM, a wrong size or an unknown version keeps
  the machine as it is and restores only what it can (*my call*). With no ROMs the image is kept and
  used when the ROMs become good, instead of the shared file.
- **Order of events in a host.** The constructor still starts the machine from the shared file;
  a project's `setStateInformation` that follows restarts it from the project's RAM. The first boot
  is thrown away; *my call*: that costs only time, and it keeps "new instance = shared file" simple.

### 6.5 The memory file, per format (goal 2, the user's rule)

| | Standalone | VST3 |
|---|---|---|
| Start | from the memory file | from the project's RAM if restored, else from the memory file |
| Saves on close and every 60 s | yes, as today (A11) | **no** |
| Sound and analogs on restart | from the JUCE wrapper's saved state (goal 5) | from the project |
| Erase memory… | zeros to the file, then a cold boot (as today) | **this instance only**: a cold boot from zeros; the file is left alone |
| Change… (memory file) | as today | picks the shared file and restarts this instance from it (as today) |
| Copy to shared memory… | not shown | **new**: asks first, then writes this instance's RAM to the shared file (temporary file, then replace, as A11); only once booted |

- The settings page's memory section says, in the VST3, that the instance's memory is kept in the
  project and new instances start from the shared file.
- *My call:* the Standalone's wrapper state (JUCE keeps it in `%APPDATA%\KS-32\KS-32.settings`) holds
  only the parameters and the sound, so the memory file stays its only memory. The builder checks in
  the JUCE source that the wrapper saves and reloads the processor state as expected.

### 6.6 The editor (goals 3 and 4)

- Sliders and wheels follow their parameters (6.3), including host automation, at the editor's
  existing redraw rate.
- Keys held by MIDI in (the snapshot's mask) are drawn pressed, as a mouse-held key is (*my call*:
  the same look; the user can ask for a different shade).
- The settings page changes in 6.5.

### 6.7 Documents

- `docs/ks32app.md`: plugin state, the memory table of 6.5, the parameters, the hunt's limits, many
  instances and CPU, and the "powers on to ROM 00" paragraph rewritten (the app now selects the
  saved sound after the boot; the display shows ROM 00 for the boot first).
- `docs/decisions.md`: new A-entries for the program tracking and restore rule, the parameters, the
  state format and contents, the per-format memory rules, the Standalone state, and the MIDI note
  mask.
- `docs/ks32-hardware-interface.md`: the hunt's facts.
- `docs/verification-plan8.md`: the closing check, with manual rows for the user (FL Studio or
  Cakewalk: save and reopen, two instances, a new instance, automation lanes, keys from the track).

## 7. Real-time rules

As in the app design section 7. In particular: the MIDI parsers, the program tracking, `setProgram`,
the audio-side `setAnalog` and the note mask are fixed-size and run in `render` without allocating,
locking or waiting; `ens_rt_tests` covers a block that tracks and sends a program change and sets
the analogs. Parameter values cross to the audio thread as JUCE's atomic raw values. The snapshot's
new fields go through the existing published copy.

## 8. Decisions this plan expects to log

A15 on, as listed in 6.7, plus the hunt's outcome (which selections come back).

## 9. Left to the builder

- The parser's shape and names, and how the bank bytes are stored once the hunt has spoken.
- The wait in `getStateInformation` (a few hundred ms is the intent).
- The exact wording on the settings page and in the readme, in the existing style.
- Test names, beyond the coverage in section 3.
