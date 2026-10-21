# KS-32 effect parameters, output fade and FC table

Date: 2026-10-20. Firmware: KS-32 OS v3.01 (`ks32v301upper.bin` = even bytes,
`ks32v301lower.bin` = odd bytes, merged at 0xC00000). All addresses are CPU
addresses in the merged image. Builds on `findings.md` (2b) and
`data/esp_init_sequence.json`.

Regenerate with (from a checkout that has the ROMs in `ks-32/`):

```
python -I docs/rom-investigation/tools/extract_effect_params.py [rom_dir] [--out FILE]
```

It writes `data/esp/effect_defaults.json` and prints the FC check, both level
tables, every effect's parameter pages and the full list of ESP GPR writes per
effect.

| Item | Result | Confidence |
|---|---|---|
| FC table 0xC09F64 | equals `round(1024·2^(i/768))` for all 768 entries | High |
| Mix level table 0xC0A72C | 128 words, `round(32767·sin(i·π/254))`; GPR pair = (table[n], table[127−n]) | High |
| Mix indices | FX1 → 7C/7D, FX2 → 7A/7B from effect parameter bytes +0x10/+0x12; 76/77 from $DE8A (an effect parameter for 2 of 13 effects) | High |
| Default index | the per-effect default parameters at 0xC1FB8A (record bytes +6..+15) | Medium-high |
| Fade target | GPR 7F = word[0xC0A62C + 2·$DD72] << 8, $DD72 = master volume slider (0..127) | High |
| Fade timing | 111 GPR writes of +0x012F00; about 666 frames (22 ms) | Low-medium |
| Other GPRs | the parameter handlers write 6–18 more GPRs per effect; all are in `effect_defaults.json` | High (method), medium (interpreter) |
| Effect names | not found; parameter names identify the kind of each effect | — |

---

## 1. FC table (Step 1)

The 768 words at 0xC09F64 run from 0x400 to 0x7FE. **The firmware table at
0xC09F64 equals round(1024·2^(i/768)) for all 768 entries.** The largest
difference from the unrounded value is 0.4995, so plain rounding (no floor or
ceiling) is what was used. No exact .5 cases occur, so `std::lround` and
Python's `round` agree. `data/fc_table.json` is therefore not written.

## 2. Effect selection and the load sequence

### Where the effect parameters live

- Each sound has an effect parameter block at sound bytes 0x182..0x196. Only the
  even bytes are used.
- When a sound becomes current, 0xC0D682 copies the block to RAM **0xFFCBEE**.
  The copy takes block bytes +0x00, +0x02 .. +0x12 and the low 5 bits of
  +0x14.
- **Effect number** e = block[+0x14] & 0x1F, in the range 0..12. 0xC0F936 maps
  e ≥ 13 to 0.

### Effect record table 0xC1FB8A

There are 13 records of 16 bytes, one per effect number.

| Offset | Meaning |
|---|---|
| +0 | word: init routine, at 0xC10000 + word |
| +2 | word: modulation update routine (0xC1CD60 calls it) |
| +4 | parameter page set (index into the RAM table 0xFFD476) |
| +5 | **ESP program index** passed to 0xC1C8C0 (the pointer table 0xC1FC5A) |
| +6..+15 | **default values** of block bytes +0x00, +0x02 .. +0x12 |

0xC0F9F6 copies bytes +6..+15 into the block when the effect type changes.
The factory sounds (upper EPROM 0x1C040, 40 × 0x198 bytes) confirm that these
are the defaults: sounds 2 and 22 use effect 5, sound 36 uses effect 1, and
sounds 25 and 26 use effect 6, each with exactly these bytes.

The effect number is **not** the program header's "effect ID" byte (findings
2b). Program 4 has header ID 06 but belongs to effect 4, and program 10 has
header ID 04 but belongs to effect 12.

