# KS-32 hardware interface (found by running the OS)

The facts the emulated KS-32 (`src/ks32/machine.{h,cpp}`) relies on. Each comes from running
the user's OS ROM (v3.01) and reading the bus trace, interrupt acknowledges, exception counts
and what the firmware transmits. The firmware is run, never studied (user ruling 2026-10-23).
Each section gives the fact, how it was found, the alternatives tried, the confidence and the
decision entry in `docs/decisions.md`.

**How the evidence was gathered (Plan 6 Task 3).**
- `ks32emu boot --ms 5000 --trace unmapped,iack,duart` was run on the Task 2 machine.
- A scratch harness, not committed, was a copy of the machine with run-time switches for each
  candidate. It printed exception counts, acknowledges, unmapped accesses, RAM accesses by
  region, DUART writes, ESP halt edges, the transmitted bytes and an FNV-1a hash of the device
  trace.
- "Gets further" means: new kinds of device access (device, register, read or write), fewer
  unmapped or fault events, and no unexpected-event message ("Unexpected Event ID = n", service
  manual PDF p11) in the bytes sent on DUART channel B (the panel link).

**Where the boot ends.** With the wiring below the boot reaches a steady state.
- The last new kind of device access comes at 1066 ms.
- The last byte of the start-up text comes at 1917 ms. The text is "ENSONIQ KS-32", then
  "WARNING! Battery low. See manual.", sent on channel B. The battery warning is Task 6's analog
  inputs.
- After that the CPU idles in STOP with the interrupt mask at 0. The 1 ms tick runs, and the
  stack pointer is the same at 2.9 s and at 10 s.
- The steady state is the low-battery warning on the panel display, which waits for a button
  ("Press any button to continue", service manual PDF p12). See "Front panel: display and LEDs".
- `kBootMs` was 2900, which is 1917 ms plus 50 %, rounded up.

**Task 6 update.** With the analog inputs at their K13 resting values (battery nominal) the boot
ends on the play screen instead (see "Analog inputs"). The last panel byte is written at 2121 ms
and nothing is sent in the 10 s after it. The last new kind of device access comes at 1997 ms.
`kBootMs` = 3200, which is 2121 ms plus 50 %, rounded up.

## Memory map (K1)

**Fact.**

| Addresses | What |
|---|---|
| 0xC00000–0xC3FFFF | ROM. Upper EPROM = even bytes. Lower EPROM = odd bytes, mirrored every 0x20000 |
| 0x000000–0x03FFFF and 0xFF0000–0xFFFFFF | 64 KB of RAM. A16 and A17 are not decoded, so 0x000000–0x03FFFF holds four mirrors and 0xFF0000 is a fifth. Reads of 0–7 come from ROM 0xC00000–7 until the first write into 0–7 |
| 0x200000–0x20001F | OTIS (words) |
| 0x260000–0x2601FF | ESP (odd bytes) |
| 0x280000–0x28001F | DUART (odd bytes) |

Nothing else is touched in the boot: 0 unmapped accesses.

**How it was found.**
- Under the Task 2 map (RAM only at 0–0xFFFF and 0xFF0000), the firmware writes long zeros over
  0x000000–0x03FFFF from about cycle 45 000 to 2 405 050. That is 98 304 unmapped addresses
  (0x010000–0x03FFFE), writes only.
- Later, with a working serial clock, it makes a RAM-size probe:
  - a word write to 0x008000;
  - a word write to 0x018000;
  - a read of 0x018000, then a read of 0x008000.

  These two addresses meet exactly when the RAM is 64 KB.
- The firmware adapts to the answer:
  - with 64 KB it keeps a work area at 0x008400–0x008BFF;
  - with 128 KB or 256 KB it keeps it at 0x038400–0x038BFF.

  Each is the linear alias of 0xFF8400–0xFF8BFF under that size, the area it uses most.
- After the probe, every access lands in the map above. The boot test
  `ks32_firmware_boot_is_fully_mapped` also finds no byte access to OTIS and no even-byte access
  to the ESP or the DUART.

**Alternatives.**

| Candidate | Result |
|---|---|
| Task 2 map (nothing at 0x010000–0x03FFFF) | Rejected. The boot fails with "Unexpected Event ID = 130" (divide by zero, vector 5), and unmapped accesses run up to 0x04891C |
| 128 KB or 256 KB of RAM | Also boots, with the same outcome. Its trace differs from 64 KB only in timing; 128 KB and 256 KB give the same trace hash. The boot cannot tell them apart. 64 KB follows the service manual ("64K of RAM", PDF p4). The RAM-chip note (PDF p13, U28/U33, "pin 28 (or 32)") suggests a variant with 128K x 8 parts |
| (a) A separate RAM at 0x000000 | Same trace hash as the shared RAM, so not distinguishable. One chip pair (U28/U33) serves both windows, so they are one RAM |
| (b) The vector overlay ends at the first access outside 0–7 | Rejected. No device access at all in 5 s |

