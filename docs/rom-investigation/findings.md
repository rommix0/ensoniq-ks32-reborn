# KS-32 ROM investigation: findings

Date: 2026-10-19. Scope: open items A–D and section 8 of
`docs/design/2026-10-19-otis-esp-emulator-design.md`.
Firmware analysed: KS-32 OS v3.01 (`ks-32/ks32/ks32v301upper.bin` = even bytes,
`ks32v301lower.bin` = odd bytes), merged at 0xC00000. v3.00 and SQ-2 v2.03 were
used for comparison only.

Everything here can be reproduced with the scripts in `tools/` (pure Python,
run with `python -I`). Firmware addresses are CPU addresses in the merged image.

| Item | Answer | Confidence |
|---|---|---|
| A. Wave ROM format | Bank 0: 8-bit samples, 1 byte = 1 word, u25 = words 0x00000–0x7FFFF, u26 = 0x80000–0xFFFFF. Bank 1: 16-bit **little-endian** words, rom2 = 0x00000–0x7FFFF, rom3 = 0x80000–0xFFFFF. | **High** |
| B. ESP clock | **8 MHz** is the best fit: 16 MHz crystal / 2, which gives exactly 89 instruction slots per 29.76 kHz frame. Hard lower bound: 7.946 MHz. | Medium |
| C. Serial routing | ESP SER0–2 are inputs and SER3 is the output (host serial control = 0x63: slave, Sony). Voices use CA 1–3 (CA = bus+1). SER0 behaves as the dry bus; SER1 and SER2 are the effect sends. OTIS CH0 is unused by voices. | High (ESP side); medium-high for CHn→SER(n−1) |
| D. Delay DRAM size | **64K words** (128 KB). Every program writes MEMSIZ = 0x0000FF. | High |
| 2a. Sample map | Found and exported: 126 internal waves (388 zones) and 43 rom3 waves (110 zones, named). | High for the format; medium for waves 0x6F–0x78 |
| 2b. ESP programs | Found and exported: 11 pointers, 10 distinct programs, each 89 instructions with END at pc 0x58. Includes initial GPRs and two DRAM tables. | High |

---

## A. Wave ROM format (decision)

**Decision.**

- **Bank 0 (BS=0).** Use `sq1-u25.bin` then `sq1-u26.bin`, one signed 8-bit sample per ROM byte. Each byte goes on D15..D8 of a 16-bit word, with D7..D0 = 0. Word address = byte offset, and u26 starts at word 0x80000.
- **Bank 1 (BS=1).** Use `rom2.u39` then `rom3.u38`. Each file is already a stream of little-endian 16-bit words (file byte 2n = D7..D0), and rom3 starts at word 0x80000.
- This matches what MAME's KS-32 ROM loading produces. MAME's "unswapped" `ROM_LOAD` for rom2/rom3 is correct for this dump. The SD-1's word swap reflects how those dumps were made and does not apply here.

**Converter.** `tools/convert_wave_roms.py <rom dir> <out dir>` writes `bank0.bin` and `bank1.bin`, each 1M little-endian uint16 words indexed by the OTIS 20-bit word address.

### Evidence 1: signal statistics (`tools/wave_format_stats.py`, output in `data/wave_format_stats.txt`)

The script analyses every 16th non-silent 256-sample block. It reports the lag-1 autocorrelation (r1), the spectral flatness (sfm), and the lag-1 autocorrelation of the low byte alone (lo_r1). A real low byte of a 16-bit sample is noise, so its lo_r1 is near 0.

| Interpretation | r1 | sfm | lo_r1 | Verdict |
|---|---|---|---|---|
| u25 as 8-bit, 1 byte/sample | 0.932 | 0.014 | — | audio |
| u25 as 16-bit pairs (either order) | 0.82 | 0.046 | 0.62 | the "low" byte is not noise, so these are not pairs |
| u25 = hi, u26 = lo at the same address | — | — | 0.70 | u26 is smooth audio, not a low byte; corr(u25[i], u26[i]) = 0.002 |
| rom2 16-bit LE | **0.972** | **0.004** | **−0.01** | audio with a noise-like low byte |
| rom2 16-bit BE | −0.001 | 0.556 | 0.85 | white noise |
| rom3 16-bit LE | **0.915** | **0.013** | **−0.002** | audio |
| rom3 16-bit BE | 0.010 | 0.566 | 0.79 | white noise |

### Evidence 2: rom2/rom3 carry their own directories