| Effect | ESP prog | Init | Defaults +00 .. +12 | Parameters (block offset = name) | Kind (from the names) |
|---|---|---|---|---|---|
| 0 | 2 | 0xC1CFC0 | 38 06 20 10 25 00 8F 54 20 20 | 00 Decay Time, 0E Diffusion, 04/06 Detune Rate/Depth, 0C HF Damping + HF Bandwidth, 02 Low Frequency Decay | reverb |
| 1 | 0 | 0xC1CFE0 | 20 07 28 10 35 00 5E 33 20 20 | same as effect 0 | reverb |
| 2 | 1 | 0xC1CFC0 | 28 13 20 10 35 00 8F 2A 30 30 | same as effect 0 | reverb |
| 3 | 3 | 0xC1D020 | 40 00 21 12 35 00 3E 48 30 30 | as effect 0 without LF Decay | reverb |
| 4 | 4 | 0xC1D43C | 00 00 14 1A 00 00 00 14 40 40 | 04/06/0E Chorus Rate/Depth/Center, 02 Feedback | chorus |
| 5 | 5 | 0xC1D3F0 | 28 00 14 1A 25 00 68 40 14 14 | Decay, HF Damping, Chorus Rate/Depth/Center, Feedback, 0C Chorus Level | chorus + reverb |
| 6 | 7 | 0xC1D3B8 | 3C A4 0F 32 25 00 6F 20 20 20 | Decay, HF Damping, Flange Rate/Depth/Center, Feedback, Flange Level, 04 Input Invert | flange + reverb |
| 7 | 7 | 0xC1D3D4 | 3C 88 12 40 25 00 6F 0A 20 20 | same as effect 6 (different $DE90 coefficient table) | flange + reverb |
| 8 | 8 | 0xC1D40C | 05 05 0F 40 25 00 0F 40 7F 7F | Phaser Rate/Depth/Center, Feedback, 00 Stereo Cross-Feedback, Phaser Level, Input Invert | phaser |
| 9 | 9 | 0xC1D424 | 3C 00 0F 20 25 00 6F 40 19 20 | Decay, HF Damping, Phaser Rate/Depth/Center, Feedback, Phaser Level, Input Invert | phaser + reverb |
| 10 | 9 | 0xC1D04C | 3C 00 5A 32 14 0A 60 40 1A 20 | Decay, HF Damping, Slow/Fast Speed, Rotor Center/Depth, Speed Mode, Mod Src | rotary speaker + reverb |
| 11 | 6 | 0xC1D3F0 | 28 00 14 1A 25 00 58 40 14 14 | Decay, 0C Dist Level, Chorus Rate/Depth/Center, Feedback, Chorus Level | distortion + chorus + reverb |
| 12 | 10 | 0xC1D472 | 4D FC 01 B7 25 00 53 0A 40 14 | Decay, HF Damping, Flange Rate, Compression, Distortion Level In/Out, "Reverb to Cmprss" Feedback, HiPass/LoPass Cutoff | reverb → compressor/distortion |

Common to all 13 effects:

- Every effect also has `+10 FX1`, `+12 FX2` and the modulation parameters
  `+08 Modulate / by` and `+0A` (amount).
- The parameter names are display strings: descriptor +6 points to text, and
  token 0x9E nn expands from the word table 0xC05972.

**Effect names (Step 4).** No effect-name strings were found in clear text. The
effect-select descriptor (0xC07D34, name word 0) uses a value list in what looks
like a compressed text area at 0xC076C0 (pairs of (pointer, word) into
0xC051xx–0xC057xx). That area is not decoded. The KS-32 user manual is not in the
repository; `datasheets/ks32.pdf` is the service manual. So `name` stays `null`.
The "Kind" column is derived only from the parameter names.

### Parameter descriptors and pages

- **Descriptors** are 10 bytes each, in the range 0xC07D34..0xC07F13:
  - +0 word: block offset;
  - +2: range, or a pointer to a value-name list;
  - +4: flags;
  - +6 word: name text;
  - +8 word: **handler**, at 0xC10000 + word.
- **Pages** are 4-byte records (descriptor-list pointer, byte, count). They live
  in RAM 0xFFD1C6..0xFFD725, which 0xC11938 copies from ROM 0xC1D872 at boot.
- **Page sets** are the 4-byte entries at RAM 0xFFD476 + 4·set. Each holds a
  page-list pointer and a page count.
- **16-bit pointers.** 0xC117FA maps a 16-bit table pointer w to 0xC0xxxx when
  w ≥ 0x50E4, else to 0xC1xxxx. Page pointers are sign-extended RAM addresses.

### What runs on an effect load (0xC0F9C2..0xC0FA38)

1. **0xC1C8C0: load the program.**
   - It fades GPR 7F down (0xC1CC62), halts the ESP, clears GPRs 0x00..0x8F and
     writes the program records and tables (as in `esp_init_sequence.json`).
   - **The ESP stays halted** after this step.
2. **Defaults.** If the effect type changed, the defaults are copied into the
   block (0xC0F9F6).