**Confidence.** High for the windows and the overlay. Medium for the RAM size (64 KB).
**MAME.** Not compared.

## Interrupts (K2)

**Fact.**
- The DUART answers the interrupt acknowledge with its IVR. The firmware writes IVR = 0x40, so
  vector 64 is taken.
- The levels are provisional: DUART on level 3, OTIS on level 1 (autovector). The boot cannot
  tell levels apart. They are settled in Task 7, when notes make OTIS interrupt.

**How it was found.**
- **Vectored versus autovector.** The boot clock test enables the C/T interrupt (IMR = 0x08)
  and waits in STOP.
  - With the IVR answer: one vector-64 interrupt; the firmware stops the counter and goes on.
  - With autovector: vector 24 + level is taken. With a serial clock, the firmware then reports
    "Unexpected Event ID = 138" (spurious interrupt, at level 1) or 139 (unused vector, at
    level 4).
- **IMR values written:** 0x08 (clock test), 0x00, 0x2B (TxRDYA, RxRDYA, C/T, RxRDYB), then 0x00
  again. IVR = 0x40 is written twice.
- **Level.**
  - DUART levels 1 to 6 give identical behaviour: the same exception counts, the same kinds of
    access and the same display text. Level 7 breaks the boot (vector 4).
  - A histogram of the interrupt mask (SR bits 10..8, sampled before every instruction, 10 s)
    shows the masks 0, 5 (6 instructions in all) and 7. Mask L also appears, but only inside
    the level-L handler.
  - The firmware idles at mask 0, so any level from 1 up is serviced. The short mask-5 section
    fits a device at level 5 or below. Nothing bounds the level further.
  - Level 3 is provisional. The evidence bounds only the DUART level (1-5) and says nothing about OTIS. DUART 3 and OTIS 1 came from the Task 3 implementer's unverified recollection of MAME's esq5505 driver (DUART on IRQ 3, OTIS on IRQ 1). MAME was not opened to check it, so the values are not confirmed by evidence or by MAME until Task 7.
- **OTIS.** OTIS never raises its IRQ during the boot (no voice ends with IRQE set). Its level
  cannot be observed until notes play.

**Alternatives.** Autovector was rejected (events 138 and 139). Levels 1 to 6 are equivalent
in the boot.

**Confidence.** High for the IVR answer. Low for both levels.
**MAME.** Not compared. The provisional levels equal the recalled MAME values, unchecked (see above).

## Clocks (K3, K16)

**Fact.**
- The CPU and OTIS run at 15 238 090 Hz, and a voice slot is 16 CPU clocks. The DUART X1 is
  4 MHz. The ESP gets 89 instruction periods per frame.
- All four DUART serial clocks are external 16x clocks at 500 kHz (16 MHz / 32, findings B):
  - TxCA = IP3 and RxCA = IP4 (MIDI, K3);
  - TxCB = IP5 and RxCB = IP2 (panel link, K16).

  That is 31 250 baud, 128 X1 clocks per bit.

**How it was found.**
- **Boot clock test** (the DUART write summary):
  - IVR = 0x40, CTUR/CTLR = 0x09/0xC4 (preload 2500), ACR = 0x30 (counter, X1/16),
    IMR = 0x08;
  - a start (read of register 14), one vector-64 interrupt, a stop (read of register 15);
  - then ACR = 0x60.

  OTIS keeps ACT = 0x1F, so `activeVoices()` is 32: the clock ratio passes the firmware's own
  test (findings B).
- **System tick:** ACR = 0x60 (timer, X1), preload 0x07D0 (2000), so one interrupt per 1 ms.
- **Serial clocks.** The firmware writes CSRA = CSRB = 0xEE: all four clocks from IP pins, at
  16x (data sheet table 4-5, PDF p33; table 4-2, PDF p29).
  - Without a clock, channel B never sends. The firmware then waits for TxRDY/TxEMT in a masked
    loop at 550 ms, after THRB = 0x80.
  - Channel A carries MIDI: control changes in running status, then pitch bend. So its rate is
    the MIDI 31 250 baud.
  - For channel B, 16x clocks of 0.5, 1 and 2 MHz all boot with the same display text; only
    the timing moves.
  - So K16 (channel B at 500 kHz, the same source as MIDI) has no evidence yet. It is revisited
    in Tasks 4–5.
  - Task 4 found a lower bound only. There is no start-up handshake, so there is no reply timing
    to measure. But while it sends the warning, the firmware writes THRB on every 1 ms tick
    without waiting for TxRDYB: at 9600 baud the trace shows ISR = 0x08 (TxRDYB clear) at
    1903.074 ms and a THRB write at 1903.121 ms all the same, which overwrites the waiting
    character. A panel character is 11 bits (MR1B = 0x13: 8 data bits, no parity; MR2B = 0x0F:
    2 stop bits), so the link must run at 11 000 baud or more (a 16x clock of 176 kHz or more).
    At 153.6 kHz (9600 baud) the warning loses characters; at 250 kHz and above the stream is the
    same as at 500 kHz.