Words 0 onward of each ROM hold a directory: a 16-bit byte offset followed by a 12-character name, then 14-byte zone records. The text reads correctly only as LE words, for example `5649 4f4c 4120 ...` = "VIOLA SECT", and "SHAMIZEN", "ELEC PIANO 1" and so on in rom3, and "B-PNO1-HI", "B-PNO1-LO", "SIN-32" in rom2. The OS ROM contains a big-endian copy of rom3's directory and records at 0xC1DDD2.

### Evidence 3: the firmware's own loop points (`tools/verify_loop_seams.py`, `data/loop_seam_check.txt`)

For every forward-looping zone in the firmware sample map, the script compares the loop-seam step |x[loop_start] − x[end]| with the mean step inside the loop. As a null reference it repeats the comparison with the loop start moved by a pseudo-random offset.

| Interpretation | Zones in range | Seam/step median, p90 | Null median |
|---|---|---|---|
| bank0 8-bit, u25 below u26 (chosen) | 325/325 | **0.00, 0.27** | 6.84 |
| bank0 8-bit, u26 below u25 | 325/325 | 6.02, 38.0 | 4.73 |
| bank0 any 16-bit pairing (512K words) | **71/325** | — | — |
| bank1 LE, rom2 below rom3 (chosen) | 139/139 | **0.00, 1.01** | 5.86 |
| bank1 BE | 139/139 | 0.00, 2.00 | 0.94 (noise) |
| bank1 LE, rom3 below rom2 | 139/139 | 7.89, 25.6 | 7.05 |

- 254 of the 325 bank-0 zone addresses are at or above 0x80000. Only the 1M-word, 8-bit reading can supply them.
- The firmware also sets **bit 28** (word address bit 19) on every rom3 address (0xC1B04E). That puts rom3 in the upper half of bank 1.

### Unknown

- The value of D7..D0 for bank 0 on the real board (pulled low, pulled high, or open). 0 is assumed. 0xFF would add a DC offset of only 255/65536.
- The KS-32 has no nibble ROM, so the 12-bit "denib" scheme cannot apply.

---

## 2a. Sample map (waveform → zones)

**Output.**

- `data/sample_map.json`: the full structure, plus the formulas.
- `data/sample_map.csv`: one row per zone.
- Generated by `tools/extract_sample_map.py`.

### Firmware path

Routines 0xC1ACA4–0xC1AD12 (voice set-up) and 0xC1AE56–0xC1B1C8 (voice start) work as follows:

- The waveform number is **W = sound-program byte 0x6C & 0x7F**.
- **Internal set.** `zone0 = 0xC1E636 + word[0xC1FA8E + 2W]`.
- **Expansion set (rom3).** The expansion set is used when the ROM-present flag at $DBA1 is set and the program's flag bytes give the pattern 0x1515. Then `zone0 = 0xC1DDD2 + word[0xC1DDD4 + 14W]`.
  - The 12-character name follows that offset word. The 43 names start at 0xC1DDD6.
  - When $DBA1 = 0 the display shows "EMPTY SOCKET", so rom3 sits in an optional socket.
- **Zone selection.** The firmware steps through 14-byte records while the note number is greater than the record's top key. The last zone of a waveform has top key 0x7F.

### Zone record (14 bytes, big-endian)

| Off | Field | Meaning |
|---|---|---|
| +0 | int16 pitch word w | P = note·256 + w (P is clamped to [−0x4800, 0x5400]). FC = 0x400·2^(P/3072), from the table at 0xC09F64 (768 entries per octave, 0x0400..0x07FE). So **root key = −w/256**: 0xC400 → key 60 plays at 1.0 word per frame. |
| +2 | u32 start | OTIS address format (word address << 9). **Bit 29 = BS**, bits 28:9 = the 20-bit word address. **Low byte = the zone's top key.** The firmware clears this byte before writing ACC. |
| +6 | u32 loop start | Written to START. Bits 4:0 = loop-mode index into the table at 0xC0A83C, which gives the CR low byte: 0→0x03 (one-shot), 1/2/4→0x0B (forward loop), 3→0x1B (bidirectional). Bit 7 is tested elsewhere. |
| +10 | u32 end | Written to END. Bits 8:5 are real fractional END bits. |

### Voice start (0xC1B03E)

1. The firmware writes CR (with STOP0/STOP1 set), START = loop, END = end and ACC = start.
2. It sets **BS = bit 29** of the start long.
3. A per-note modulation (program bytes 0x70/0x72 times voice byte 0x08, probably velocity) can push the start point toward the end (0xC1AEEC).
4. The voice is released later by clearing the STOP bits (0xC1A880).

### Coverage