3. **Init routine** (record +0), with A3 = 0xFFCBEE.
   - It sets the effect's GPR map pointer $DE8C, the coefficient table pointer
     $DE90 and the modulation-target words $DE96/$DE9A.
   - Effect 10 also writes GPR 8E. Effect 12 sets $DE8A = 0x7F.
4. **Parameter handlers (0xC0FA70).** The handler of every descriptor of the
   effect's pages runs, in page order. The effect-select descriptor (handler
   0x989A) is skipped.
   - The handlers write GPRs through 0xC1CC80, a verified host write.
   - The modulation-routing handler 0xC1D262 re-enters the current modulation
     target's handler. The modulation amount (+0A) is 0 in every default
     except effect 10, and the sources are at rest, so this only repeats the
     same values.
5. **0xC1CB76: mix GPRs, unhalt, ramps.** See sections 3 and 4.

## 3. Mix level table 0xC0A72C and its indexing (Step 2)

The table holds 128 words: **table[i] = round(32767·sin(i·π/254))**, exact for
all 128 entries. Entry 0 = 0 and entry 127 = 0x7FFF. The pair (table[n],
table[127−n]) is an equal-power sine/cosine split.

```
  0: 0000 0195 032A 04C0 0654 07E9 097D 0B11 0CA5 0E38 0FCA 115C 12ED 147E 160E 179C
 16: 192A 1AB7 1C43 1DCE 1F57 20DF 2266 23EC 2570 26F3 2875 29F4 2B72 2CEF 2E69 2FE2
 32: 3159 32CE 3441 35B2 3721 388D 39F8 3B60 3CC6 3E29 3F8A 40E9 4245 439E 44F5 4649
 48: 479B 48E9 4A35 4B7D 4CC3 4E06 4F46 5082 51BC 52F2 5425 5555 5681 57AA 58D0 59F2
 64: 5B11 5C2C 5D43 5E57 5F67 6073 617C 6280 6381 647E 6577 666C 675D 684A 6933 6A18
 80: 6AF9 6BD5 6CAD 6D81 6E51 6F1C 6FE3 70A6 7164 721E 72D3 7384 7430 74D8 757B 761A
 96: 76B4 7749 77DA 7866 78ED 796F 79ED 7A66 7ADA 7B49 7BB4 7C1A 7C7A 7CD6 7D2D 7D7F
112: 7DCD 7E15 7E58 7E97 7ED0 7F05 7F34 7F5F 7F84 7FA5 7FC0 7FD7 7FE8 7FF5 7FFC 7FFF
```

### 0xC1CB76: what it writes

A6 = 0xFFCBEE, A2 = 0xC0A72C. A GPR value is the word << 8.

| GPR | Value | Index n | When written |
|---|---|---|---|
| 7D | table[127−n] | n = block[+0x10] ("FX1") | at once (0xC1CD8E), still halted |
| 7C | table[n] | same n | 0 at once, ramped up after unhalt |
| 7B | table[127−n] | n = block[+0x12] ("FX2") | at once (0xC1CDC2) |
| 7A | table[n] | same n | 0, then ramped |
| 77 | table[127−n] | n = byte $DE8A | at once (0xC1CDD0) |
| 76 | table[n] | same n | 0, then ramped |

Order of events in 0xC1CB76:

1. While the ESP is halted, write 7D, 7C = 0, 7B, 7A = 0, 77, 76 = 0.
2. Release ESPHALT (0xC1CBC8 → 0xC1CD3C).
3. Ramp 7C from 0 to its target in steps of 0xC0 (word), then 7A, then 76. A
   step that overflows 0x7FFF is clamped (0xC1CC3A), and the exact target is
   written last.
4. Ramp 7F (section 4).

So the mix ramps happen while 7F is still 0, which is inaudible.

### $DE8A (pair 76/77)

