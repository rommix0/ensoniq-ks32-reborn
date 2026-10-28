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
- `kBootMs` = 2900, which is 1917 ms plus 50 %, rounded up.

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
- There are 15 LEDs, numbered 0x00–0x0E. Which LED is which control is Task 5's, since it needs
  the button codes.
- There is no start-up handshake. The firmware never waits for or reads a reply from the panel
  at start-up.
- Bytes 0x00–0x7F are characters. Bytes 0x80 and up are commands, some with parameter bytes
  (table below).
- **Steady state at `kBootMs`:** the low-battery warning, row 0 `WARNING! Battery` and row 1
  `low. See manual.` (the service manual's wording, PDF p12). No LED command has been sent, so
  every LED is off, and nothing is marked blinking. The warning waits for a button ("Press any
  button to continue"); it is not timed, and 10 s of boot send nothing after it. Task 6 removes
  it with the battery input. `ks32_firmware_boot_shows_play_screen` pins this state.

**Command table.** "Effect" is what `Panel` does. "None" means the panel takes the parameter
bytes and changes nothing it models.

| Bytes | Effect | Evidence | Confidence |
|---|---|---|---|
| 0x00–0x7F | A character at the cursor; the cursor moves on, from column 15 of row 0 to row 1 | The boot sends " ENSONIQ KS-32  " (16 bytes) and the warning (32 bytes, breaking between "Battery" and "low" exactly at 16). Codes below 0x20 come as characters too: the first play-screen draw sends 16 bytes, mostly 0x00, after `87 10`, and a later draw sends the sound name in their place | High for 0x20–0x7E. Medium that 0x00–0x1F are characters (custom glyphs?) and not commands |
| `87 pp` | Cursor to position pp | `87 10` is always followed by 16 characters that read as row 1 (" KS Grand Piano ", "STRING ENSEMBLE "). `87 00` "INT   " `87 06` "00" builds row 0. The parameters seen are 0x00–0x1D, never 0x20 or more | High |
| `8C` | Blank the display; cursor to 0 | Text follows with no `87`: the 32-character warning, and "Replace edit" (then `87 10` "sound?"). The play screen drawn after `8C` writes only columns 0–7 of row 0. Without the blanking, columns 8–15 would still read "Battery" | High for the cursor; Medium for the blanking |
| `8D nn` / `8E nn` | LED nn on / off | After a button event the firmware sends `8E nn` for every nn from 0x00 to 0x0E but one, and `8D` for that one: a full refresh with one LED lit. The parameters are only 0x00–0x0E. That is 15 LEDs, the count of LED controls in `docs/reference/ks32-front-panel.md` (Edit Sound, Select Sound, Edit Seq/Preset, Edit Track, Select Seq/Preset, Track 1–8, Replace Track Sound, System/MIDI) | High for 15 LEDs. Medium for which of the two is "on" (one `8D` against fourteen `8E` reads as one LED lit) |
| `8F nn` | LED nn blinks | Seen in two screens, on LEDs 0x0A and 0x00, mostly a few commands after `8D` on the same LED (`8D 0A` ... `8F 0A`): a third LED state. Blinking is a reading, not a proof | Low |
| `90` ... `91` | Characters written between them are marked blinking (`PanelState::blink`) | The pair brackets exactly the value field of the parameter being edited ("+00" after "Master Tune= ", "99" after "Vol=", "ON  "). The same field is redrawn elsewhere without it. Whether the panel blinks, underlines or inverts it is not known | Medium that it marks the edit field; Low that it is blinking |
| `80` | None | The first byte of all, at 549 ms, followed by a 16 ms pause before anything else. It reads as a reset | Unknown effect |
| `94 xx` | None | Sent once, as `94 FF`, right after `80`. Whether FF is its parameter or a command of its own is not settled; neither changes the model | Low |
| `93` + 9 bytes | None | Sent once in the boot. The last 8 bytes read as a 5-column bitmap of a note glyph (a stem in one column, the head at the bottom left), so it is likely a custom-character definition. Which code it defines is unknown. The bytes are taken as parameters | Low |
| `81`, `83` | None | In the boot only: `81 83` before the start-up logo, `83` after it | Unknown effect |
| `92`, `98` | None | Always the pair `92 98`, after `8C` or `87 00`, before a screen is drawn or redrawn | Unknown effect |
| `88 00` | None | Always with 00. It is a command with a parameter, not text: the redraw `87 06 88 00 "00"` writes the field the first draw wrote as `87 06 "00"`. If the 00 were a character, the two draws would differ | Medium |
| `86 00`, `89 00` | None | Always with 00; taken as parameters by analogy with `88` | Low |
| `95 03` | None | Seen twice, in "Global Bend= " `95 03` "02". Taken as a one-parameter command like `94`. If `95` took no parameter, 0x03 would be a character in column 13 of row 1 | Low |
| other 0x80–0xFF | Counted in `PanelState::unknown` | None seen | — |

Two choices are not settled by evidence and are marked `// DECISION K7`. Past position 0x1F the
cursor wraps to 0 (the firmware never writes there). Out-of-range parameters (`87` ≥ 0x20, LED
≥ 0x0F) are counted in `unknown` and ignored.

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
  the 0x80–0x83 starts do nothing. These events are evidence for Task 5. The button codes are
  not decoded here.
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

## Line-A and TRAP use

The firmware uses line-A opcodes and TRAPs as calls. Counts up to `kBootMs` (2900 ms):

- vector 10 (line A): 5236;
- TRAP #2 to #12 (vectors 34–44): 292, 10, 302, 0, 17, 2380, 436, 13, 18, 1, 69.

They are not faults:
- the boot carries on after each one;
- no fault vector is taken;
- the stack pointer at the idle STOP is the same at 2.9 s and at 10 s, so every one returns.

`ks32_firmware_boot_takes_no_fault_vectors` prints the line-A count; Plan 5 ruling R15 says it
is not asserted.