**Confidence.** Medium for the CPU and DUART clocks (they pass the clock test, which constrains
only their ratio). High for channel A. Low for channel B.
**MAME.** Not compared.

## ESP wiring (K15)

**Fact.** DUART OP6 drives the ESP's HALT pin directly. The pin is high while OPR bit 6 is 0,
and high means halt. After reset OP6 is high, so the ESP halts at its first END. The firmware
releases it once its program is loaded.

**How it was found.**
- The first DUART write of the boot is register 15 = 0x40 (reset OPR bit 6, so OP6 is high).
  It comes just before the ESP halt-enable writes (`$1F` = 0x02, 0xFF).
- With OP6 driving HALT directly:
  - the ESP halts at reset;
  - it runs from 1036 ms on (with edges at about 900, 955 and 1036 ms);
  - the boot sends no unexpected event;
  - the ESP's serial control register is 0x63 (findings C).
- With the polarity inverted, the firmware reports "Unexpected Event ID = 33" (bad ESP chip),
  and the ESP is left halted.

**Alternatives.** The inverted polarity was rejected (event 33).
**Confidence.** High.
**MAME.** Not compared.

## RESET line (K14)

**Fact.** The OS never executes the RESET instruction during the boot: 0 calls of
`resetPeripherals()` in 10 s. The three candidates (reset every device, the DUART only, none)
give identical traces. The machine keeps "the DUART only".

**Confidence.** High that the boot does not use it. What the line resets is unobservable.

## Wait states (K9)

No evidence of wait states. No failure points to timing, and the boot clock test passes with
zero wait states.

**Confidence.** Medium.

## Unmapped accesses (K6)

With the K1 map the boot makes no unmapped access, so the "reads 0, writes ignored, traced" rule
never applies.

## Front panel: display and LEDs (K7)

**Fact.**
- The keypad/display board is on DUART channel B. The firmware sets it up as MR1B = 0x13
  (8 data bits, no parity), MR2B = 0x0F (2 stop bits) and CSRB = 0xEE (the K16 clocks), so a
  character is 11 bits.
- The display is 2 rows of 16 characters, addressed as positions 0x00–0x1F: row = position / 16,
  column = position % 16.
- There are 15 LEDs, numbered 0x00–0x0E. Which LED is which control is in "Front panel:
  buttons".
- There is no start-up handshake. The firmware never waits for or reads a reply from the panel
  at start-up.
- Bytes 0x00–0x7F are characters. Bytes 0x80 and up are commands, some with parameter bytes
  (table below).
- **Steady state at `kBootMs`** (Task 6): the play screen, row 0 `ROM   00        ` and row 1
  ` KS Grand Piano `. LED 8 (Select Sound) is on and the others are off. Nothing is marked
  blinking. `ks32_firmware_boot_shows_play_screen` pins this state.