- **W 0–0x6E:** plain multisample waves. They decode cleanly, and their addresses are contiguous across the 1M-word bank 0.
- **W 0x79–0x7D (121–125):** the rom2 piano layers and SIN-32 (bank 1). They have a separate firmware path at 0xC1A37A that probably layers HI and LO. That path is not decoded.
- **W 0x6F–0x77:** a special encoding. The voice is started on a second OTIS voice with CR = 0x0B, and the "start" fields are not plain addresses. The zone walk is **unverified** and may run into the next wave.
- **W 0x78:** picks its records from the whole table using program bytes 0x70/0x72.
- **W 0x7E–0x7F:** their offsets do not point into the table.

### Unknown

- **Names of the internal (bank 0 and rom2) waveforms.** No waveform name text exists in the OS. Display strings are tokenized (0x9E nn → word table at 0xC05972, 0x9F = end), and the wave-name list was not found. Names could be taken from the KS-32 manual's waveform list.
- **Program bytes.** The meaning of program bytes 0x70/0x72 (start modulation) and the piano layering are not decoded.

### Factory sounds

Upper-EPROM 0x1C040–0x1FFFF (CPU 0xC38080, even bytes) is copied into RAM 0xFF8C2E–0xFFCBEE at init (0xC12860). The program stride is 0x198 = 408 bytes, which suggests 40 programs. This data is what differs from the SQ-2 v2.03 OS. It is a lead for real preset data, but its format is not decoded.

---

## 2b. ESP effect programs

**Output.**

- `data/esp/esp_programs.json`: every program's raw records, INSTR and GPR maps, header, and the DRAM tables.
- `data/esp/esp_prog_NN.txt`: a field-level disassembly of each distinct program.
- Generated by `tools/extract_esp_programs.py`.

### Where and how

- **Pointer table.** At **0xC1FC5A**: 11 longword pointers indexed by effect number (routine 0xC1C8C0).
- **Address mapping.** A pointer P is mapped to `0xC20000 + 2·(P − 0xC20000)`. Programs live in the byte-wide area: the even bytes of 0xC20000–0xC3FFFF, which is upper-EPROM file offset 0x10000 + (P − 0xC20000).
- **Program layout.** Each program is an 8-byte header followed by records `type, count, addr, data…` and ends with type 0xFF:

| Type | Data per entry | Host registers written | Select register |
|---|---|---|---|
| 1 | GPR (3 bytes) + INSTR (6 bytes) | $00–$08 | $E0 |
| 2 | INSTR (6 bytes) | $03–$08 | $C0 |
| 3 | GPR (3 bytes), consecutive addresses | $00–$02 | $A0 |
| 4 | address (1 byte) + GPR (3 bytes) | $00–$02 | $A0 |

- **Byte order.** INSTR bytes go out MSB first (bits 47..40 first). The byte order is confirmed by END: ALU opcode 0xF at bits 15:12 appears **only at pc 0x58** in every program.
- **Header.** Byte 0 = 0x45. Bytes 1–2 = encoded size − 3. Byte 5 = effect ID, with values 0,1,2,4,5,6,8,9,0xA,0xB present. Byte 6 = **0x59 = 89 = program length**.

| Index | Pointer | Effect ID | NOP-like padding | Notes |
|---|---|---|---|---|
| 0 | 0xC20354 | 00 | 36 | |
| 1, 2 | 0xC20000 | 01 | 2 | indices 1 and 2 share one program |
| 3 | 0xC20637 | 02 | 28 | |
| 4 | 0xC20939 | 06 | 1 | |
| 5 | 0xC20C8F | 08 | 1 | END carries MAC + WR_DELAY |
| 6 | 0xC20FF2 | 05 | 1 | END carries MAC + WR_DELAY |
| 7 | 0xC21349 | 0B | 1 | END carries MAC + WR_DELAY |
| 8 | 0xC216B0 | 09 | 7 | only 21 GPRs preset |
| 9 | 0xC21928 | 0A | 2 | |
| 10 | 0xC21C31 | 04 | 3 | uses tables A and B; DLENGTH = 0x1F7DFF, ABASE = 0x1FBF00, BBASE = 0x1F7E00 |

### Initial SPR values (from the records)

- **MEMSIZ = 0x0000FF** in all programs.
- **DLENGTH = 0xFFFFFF**, except 0x1F7DFF in program 10.
- **SIGREG = 0xFFFFFF**, i.e. bit 22 = 1, product shift ONE. Program 10 keeps the boot value, which is also 0xFFFFFF.

### DRAM tables (program 10 only)

The tables are listed right after the pointer table as (pointer, DRAM word address) pairs and are written by the host with RAM access while halted (0xC1C966):

