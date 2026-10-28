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
- There is no panel model yet (Task 4), so this steady state is "waiting for the panel".
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