- **Before Task 6** the battery read 0 and the boot ended on the low-battery warning, row 0
  `WARNING! Battery` and row 1 `low. See manual.` (the service manual's wording, PDF p12), with
  every LED off. The warning waits for a button ("Press any button to continue"); it is not
  timed. A low battery input still gives it, for example `ks32emu boot --analog BATTERY=480`.

**Command table.** "Effect" is what `Panel` does. "None" means the panel takes the parameter
bytes and changes nothing it models.

| Bytes | Effect | Evidence | Confidence |
|---|---|---|---|
| 0x00–0x7F | A character at the cursor; the cursor moves on, from column 15 of row 0 to row 1 | The boot sends " ENSONIQ KS-32  " (16 bytes) and the warning (32 bytes, breaking between "Battery" and "low" exactly at 16). Codes below 0x20 come as characters too: the first play-screen draw sends 16 bytes, mostly 0x00, after `87 10`, and a later draw sends the sound name in their place | High for 0x20–0x7E. Medium that 0x00–0x1F are characters (custom glyphs?) and not commands |
| `87 pp` | Cursor to position pp | `87 10` is always followed by 16 characters that read as row 1 (" KS Grand Piano ", "STRING ENSEMBLE "). `87 00` "INT   " `87 06` "00" builds row 0. The boot sends 0x00–0x1D only. After button presses (Task 5) the firmware also sends `87 20` with no text after it (`87 00 92 98 87 20`, around the "Store Sequences" and "Analog Tests" pages): it reads as parking the cursor off the display. It is counted in `unknown` | High for 0x00–0x1F; Low for 0x20 |
| `8C` | Blank the display; cursor to 0 | Text follows with no `87`: the 32-character warning, and "Replace edit" (then `87 10` "sound?"). The play screen drawn after `8C` writes only columns 0–7 of row 0. Without the blanking, columns 8–15 would still read "Battery" | High for the cursor; Medium for the blanking |
| `8D nn` / `8E nn` | LED nn on / off | After a button event the firmware sends `8E nn` for every nn from 0x00 to 0x0E but one, and `8D` for that one: a full refresh with one LED lit. The parameters are only 0x00–0x0E. That is 15 LEDs, the count of LED controls in `docs/reference/ks32-front-panel.md` (Edit Sound, Select Sound, Edit Seq/Preset, Edit Track, Select Seq/Preset, Track 1–8, Replace Track Sound, System/MIDI) | High for 15 LEDs. Medium for which of the two is "on" (one `8D` against fourteen `8E` reads as one LED lit) |
| `8F nn` | LED nn blinks | Seen in two screens, on LEDs 0x0A and 0x00, mostly a few commands after `8D` on the same LED (`8D 0A` ... `8F 0A`): a third LED state. Blinking is a reading, not a proof | Low |
| `90` ... `91` | Characters written between them are marked blinking (`PanelState::blink`) | The pair brackets exactly the value field of the parameter being edited ("+00" after "Master Tune= ", "99" after "Vol=", "ON  "). The same field is redrawn elsewhere without it. Whether the panel blinks, underlines or inverts it is not known | Medium that it marks the edit field; Low that it is blinking |
| `80` | None | The first byte of all, at 549 ms, followed by a 16 ms pause before anything else. It reads as a reset | Unknown effect |
| `94 xx` | None | Sent once, as `94 FF`, right after `80`. Whether FF is its parameter or a command of its own is not settled; neither changes the model | Low |
| `93` + 9 bytes | None | Sent once in the boot. The last 8 bytes read as a 5-column bitmap of a note glyph (a stem in one column, the head at the bottom left), so it is likely a custom-character definition. Which code it defines is unknown. The bytes are taken as parameters | Low |
| `81`, `83` | None | In the boot only: `81 83` before the start-up logo, `83` after it | Unknown effect |
| `92`, `98` | None | Always the pair `92 98`, after `8C` or `87 00`, before a screen is drawn or redrawn | Unknown effect |
| `88 nn` / `89 nn` | Save the cursor in slot nn / move it back there (Task 5) | Seen with nn = 00 and 01. A value redrawn after Up/Yes comes as `89 00 88 00 90 "02" 91`, with no `87`: "Location = " `87 1B 88 00 90 "01" 91`, then `89 00 88 00 90 "02" 91`; "Time Signature =" ... `87 10 88 00 90 "04" 91`, then `89 00 88 00 90 "05" 91`. Without the restore "02" lands after "01" ("Location = 0102") and the Transpose page writes "+0" over "PRESET" in row 0. The name page uses slot 01: `87 10 88 01` "Sequence-01     " `89 01` "S" | Medium. Slots 02 and up are not seen and are counted in `unknown` |
| `85 pp` | None (Task 5) | Seen once, as `89 01 "S" 85 10 86 01` on the "New name =" page, where the first letter of the name is being edited: pp = 0x10 is that letter's position. Read as a one-parameter command (a cursor or underline at pp?). If 0x10 were text, a character 0x10 would land in row 0 | Low |
| `86 nn` | None | `86 00` and `86 01` (after `85 10` on the name page): an on/off parameter? | Low |
| `95 03` | None | Seen twice, in "Global Bend= " `95 03` "02". Taken as a one-parameter command like `94`. If `95` took no parameter, 0x03 would be a character in column 13 of row 1 | Low |
| other 0x80–0xFF | Counted in `PanelState::unknown` | None seen | — |

Two choices are not settled by evidence and are marked `// DECISION K7`. Past position 0x1F the
cursor wraps to 0 (the firmware never writes there). Out-of-range parameters (`87` ≥ 0x20, LED
≥ 0x0F, `88`/`89` slot ≥ 2) are counted in `unknown` and ignored.

**How it was found.** Scratch runs printed channel B's bytes with their times and the
channel-B DUART accesses. The firmware was run, never studied.
- **Start-up sequence.**
  - From 549 ms to 603 ms the firmware sends each byte on its own: CRB = 0x04 (enable the
    transmitter), the THRB write, SRB polled until 0x0C (TxRDY and TxEMT), then CRB = 0x08.
  - At 598 ms it sets channel B up again (CRB 0x20, 0x30, 0x40, 0x50, 0x10, MR 0x13 0x0F,
    CSRB 0xEE, CRB 0x05), then sends `93` with its 9 bytes, `81` and `83`.
  - At 603 ms IMR = 0x2B enables RxRDYB, and from then on the 1 ms tick sends at most one byte
    per tick.
  - The logo goes out at one character per 50 ms (604–1608 ms), then `83` at 1658 ms. `8C` and
    the warning follow from 1884 ms at one character per ms; the last byte is at 1916 ms.
- **No handshake.**
  - Before 603 ms the firmware reads SRB only while it waits for TxEMT, and it never reads RHRB.
  - A byte injected at 549.5 ms (0x00, 0x80 or 0xFF) stays in the FIFO, which shows in the SRB
    values (0x05, 0x0D), and is ignored. The receiver reset at 598 ms discards it. The stream is
    unchanged, bytes and times.
  - Each of the 256 single bytes injected at 700 ms or at 2000 ms is read by the RxRDYB
    interrupt (ISR 0x20, then SRB, then RHRB) and changes nothing that is sent.
  - A count of executed instructions after each single byte at 2000 ms falls into three classes:
    0x00, 0x01–0x7F and 0x80–0xFF. The receive path stores a byte without acting on it alone.
- **Screens for the command table.** The boot alone uses only `80`, `94`, `93`, `81`, `83`,
  `8C` and text. The other commands were seen in scratch runs that dismissed the warning with a
  two-byte event: one of most bytes from 0x84 to 0xFF, then 0x00, on RxB. Single bytes and
  the 0x80–0x83 starts do nothing. Task 5 decodes these events as button presses ("Front panel:
  buttons").
- **The `ks32emu boot --display-only` view of the boot** (rows printed on each change, trimmed):

  ```
  861 | E              |
  ...
  1508 | ENSONIQ KS-32  |
  1884 |                |
  1885 |W               |
  ...
  1916 |WARNING! Battery|
  1916 |low. See manual.|
  ```

  Where the logo lands depends on the cursor after `80`...`83`, whose effect is unknown. The
  model puts it on row 0. The steady state does not depend on it, because `8C` blanks and homes.

**Confidence.** High for the channel, the 2 x 16 geometry, positioning, text and the 15 LED
numbers. High that there is no handshake. Medium to Low for the rest, per row above.
**MAME.** Not compared.

## Front panel: buttons (K7)

**Fact.**
- **Byte forms.** The panel sends a button event as two bytes on RxDB (channel B):
  - a press is `code | 0x80`, then `00`;
  - a release is `code`, then `00`.

  The controls found have codes 0x04–0x39. A single byte does nothing; the `00` completes the
  event.
- **Held.** A button is held from its press to its release, and the firmware tracks it:
  - Holding Down/No repeats it: ROM 55 goes to 52 in 1 s and to 47 in 2 s. `0C 00` stops the
    repeat, but `0C` alone does not, and a second `8C 00` counts as a new press.
  - Holding Bank 9 and pressing Screen 1, 9 or 0 opens the service screens: the version, the
    reinitialize prompt and Analog Tests (service manual PDF p4, p5 and p13). If Bank 9 is
    pressed and released first, Screen 1 does nothing on the "Store Sequences" page.
  - The firmware sees both bytes of a quick tap. A press and a release queued in the same slot
    reach it at the line rate. They dismiss the battery warning to the same screen and LEDs as a
    100 ms press. Since Task 6 the boot ends on the play screen, so the test taps Up/Yes instead
    (ROM 00 to ROM 01), with the same result.
- **The mode button is released first.** The manual says "Press Edit Seq/Preset. While holding
  down Bank 9, press Screen 1".
  - The combination works only when Edit Seq/Preset is released before Bank 9 is pressed.
  - Holding Edit Seq/Preset through the combination gives the Locate page, at every spacing
    tried (100–500 ms).
  - So `ks32emu boot --press 3000:SELECT_SOUND --press 3500:EDIT_SEQ_PRESET --press
    3900:BANK9+SCREEN1` shows "ENSONIQ KS-32 / OS Version 3.01", and
    `--press 3500:EDIT_SEQ_PRESET+BANK9+SCREEN1` does not.
- **Event 133** ("unknown button event", PDF p11) was never raised by any byte form tried.

**Codes.** `Button` (`src/ks32/panel.h`) has these 43 codes.
- "Code" is the 7-bit code; the press byte is code | 0x80.
- Each name follows the function the firmware shows for that code, checked against service
  manual Figure 2 (PDF p4) and `docs/reference/ks32-front-panel.md`.

| Control | Code | Evidence | Confidence |
|---|---|---|---|
| Bank 0–9 | 25 15 05 37 07 0F 17 27 1F 2F | In Select Sound, from ROM 55, Bank n gives ROM n5 (DRUM for 8 and 9). In Edit Sound mode Bank 0–9 open the lower label row: wave, Oct/Semi/Fine, Env1 levels, LFO, filters, Env2, a Levl/Time page (Amp), Vol/Boost (Output), effect and "Store Sounds". In Edit Seq/Preset mode they open the upper row: Locate, Loop/Countoff (Control), Click, Song, "Create SEQ/Pset", "Event Edit Track", "Quantize Track", the Mute status, effect and "Store Sequences" | High |
| Screen 0–9 | 24 14 04 36 06 0E 16 26 1E 2E | From ROM 55, Screen n gives ROM 5n. In Edit Track mode they open the pages named under the buttons: Vol/Pan, Output, KeyRange, Transpose, Chan/Prog, Status (MIDI), Pressure, SustainPedal, Timbre and Release | High |
| Track 1–8 | 10 20 18 28 11 21 08 30 | "Trk01" … "Trk08" over the track's sound. LED 1 lights, with one track LED: 2, 3, 4, 5, 10, 11, 12, 13 in track order. In Edit Track mode Track 2 switches the page to "Trk2" and lights LED 3 | High |
| Up/Yes | 0D | ROM 55 → 56; "Location = 01" → 02; time signature 04 → 05 | High |
| Down/No | 0C | ROM 55 → 54, repeating while held. It leaves the reinitialize prompt without erasing, to "Store Sequences" | High |
| Left / Right | 1C / 1D | On Edit Sound and Edit Seq/Preset pages they move the edit field (the `90`…`91` mark) in opposite directions and run on into the next pages. 1D goes Oct/Semi/Fine → Glide → Keyboard Pitch Tracking, and Locate → Auto Punch/Tempo. 1C, pressed three times from Oct/Semi/Fine, ends on the Env1/LFO/Mod page. In Select Sound mode each switches between INT and ROM. Calling 1C Left rests only on 1D stepping forward through the pages | Medium |
| Compare | 2D | On an Edit Sound page the values change (Fine "+00" → "00") and the Edit Sound LED blinks. It reaches the edit pages as Edit Sound does | Medium |
| Enter/Save | 2C | "Replace edit / sound?", as in the manual's write test (PDF p20); "Command successful!" after creating a sequence | High |
| Edit Sound | 35 | LED 0; the Bank buttons open the lower label row (above) | High |
| BankSet | 39 | In Select Sound it switches between ROM and INT (there is no card). On edit pages it does nothing, whereas 1C and 1D move the field there | Medium |
| Select Sound | 34 | From edit pages, back to the sound screen ("ROM 00 KS Grand Piano") with LED 8 | High |
| Edit Seq/Preset | 38 | LED 9; the Locate page ("SEQ 00 STOP / Bar=001 Beat=01"). The Bank buttons open the upper label row, and Bank 9 held with a Screen button opens the service screens | High |
| Edit Track | 1A | LEDs 6 and 9; "PRESET 00 Trk1 - / Vol=99 Pan=SOUND". The Screen buttons open the track pages | High |
| Select Seq/Preset | 19 | LED 1; "PRESET 00 Loc=00"; Bank n gives Loc=n0 | High |
| Replace Track Sound | 2A | LED 14 blinks over "Trk01 INT 00". It acts on the release: a release alone (`2A 00`) shows the page, and the press runs no more instructions than an unused code | Medium |
| Make Default Preset | 32 | From Select Sound: "Trk01" over the sound, LEDs 1, 2 and 10 (blinking), at the next location (Bank n then gives "PRESET 01 Loc=n1"). From Edit Seq/Preset: "Copy FROM Preset", its second function (Clone Preset) | Medium |
| System/MIDI | 0A | LED 7; "Master Tune= +00 / Global Bend= 02". Screen 1 then shows "Touch=… / Pressure=", as in the manual (PDF p20: System•MIDI, Screen 1, "Pressure=") | High |

**LEDs.** All 15 LED controls of the inventory are accounted for:

| LED | Control |
|---|---|
| 0 | Edit Sound |
| 1 | Select Seq/Preset |
| 2–5 | Track 1–4 |
| 6 | Edit Track |
| 7 | System/MIDI |
| 8 | Select Sound |
| 9 | Edit Seq/Preset |
| 10–13 | Track 5–8 |
| 14 | Replace Track Sound |

Confidence: High for the tracks and for LEDs 0, 1, 7, 8 and 9; Medium for 6 and 14.

**Open.**
- **Record, Stop and Play are not found.**
  - Codes 0x00–0x03, 09, 0B, 12, 13, 1B, 22, 23, 29, 2B, 31, 33, 3A and 3B show nothing on any
    screen tried. The screens were the sound screen, Edit Sound and Edit Seq/Preset pages,
    Select Seq/Preset, and the Locate page of a newly created sequence.
  - The instructions run in the 150 ms after each press, on the Edit Seq/Preset page, set a few
    codes apart from the rest. The baseline, code 0x00, is about 89 190:
    - 09 and 29 run about 3 500 more. They also resend the LEDs and redraw the Locate page
      unchanged.
    - 31 runs about 140 more.
    - 02 and 3A run about 500 more.
    - 03 and 3B run about 18 more.
  - Guess: 09 and 29 behave like a transport button acting on an empty sequence. The sequencer
    may also need a clock the machine does not give it yet (OTIS interrupts, Task 7).
  - These codes are not in `Button`.
- **Bytes 0xBC–0xFF** followed by `00` act in runs like the bank and screen codes:
  - 0xBC–0xC4 act like Bank 1, 0xC5–0xD1 like Bank 2, and so on;
  - 0xF0–0xF1 act like Screen 0, and so on up to 0xFD, which acts like Screen 9;
  - 0xFE does nothing, and 0xFF acts like Select Seq/Preset.

  They are not separate controls and are not in `Button`.
- **A second byte other than `00`** after a press byte does nothing. Task 4's sweeps covered
  press bytes 0x80, 0x81, 0xC0 and 0xFF. Here, every second byte 0x01–0xFF was sent after each
  code that does nothing, from the sound screen.
- **The sliders and wheels** are in "Analog inputs".

**How it was found.** A scratch harness, not committed, booted the machine and put bytes on RxB
2 ms apart. It printed the display rows and LEDs, the bytes sent and instruction counts. The
firmware was run, never studied.
- Every press code 0x80–0xFF was tapped (pressed and released) from five places: the sound
  screen, ROM 55, the Edit Sound Pitch page, the Edit Seq/Preset Locate page and Select
  Seq/Preset.
- Each mode button was followed by each Bank button, and Edit Track by each Screen button.
- Every release code 0x01–0x7F, with its `00`, was sent alone from the sound screen. Only 0x2A
  (Replace Track Sound) shows anything.
- The holds were timed: Down/No held for 1 s and 2 s, with each release form; Bank 9 held and
  tapped; Edit Seq/Preset held and tapped.

**Tests.**
- The tests are panel_button_bytes, panel_button_table, ks32_firmware_version_screen,
  ks32_firmware_release_ends_the_hold, ks32_firmware_reinit_prompt,
  ks32_firmware_analog_test_page and ks32_firmware_quick_tap_is_seen.
- Without the release bytes the four service-screen tests fail: Select Sound and Edit
  Seq/Preset stay held and block the combination.

**K16.** The buttons add no bound: four bytes sent back to back are all read at 500 kHz.

**MAME.** Not compared.
- The release form (`code`, then `00`) matches the Task 5 implementer's recollection of MAME's
  esq5505 driver: press `code | 0x80, 00`, release `code & 0x7F, 00`.
- That recollection was not checked against MAME. The evidence above settles the form.

## Analog inputs (K8, K13)

**Fact.**
- The analog inputs reach the OTIS POT pin through a multiplexer selected by DUART OP3..OP0.
  The pin feeds OTIS's 10-bit converter, read as PAR (page 64 and up, register 9, value in
  [15:6]).
- Every 4 ms the firmware does three things:
  - it reads PAR;
  - it writes OPR set = 0x0F, which drives OP3..OP0 low;
  - it writes OPR reset = n, so that OP3..OP0 = n.

  The next read, 4 ms later, takes input n.
- No other OPR write touches OP3..OP0. The others drive OP6 (ESPHALT, K15) and OP7 (pulsed at
  565 and 598 ms, around the panel set-up).
- **The select table.** It was found by setting one select to 0x3FF or 0, with the others at
  0x100, and watching which Analog Test reading moved. "Screen" is the Analog Test screen that
  shows the reading. The last column holds with the reference at or below 0x180.

  | Select | Input | Reading (screen) | Raw to reading |
  |---|---|---|---|
  | 0 | Pitch wheel | PW= (0) | Inverted, centred on its value at power-on: 64 there, 0 from about 0x140 above it, 127 from about 0x140 below it |
  | 1 | Pressure | Pressure= (4) | 0 up to 0x200, 50 at 0x280, 101 at 0x300, 127 from 0x380 |
  | 2 | Mod wheel | Mod= (0) | Inverted: 127 up to about 0x30, 86 at 0x100, 36 at 0x200, 0 from 0x300 |
  | 3 | Data entry slider | Knob= (2) | 0 at 0, about 0.35 per raw step, 255 from about 0x2E0. Steps of 0x10 or less do not move the display |
  | 4 | CV pedal | CV= (1) | 127 below about 0xA0 (unplugged), 0 at 0xC0-0x100, 40 at 0x180, 82 at 0x200, 127 from 0x300 |
  | 5 | Volume slider | Vol= (1) | 0 at 0, about 0.17 per raw step, 127 from about 0x2E8 |
  | 6 | Battery | Bat= (2) | raw x 255 / 1023: 125 at 0x1F8, 165 at 0x298, 205 at 0x338 |
  | 7-14 | None | None | Never selected |
  | 15 | Reference | Reference= (3) | raw / 4 |

- **The reference.** Above 0x180, the firmware subtracts (reference - 0x180) from selects 1-6
  before scaling. Bat= then wraps below 0. At or below 0x180 it subtracts nothing.
- **When each input is read.**
  - Boot: the reference at 633 ms, then the pitch wheel 8 times from 955 ms, between two reads
    of the reference. Then the cycle 0 2 3 1 0 5 4 (28 ms).
  - The battery is read once in the boot, at 1657 ms. Below a reading of about 125 the warning
    follows at 1884 ms: raw 0x1E0 warns, 0x200 does not.
  - Play screen: the same cycle, with the reference now and then.
  - Analog Test page: 0 5 4 F 6 0 2 3 1 (36 ms).
- **Smoothing.** The firmware filters the readings. After a step, the battery reading takes about
  1 s to settle, and the display is redrawn about every 120 ms.
- **Pitch-wheel centre.** The centre is the wheel's value at power-on. Booted at 0x100, PW= reads
  64 at 0x100; booted at 0x300, it reads 64 at 0x300.
- **Ranges.** `Machine::setAnalog(Analog, raw)` takes the raw conversion, 0..0x3FF (masked).
- **Slider ends.** They match the manual's table (PDF p20). Its text extraction is garbled, so
  the page was rendered as an image. Vol= reads 0 at raw 0 and 127 at 0x3FF. Knob= reads 0 at
  raw 0 and 255 at 0x3FF.
- **Not part of the path.** The DUART input port (register 13) is read regularly (58 times in the
  boot) and reads 0xFF. No reading follows it.

**Resting values (K13).** `kAnalogRest` (`src/ks32/machine.h`) holds them. The reference's value
is `kReferenceRest` (`src/ks32/machine.cpp`).

| Input | Raw | Reading | Basis |
|---|---|---|---|
| Pitch wheel | 0x200 | PW=064 | Centred: any value reads 64 when it is the power-on value. 0x200 is the converter's middle and leaves room both ways |
| Mod wheel | 0x3FF | Mod=000 | Mod wheel down. Mod= is 0 from 0x300 |
| CV pedal | 0x000 | CV=127 | Unplugged: "Unplug the CV pedal and verify that CV=127" (PDF p20). Raws below about 0xA0 read 127, apart from the pedal's travel |
| Volume | 0x3FF | Vol=127 | Maximum, so headless renders are audible |
| Data entry | 0x16E | Knob=127 | Mid of 0-255 |
| Pressure | 0x000 | Pressure=000 | No key pressed (PDF p21) |
| Battery | 0x298 | Bat=164 | Middle of 125-205 (PDF p13). Set after the boot, 0x298 settles at 165. From power-on the filter stops at 164 |
| Reference (select 15) | 0x180 | Reference=095 | Every value from 0 to 0x180 gives the same readings (0, 0x100, 0x170, 0x17C and 0x180 tried). 0x180 is the highest of them. The manual gives no Reference= value |

**How it was found.** Scratch harnesses, not committed, did the following:
- traced the OTIS and DUART accesses on the Analog Test page (PAR reads and OPR writes, with
  times);
- listed the select sequence in the boot, on the play screen and on the page;
- made each select drive PAR at once (K8);
- moved one select at a time: to 0x3FF and to 0 for the table, then up and down in 0x40 steps;
- read settled displays (2.5 s each) on a grid of reference values (0x080-0x3FF) by common raw
  values (0-0x3FF);
- booted with the battery from 0x100 to 0x3E0 in 0x20 steps and recorded whether the warning
  appeared;
- booted with the pitch wheel at 0x100, 0x200 and 0x300.

**Alternatives.** The DUART input port reads 0xFF throughout and no reading follows it. No other
device access and no unmapped access appears on the page.

**Conversion timing (K8).** The datasheet's 1.6 ms conversion is not modelled: PAR follows the
selected input at once. The firmware reads 4 ms after each select, so the timing cannot show.

**Confidence.** High for the path and the select table. Medium for the raw-to-reading mappings,
which were read off settled displays. Low for the reference's resting value.

**Tests.** otis_par_reads_pot_value, ks32_firmware_cold_boot_is_clean,
ks32_firmware_analog_resting_values, ks32_firmware_slider_extremes and
ks32_firmware_boot_shows_play_screen.

**MAME.** Not compared. The Task 6 implementer recalls, without checking, that MAME's esq5505
driver feeds the OTIS pot input from a table indexed by DUART output bits. The evidence above
settles the path.

## Line-A and TRAP use

The firmware uses line-A opcodes and TRAPs as calls. Counts up to 2900 ms of Task 3's boot, which
ended on the battery warning:

- vector 10 (line A): 5236;
- TRAP #2 to #12 (vectors 34–44): 292, 10, 302, 0, 17, 2380, 436, 13, 18, 1, 69.

They are not faults:
- the boot carries on after each one;
- no fault vector is taken;
- the stack pointer at the idle STOP is the same at 2.9 s and at 10 s, so every one returns.

`ks32_firmware_boot_takes_no_fault_vectors` prints the line-A count; Plan 5 ruling R15 says it
is not asserted.