| ROM address | DRAM words | Contents |
|---|---|---|
| 0xC1FC92 | 65 words at 0x1FBF–0x1FFF | a half-cosine S-curve from 0x8001 to 0x7FFF |
| 0xC1FD16 | 65 words at 0x1F7E–0x1FBE | a 1/x-like curve from 0x7FFF down to 0x00FC |

### Init and load sequences

The full host-register sequences are in `data/esp_init_sequence.json`. Points that matter for later firmware emulation:

- **PC poll.** Before every select-register write while the ESP runs, the host waits until **PC (host reg $16) < 0x28**. MAME's constant read-back of 0x27 passes this check.
- **Host Access OK/ poll.** The host polls bit 2 of $12 (Host Access OK/). On timeout it raises error 0x21 = 33, "bad ESP chip".
- **Read-back verification.** Every load is verified by reading it back.
- **Parking.** Afterwards read-select is parked at 0xC0.
- **Fade.** GPR 0x7F is the output level, and the firmware fades it down before a load and up after it.

### Unknown

- **Effect names.** The display text is tokenized; only fragments such as "Chorus", "Flange", "Phaser", "Rotor", "Delay", "Dist" and "Reverb to Cmprss" were found. The mapping from effect ID to name is open.
- **Operand-select (`sel`) field.** The disassembly prints it raw; it is not interpreted against the datasheet's table.
- **Effect-index selection.** Which sound parameter picks the effect index (D0 at 0xC1C8C0) is not traced.

---

## B. ESP clock

### Facts from the firmware

1. **Fixed length.** Every program is exactly 89 instructions (pc 0x00–0x58, END at 0x58), and the header field says so (0x59). Programs 0 and 3 are padded to that length with 36 and 28 NOP-like words. So 89 is a deliberate fixed length, not the length of the longest effect.
2. **No hardware sync.** ESPHALT (DUART OP6) is touched only around program loads: 0xC1CD4A/0xC1CD3C, called from 0xC1C7E8, 0xC1CCE0, 0xC1CBC8 and 0xC11514. It is never used for per-sample sync. The ESP is in **slave** serial mode, with clocks from OTIS.
3. **Voice count.** OTIS ACT = 0x1F (32 voices) on the KS-32 branch, so the frame rate is CLK/512. MAME's 15.238 MHz gives 29 761.9 Hz.

### Reasoning

- The ESP has no sync instruction; the ESP2's BIOZ does not exist on the ES5510. So one program pass must fit in one frame: 3 × 89 = 267 ESP clocks per frame, giving **MCLK ≥ 7.946 MHz**.
- If 89 is the number of whole slots that fit, then MCLK lies in [7.946, 8.036) MHz. Of common clocks only **8 MHz** falls in that range: 10 MHz would give 112 slots and 15.238 MHz would give 170.
- MAME's own source comment lists the SQ-2 board crystals as "16MHz  30.47618MHz", and its DUART runs at 4 MHz (= 16/4). 16/2 = 8 MHz is therefore available on the board.

### Cross-check: the boot clock self-test (0xC110DC–0xC11186, 0xC1AB36)

1. With ACT = 31, the firmware starts voice 0 at FC = 0x100.
2. It runs the DUART counter for 2500 ticks of X/16.
3. It reads the voice's ACC.
4. If ACC is in [0x4C00, 0x5000] (152–160 frames), it switches to **ACT = 0x14 (21 voices)**. That is the SQ-1 case, OTIS clock = 2 × DUART clock (10 MHz/5 MHz in MAME). Otherwise it keeps 32 voices.
5. With MAME's KS-32 clocks (15.238 MHz OTIS, 4 MHz DUART) the test gives about 298 frames, so 32 voices. This is consistent, but it constrains only the ratio of the two clocks.

### Recommendation for the emulator

- Model the ESP at 8 MHz (a budget of 89 instructions per frame).
- Run exactly one program pass per OTIS frame, as MAME does.
- Log the choice in `docs/decisions.md` as medium confidence.

### Unknown

- The actual board wiring of ESP CLKIN.
- Whether a slave-mode END waits for LRCLK. If it does not, a free-running 8 MHz ESP would run at 29 963 Hz, 0.67 % fast against OTIS. The datasheet does not say.
- The handwritten datasheet note "Instructions = ½ × (Sample cycle / Instr cycle)" fits an ESP at half the OTIS clock (85.3 slots). That is too few for 89 instructions, so it probably describes a different reference design.

---

## C. Serial routing / CA assignments

### ESP side