- $DE8A is set by handler 0xC1D18E from block[+0x0C] bits 3..0 (the "Chorus
  Level" of effects 5 and 11): n = 8.5·nibble, giving 0..127. Effect 12's init
  routine sets it to 0x7F.
- No other effect sets it, so it keeps the value an earlier effect left.
- Only ESP programs 5 and 6 (effects 5 and 11) read GPRs 76/77.

### Roles in the programs (from `esp/esp_prog_NN.txt`)

- 7C/7D scale SER1 (the FX1 send) and 7A/7B scale SER2 (the FX2 send), as two
  paths into the program.
- In program 0, 7D·SER1 + 7B·SER2 is accumulated into the delay-line input,
  while 7C·SER1 takes the other path.
- In every program, 7F multiplies the output path (`MAC C=… D=R7F`).
- The dry SER0 is added unscaled except by 7F (findings C).

Live edits use 0xC1CD7E / 0xC1CDB2 (FX1 / FX2 handlers). They pass the index
through the modulation adder 0xC1D1D8 (clamp 0..127) and write both GPRs of the
pair at once.

## 4. Output level GPR 7F: target, ramp and timing

### Target

- 0xC1CC54: D2 = word[**0xC0A62C** + 2·$DD72], GPR 7F = D2 << 8.
- $DD72 is the master volume. 0xC199AE moves it one step per call toward the
  slider reading $DDC8 (DUART-multiplexed ADC, 0..127) and also writes GPR 7F
  directly, so the volume slider drives GPR 7F continuously.
- Table 0xC0A62C: entry 127 = 0x7FFF and entry 0 = 0. The steps are 0.25 dB
  from 64 to 127, 0.5 dB from 32 to 63, and about 1 dB below that (−15.75 dB at
  64, −31.75 dB at 32).
- **The power-on value of $DD72 is the slider position, which is hardware.** The
  JSON uses **$DD72 = 127**, giving GPR 7F = **0x7FFF00**. This index is
  hand-set; the value comes from the firmware table.

### Ramps

- **Fade-up** (0xC1CC1A..0xC1CC38): D3 = 0, 0x12F, 0x25E, … while D3 < target.
  Each D3 is written as GPR 7F = D3 << 8. The step that overflows is clamped to
  0x7FFF and written, and then the target is written again. For target 0x7FFF
  that is **111 writes** (109 steps + clamp + target), counted from the
  interpreted run. The step is **0x012F00** in GPR units.
- **Fade-down before a load** (0xC1CC62): from the target down in steps of
  0x12F while ≥ 0, then 0. That is 110 writes for target 0x7FFF.

### Timing (Step 2; decision P13)

There is no timer in the ramp loops. The speed is set by the ESP host
protocol:

- **Three transfers per write.** Every write is 0xC1CC80:
  1. write select $A0;
  2. read-back select $80;
  3. park read-select $C0.

  Each select write first polls PC < 0x28 and then waits for Host Access OK, so
  each is one host transfer.
- **One transfer per sample, at END** (decisions E19 host part, E26). The ESP is
  running during the fade.
- **Two frames per transfer (estimate).** After a transfer completes at END,
  0xC1CC80 needs about 360 68000 cycles before its next select write. That is
  23 µs at 15.2 MHz, 36 µs at 10 MHz and 45 µs at 8 MHz. The PC < 0x28 window
  is the first 40 of the 89 instruction periods (15 µs at 8 MHz ESP), so the
  next write misses that frame's window and lands in the following frame. Its
  transfer then happens at that frame's END: 2 frames per transfer for any
  68000 clock from about 8 to 16 MHz.

Result: **111 writes × 3 transfers × 2 frames ≈ 666 frames ≈ 22.4 ms**
(`fade.frames` in the JSON).

- The lower bound is 333 frames, if the CPU always caught the window.
- Interrupt service (MIDI, DUART timer, OTIS IRQ) can only lengthen the ramp.
- The ramp is a staircase: GPR 7F rises by 0x012F00 about every 6 frames.
- Before the fade-up, the mix ramps (7C, 7A, 76, about target/0xC0 + 2 writes
  each, 6 frames per write) run silently. With effect 0's defaults that is
  about 138 writes, roughly 830 frames (28 ms) between unhalt and the start of
  the fade. The mix ramp's last step can overshoot the target by up to 0xBF
  before the exact target is written; this is inaudible because 7F = 0.

The KS-32 68000 clock was not confirmed from the ROM. The 2-frames-per-transfer
figure holds for any clock in the 8–16 MHz range given the cycle count above.

## 5. The other GPRs the firmware sets

The handlers write many more GPRs than the mix pairs: decay and damping
coefficients, delay tap lengths, LFO rates and so on. They are computed from
the parameter bytes, and some use ROM tables (0xC08B9C, 0xC0B000, the $DE90
coefficient lists such as 0xC1D448).

**Method.** `extract_effect_params.py` contains a small 68000 interpreter that
runs these firmware routines unchanged, for each effect:

1. the init routine;
2. the page handlers in 0xC0FA70 order;
3. 0xC1CB76.

The interpreter covers MOVE/MOVEA/MOVEQ, ADD/SUB/CMP/AND/OR/EOR and their
immediate and quick forms, MULU/MULS, shifts, Bcc/BSR/JSR/RTS, DBcc, Scc,
LEA, MOVEM, bit operations, CLR/NEG/NOT/TST/EXT/SWAP and the CCR forms. Any
other opcode stops it with an error.

The ESP write 0xC1CC80 is intercepted. The interceptor logs (GPR = D1.b,
value = D2 & 0xFFFFFF) and leaves D2/D4/D0/A0 as the real routine does after a
first-try verified write. The ESPHALT release 0xC1CD3C is a no-op.

**Cross-checks done by hand:**

| Effect | Handler | Firmware rule | Value |
|---|---|---|---|
| 6 | 0xC1D0F0 | GPR 86 = byte +02 << 16 | 0xA40000 |
| 11 | 0xC1D172 | GPR 8F = ((+0C >> 4)² + 3) << 15 | 0x0E0000 |
| 5 | 0xC1D18E | $DE8A = 8.5·(+0C & 0xF) = 68 | 76/77 = 0x5F6700 / 0x555500 |
| 0 | 0xC1CF20 | GPR 01 = (+0C & 0xF0)/2 << 16; GPR 02 = 0x7FFFFF − GPR 01 | 0x400000, 0x3FFFFF |
| 4 | 0xC1D356 | coefficient × (block+04 + 8) >> 5 | GPR 6C = 0x7F7·28 >> 5 = 0x6F8 |

The handler values are close to the program records' own presets, for example
program 0: GPR 08 = 0x330000 vs preset 0x333333, and 0E = 0x6BADFF vs
0x6B851E. The firmware overwrites the presets, so the JSON carries the handler
values.

### GPRs that only the host supplies

These are GPRs that an ESP program reads, never writes, and that its records do
not preset:

| Programs | GPRs |
|---|---|
| all | 7A 7B 7C 7D 7F |
| 5, 6 | also 76 77 |
| 5, 6, 7 | also 86 |
| 6 | also 8F |

Every one of them is written by the firmware path above. **No mix level had to
be hand-set, so the empirical chip-core fallback of the brief (Step 3) was not
needed and no scratch harness was built.**

## 6. `data/esp/effect_defaults.json`

```
{"source": ..., "fade": {"gpr": "7F", "frames": 666, "from": "000000", "to": "7FFF00",
                         "step": "012F00", "writes": 111, "basis": ...},
 "programs": [{"index": 0..10, "name": null, "effect": e, "also_used_by_effects": [...],
               "pointer": "0xC2....", "gpr": {"7F": ..., "7C": ..., ...}, "basis": {...}}]}
```

- **Entries.** There is one entry per ESP program index. It uses the lowest
  effect number that loads that program. Program 7 is shared by effects 6
  and 7, which differ in their default parameters and their $DE90 coefficient
  table but have the same FX1/FX2 defaults (0x20/0x20). Program 9 is shared by effects 9 and 10; the JSON uses effect 9's
  phaser parameters.
- **`gpr`.** These are the final values of every GPR the firmware writes during
  the load. The order is 7F, 7C, 7D, 7A, 7B, 76, 77, then the rest by address.
  For 7F, 7C, 7A and 76 the value is the ramp target.
- **Applying it.** A player applies `gpr` after the program records, and ramps
  7F from 0 to `fade.to` in `fade.writes` steps of `fade.step` over
  `fade.frames` frames. It can set the mix GPRs at once, because the firmware's
  mix ramps run while 7F = 0.
- **`basis`.** Each GPR's basis names the table entry and index, or the handler
  that last wrote it. It adds "not referenced by ESP program N" where that is
  true, for example 76/77 outside programs 5 and 6.

| Prog | Effect | 7F | 7C | 7D | 7A | 7B | 76 | 77 | Other GPRs written |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 1 | 7FFF00 | 315900 (−8.3 dB) | 761A00 (−0.7) | 315900 | 761A00 | 000000 | 7FFF00 | 01–08 0B 0E 6A 6C 6D 6E |
| 1 | 2 | 7FFF00 | 479B00 (−5.0) | 6A1800 (−1.6) | 479B00 | 6A1800 | 000000 | 7FFF00 | 01–08 0B 10 11 18 1B 6A 6C 6D 6E |
| 2 | 0 | 7FFF00 | 315900 | 761A00 | 315900 | 761A00 | 000000 | 7FFF00 | 01–08 0B 10 11 18 1B 6A 6C 6D 6E |
| 3 | 3 | 7FFF00 | 479B00 | 6A1800 | 479B00 | 6A1800 | 000000 | 7FFF00 | 01–08 0B 11 6A 6D 6E |
| 4 | 4 | 7FFF00 | 5B1100 (−3.0) | 59F200 (−3.1) | 5B1100 | 59F200 | 000000 | 7FFF00 | 61–6C 6E 80–83 86 |
| 5 | 5 | 7FFF00 | 1F5700 (−12.2) | 7C1A00 (−0.3) | 1F5700 | 7C1A00 | 5F6700 (−2.6) | 555500 (−3.5) | 01 02 05–07 63 64 67 68 6B 6E 85 86 8D |
| 6 | 11 | 7FFF00 | 1F5700 | 7C1A00 | 1F5700 | 7C1A00 | 5F6700 | 555500 | 05–07 63 64 67 68 6B 6E 85 86 8D 8F |
| 7 | 6 (7) | 7FFF00 | 315900 | 761A00 | 315900 | 761A00 | 000000 | 7FFF00 | 01 02 05–07 63 64 67 68 6B 6E 85 86 88 8D |
| 8 | 8 | 7FFF00 | 7FFF00 (0) | 000000 | 7FFF00 | 000000 | 000000 | 7FFF00 | 6E 81 86 88 8C 8D |
| 9 | 9 (10) | 7FFF00 | 26F300 (−10.3) | 79ED00 (−0.4) | 315900 | 761A00 | 000000 | 7FFF00 | 01 02 05–07 6E 86 88 8C 8D 8E |
| 10 | 12 | 7FFF00 | 5B1100 | 59F200 | 1F5700 | 7C1A00 | 7FFF00 | 000000 | 00–04 6E 85 86 89 8F 91 92 99 |

### Firmware-derived vs hand-set

- **From the firmware:** every value in the JSON. That covers the tables, the
  index rules, the handler arithmetic, the default parameter bytes and the ramp
  step and count.
- **Hand-set inputs** (also stated in each `basis` and in decision P11):
  - the parameter block = the effect's firmware defaults, rather than a
    particular factory sound;
  - $DD72 = 127 (master volume at maximum);
  - $DE8A = 0 for effects that do not set it (only programs that do not read
    76/77 are affected);
  - modulation sources at rest (RAM 0);
  - for the shared programs 7 and 9, the lower effect number.
- **`fade.frames`** is an estimate from cycle counting (section 4), not a
  measurement.

## 7. How this was found

- **Disassembly.** Ghidra 12.1.3 headless (68000:BE:32, base 0xC00000) in a
  throw-away project in the session's temp directory, outside the repository.
  It used vector seeding, seeds at the known ESP routines and the aggressive
  instruction finder, plus a small `DisAt` script to disassemble from given
  addresses. The user's Ghidra project was not touched.
- **Key routines:**
  - 0xC1CB76 (mix/unhalt/fade);
  - 0xC1CC42..0xC1CC4E (GPR selectors 7C/7A/76/7F);
  - 0xC1CC54 + 0xC19BA4 (volume table);
  - 0xC1CC62 (fade-down);
  - 0xC1CD8E/0xC1CDC2/0xC1CDD0 (cosine partners);
  - 0xC0F936..0xC0FAD6 (effect selection, handler loop);
  - 0xC0D682 (block copy);
  - 0xC11938 (page-table RAM copy);
  - 0xC117FA (16-bit pointer mapping);
  - 0xC1CE44..0xC1D4C0 (handlers and init routines).
- **Text.** The token decoder is in the script (`decode_text`).

## Unknown

- **Effect names.** See section 2. The compressed text at 0xC076C0 / 0xC051xx is
  not decoded.
- **The 68000 clock and interrupt load.** These set how many frames each host
  transfer really takes (2 is an estimate; 1 is the lower bound).
- **Power-on state.** The power-on sound and the slider position are hardware
  and battery-RAM state. The firmware has no fixed power-on mix index other
  than the per-effect defaults. The "init" sound template (0xC12876, data
  0xC1D5F0) uses effect 0 with all parameters 0, so FX1 = FX2 = 0 (7C = 7A = 0).