- Boot writes **host serial control = 0x63**: bit 7 = 0 (SLAVE), bit 6 = 1 (SONY), bit 5 = 1 (SER3 = output), bits 4–2 = 0 (SER2/1/0 = inputs).
- Every program reads SER0, SER1 and SER2 (L and R) and writes only SER3 L/R. SER3 appears only as the A (destination) operand.
- **SER0 enters only as a plain ALU addend** into the output path, e.g. `ADD A=R69 B=SER0L`. That is the dry bus.
- **SER1 and SER2 are MAC D operands** (coefficient × input), i.e. the effect sends. In programs 5, 6, 7, 9 and 10, SER1 is also used as an ALU addend.

### OTIS side

- Voice start (0xC1AE92–0xC1AEC0, and also 0xC1C4CE) computes `CR[9:8] = ((bus & 3) + 1) & 3`.
- Bus 0–2 comes from byte 0x11 of a per-part structure (pointer at voice+0x8C) if it is < 3, otherwise from bits 5:4 of sound-program byte 0x60 (0x12 in the second routine).
- So voices use **CA 1, 2, 3**. Bus 3 would produce 0x400, which also sets LP3, so it is evidently never used.
- CH0 is used only by the boot clock-test voice.
- OTIS init (0xC1AB36) writes page 0x40 reg 8 **SERMODE = 0**: A/D off, so all 4 channels go out serially.

### Conclusion

CA1 → SER0 (dry), CA2 → SER1 (FX1 send), CA3 → SER2 (FX2 send), ESP SER3 → main DAC, and OTIS CH0 is unused. This agrees with MAME's VFX-derived routing. MAME's Aux output on CH0 is harmless but silent on the KS-32.

### Unknown

- The physical pin wiring. That CHn goes to SER(n−1), rather than some other permutation, is inferred from how the programs treat SER0 (dry) versus SER1/SER2 (sends).

---

## D. Delay DRAM size

- MEMSIZ is written as **0x0000FF** at boot (0xC1C800) and by every effect program.
- Per the datasheet (§3.5.2), 0x0000FF means 16 address bits, i.e. a **64K-word** system, which is the datasheet's own example. The application diagram also shows a 64K × 16 DRAM.
- So the KS-32 has 64K words = 128 KB, not MAME's 1M words. MAME's own TODO says "most games use 128KB".
- DLENGTH = 0xFFFFFF (the whole memory) in 9 of the 10 programs. Program 10 sets DLENGTH = 0x1F7DFF and places its tables at 0x1F7E–0x1FFF.
- Unknown: whether the physical DRAM is smaller and aliases. Program 10's tables just below 0x2000 would also work with 8K words, but MEMSIZ says 64K.

---

## Other findings useful for the chip emulators (open item E)

- **The firmware reads wave samples through OTIS.**
  - At voice start it writes CR with STOP bits set, sets ACC, waits, then reads **page|0x20 reg 6 = O1(n−1)** and steps ACC to find a sample with |x| < 0x800, i.e. a zero crossing (0xC1B0B2–0xC1B13E).
  - So a **stopped voice must still fetch the sample at ACC into O1(n−1)**. This is the behaviour MAME only fakes for Taito F3.
- **END is a full instruction.** Its MAC and RAM fields are used: programs 5–8 and 10 have END words carrying MAC with WR_DELAY.
- **SIGREG.** Bit 22 is 1 in every program, so the product shift is 1.

## Identical shared region (task note)

- In the upper EPROM only two parts of the high half hold data: 0x10000–0x11FFF (the ESP programs and their header area) and 0x1C000–0x1FFFF (factory sounds from 0x1C040). 0x12000–0x1BFFF is all 0xFF.
- KS-32 v3.00 and v3.01 are byte-identical over the whole of 0x10000–0x1FFFF.
- The SQ-2 v2.03 OS is identical over 0xFECD–0x1C03F, so the **ESP programs are shared with the SQ-2**. The factory sound block from 0x1C040 differs.

## What was tried and how

- **Disassembly.** The OS was disassembled with Ghidra 12.1.3 headless (68000:BE:32, base 0xC00000, vector seeding plus the aggressive instruction finder) in a throw-away project outside this repository. The user's open Ghidra project was not touched.
- **MAME source.** MAME `esq5505.cpp` (master) was read for the memory map and clocks only:
  - OTIS 0x200000, ESP 0x260001 (odd bytes), DUART 0x280001.
  - SQ-2/KS-32 crystals 16 MHz and 30.47618 MHz.
  - DUART at 4 MHz on 32-voice machines and 5 MHz on SQ-1.
- **Service manual.** The `datasheets/ks32.pdf` text has no schematic or clock information, only error codes; code 33 = bad ESP chip matches the firmware.
