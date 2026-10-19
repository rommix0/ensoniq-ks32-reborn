# MAME ES5505 (OTIS) / ES5510 (ESP) emulation audit — as used by esq5505.cpp

Sources fetched 2026-10-19 from mamedev/mame master (raw copies in `scratchpad/mamesrc/`):

| File | Lines | Notes |
|---|---|---|
| `src/devices/sound/es5506.cpp` / `.h` | 2134 / 265 | ES5505 + ES5506 share `es550x_device` (Aaron Giles + later edits) |
| `src/devices/cpu/es5510/es5510.cpp` / `.h` | 1323 / 199 | ESP is a **cpu_device**, not under sound/ (Christian Brunschen) |
| `src/devices/cpu/es5510/es5510d.cpp` | 21 | Disassembler is a **stub**: `disassemble()` just `return 1;` |
| `src/devices/sound/esqpump.cpp` / `.h` | 153 / 111 | "pump" glue: OTIS -> ESP serial -> speakers. **Drives the ESP.** |
| `src/mame/ensoniq/esq5505.cpp` | 1299 | Driver (VFX, VFX-SD, SD-1, SD-1/32, SQ-1, SQ-R, SQ-2, KS-32, EPS) |

Line numbers below refer to those files.

---

## 0. Driver wiring (esq5505.cpp)

### Status flags (L1290-1299)
- `vfx`, `vfxsd`, `sd1`, `sd132`: `MACHINE_IMPERFECT_SOUND`.
- `sq1`, `sqrack`, `sq2`, **`ks32`**: `MACHINE_NOT_WORKING`. KS-32 is a clone of SQ-2 and uses the `ks32` config (`common32` + `sq1_map`, L973-981).

### Clocks and sample rate
- `common` (VFX/SD-1 21-voice, SQ-1): 68000, OTIS, ESP all at `10_MHz_XTAL` (L773-800). The pump's initial clock is `10 MHz/(16*21)` = 29 761.9 Hz (L795), i.e. 21 voices.
- `common32` (SD-1/32, SQ-2, **KS-32**): 68000 and OTIS at `30.47618 MHz/2` = 15.238 MHz (L908, L943); the pump is at `30.47618 MHz/(2*16*32)` = 29 761.9 Hz (L934). The **ESP is still declared at 10 MHz** (L912). That clock is meaningless because the ESP is never scheduled (see below).
- The OTIS rate is `clock/(16*(ACT+1))` (es5506.cpp L1690). On each ACT write, `sample_rate_changed` -> `es5505_clock_changed()` -> `m_pump->set_unscaled_clock()` (L630-633), so the pump/ESP rate follows the OTIS rate.

### ESP is disabled as a CPU and run per sample by the pump
```cpp
ES5510(config, m_esp, 10_MHz_XTAL);   // L777 / L912
m_esp->set_disable();                 // L778 / L913: never scheduled by MAME
ESQ_5505_5510_PUMP(config, m_pump, ...); m_pump->set_esp(m_esp);
```
ESPHALT is DUART output port bit 6 (L669-684). It only toggles `m_pump->set_esp_halted()`. When it is set, the pump skips the ESP completely.

### Serial routing (OTIS CA channel -> ESP SER port), hard-coded in esqpump.cpp L69-96
- OTIS has `set_channels(4)`, so 8 outputs (L804). All 8 go to pump inputs 0-7.
- OTIS ch0 (CA=0, L/R) -> pump outputs 2/3 = **"aux"** speaker directly. This path bypasses the ESP. The `aux` speaker exists only on vfxsd/sd1/common32 (L862-864, L939-941).
- OTIS ch1..ch3 (CA=1..3) -> ESP `SER0L/R`, `SER1L/R`, `SER2L/R` (via `ser_w(0..5)`).
- ESP `SER3L/R` (`ser_r(6/7)`) -> main speaker.
- The comment says this is "Modeled specifically after the routing of ... the VFX family" (esqpump.cpp L7-8). **Nobody has verified it for the SQ-2/KS-32 boards.**

### Wave ROM banking / the "nibbles" region
- OTIS CR bit 2 is BS (bank select): `get_bank = (control>>2)&1` (es5506.h L241). BS=0 -> `waverom` (region0), BS=1 -> `waverom2` (region1). Each bank is a 20-bit word space (2 MB).
- VFX/SD-1 (`init_denib`, L1267-1285): the 8-bit wave ROMs sit in the odd bytes. The 512 KB `nibbles` ROM supplies 4 more LSBs (low nibble -> lower half, high nibble -> upper half), which gives **12-bit samples** in BS=0. BS=1 (SD-1) holds 16-bit words loaded with `ROM_LOAD16_WORD_SWAP`.
- **SQ-1 / SQ-R / SQ-2 / KS-32** use `init_sq1`, which **never calls denib**. Their `nibbles` region is declared `ROMREGION_ERASE00` with nothing loaded (KS-32: L1236). So BS=0 (`sq1-u25/u26`, loaded `ROM_LOAD16_BYTE` at odd offsets in separate 1 MB halves, L1229-1230) plays as **8-bit samples with the low byte = 0**. Nobody has checked whether the real SQ/KS hardware treats u25/u26 as 8-bit, as 12-bit, or as an interleaved 16-bit pair.
- KS-32/SQ-2 BS=1 (`rom2.u39`, `rom3.u38`) uses plain `ROM_LOAD`. The SD-1's equivalent uses `ROM_LOAD16_WORD_SWAP`, so the assumed byte order differs between the two families. This is unverified.
- `sq1`/`sqrack`/`sq2` declare `waverom` with flags 0, not ERASE00 (L1163, L1175, L1187). In debug builds MAME randomizes unloaded bytes, so the low bytes of those samples are garbage.
- Commit #16427 (2026-10-16) replaced a bad VFX/SD-1 wave ROM dump that had every byte offset by 1 and caused noise. Older SD-1 audio comparisons are therefore suspect.

---

## 1. OTIS (ES5505) — es5506.cpp

### 1.1 Voices / sample rate
- 32 voice slots. `generate_samples` loops `v = 0..m_active_voices` (L1054).
- Rate is `m_master_clock / (16*(ACT+1))`, recomputed on any ACT write from the low, high or test page (L1686-1696, L1779-1789, L1828-1838).
- The stream is created at `clock()/(16*32)` (L370). ES5505 `device_start` does **not** set `m_sample_rate` (unlike ES5506 L266-269). It stays 0 until ACT is written or the clock changes.

### 1.2 Accumulator format
- Hardware: 20-bit integer + 9-bit fraction (`ADDRESS_INTEGER_BIT_ES5505=20`, `ADDRESS_FRAC_BIT_ES5505=9`, es5506.h L236-237).
- Internally MAME normalizes everything to an **11-bit fraction** (`ADDRESS_FRAC_BIT=11`, h L43) by left-shifting 2 (`m_address_acc_shift = 11-9`, L502-508). The interpolation fraction therefore has only 9 meaningful bits.
- FC: written bits 15:1 (`data & 0xfefe`), shifted with bias 1 (L1593-1598). The register is 6.9 fixed point, so 0x0400 = 1.0.
- START/END: hi word bits 12:0, lo word bits 15:5 (low 5 bits not writable, L1601-1637). ACC: hi 12:0, lo 15:0 (L1667-1681).

### 1.3 Interpolation: plain 2-point linear
```cpp
// L517-524
inline s32 es550x_device::interpolate(s32 sample1, s32 sample2, u64 accum)
{
	const u32 shifted = 1 << ADDRESS_FRAC_BIT;
	const u32 mask = shifted - 1;
	accum &= mask & m_address_acc_mask;
	return (sample1 * (s32)(shifted - accum) + sample2 * (s32)(accum)) >> ADDRESS_FRAC_BIT;
}
```
- The second sample is always fetched at `int(accum)+1` (L919-920), in both directions. Across a loop end it reads the word after END, not START. This probably matches hardware but nobody has verified it.
- ES5505 has no u-law support: the 5505 calls `generate_pcm` only (L1073-1074). The 5505 CR has no CMPD bit, so this is correct.

### 1.4 Filter
- K1/K2 are 12-bit on the 5505 (writes keep bits 15:4, L1639-1653). The filter uses `cutoff >> FILTER_SHIFT(4)` with `FILTER_BIT = 12` (L107-109).
```cpp
// L534-542
static inline s32 apply_lowpass(s32 out, s32 cutoff, s32 in)
{	return ((s32)(cutoff >> FILTER_SHIFT) * (out - in) / (1 << FILTER_BIT)) + in; }

static inline s32 apply_highpass(s32 out, s32 cutoff, s32 in, s32 prev)
{	return out - prev + ((s32)(cutoff >> FILTER_SHIFT) * in) / (1 << (FILTER_BIT + 1)) + in / 2; }
```
- LP stage: `y[n] = y[n-1] + K/4096 * (x[n] - y[n-1])`.
- HP stage: `y[n] = x[n] - x[n-1] + y[n-1]*(K+4096)/8192`, where x[n-1] is the previous stage's stored n-2 tap.
- Topology (L556-609): poles 1 and 2 are always LP(K1). LP bits (CR 11:10 on the 5505, h L239):
  - 00 = HP(K2), HP(K2)
  - LP3 = LP(K1), HP(K2)
  - LP4 = LP(K2), LP(K2)
  - LP3|LP4 = LP(K1), LP(K2)

  This matches the datasheet table.
- **Precision issues:**
  - The math uses C `/`, which truncates toward zero. Hardware almost certainly shifts, which floors.
  - Filter state is **unbounded s32**: no 16-bit saturation or wrap between stages. The 5505 storage registers are 16-bit, and readback just masks with `& 0xffff` (L1963-1994). An HP stage can produce up to about 2x full scale (`x - x[n-1]`), which the hardware cannot hold.
  - There is a TODO on the 5506 path only: "O4(n-1); TODO: 16.1 signed fixed point according to datasheet" (L1207, L1462). Open PR #16271 adds an extra fraction bit for the 5506 only.
- No K ramping on the 5505. `update_envelopes` for the 5505 just zeroes ecount (L661-665). K1/K2/volume ramps exist only for the 5506 (L618-659). This is correct: OTIS has no hardware ramps, and the firmware steps the registers.
- **Stopped voices are not filtered at all** (the `else` branch at L967-972 skips everything). A Taito F3 hack works around this on O1(n-1) reads (L1983-1993): "they set the accumulator on a stopped voice and assume the filters continue to process the data". That comment implies real silicon keeps running the filter and output of stopped voices, and MAME does not.

### 1.5 Volume
- 8-bit LVOL/RVOL (from the data high byte, L1655-1665). The table uses a 4-bit exponent and 4-bit mantissa (`compute_tables(8,4,4)`, L383).
```cpp
// L477-484
m_volume_lookup[i] = (mantissa << mantissa_shift) >> (exponent_shift - exponent); // (16+m)<<11 >> (16-e)
m_volume_acc_shift = (16 + exponent_mask) - VOLUME_ACC_BIT;                       // = 11
```
- The output is `sample * vol >> 11` (h L107). The table is pre-shifted, so **low exponents lose all mantissa resolution**: e=0 gives 0, e=1 gives 1 for every mantissa, and so on. The hardware probably applies the exponent shift after the multiply. Fades and release tails get coarser steps at the bottom than on the real chip.
- There is no volume ramp or interpolation, so zipper noise follows the firmware's update rate. That is also true on hardware.

### 1.6 Loop / IRQ
- 5505 loop handling (L749-812):
  - Checked **after** output and accumulate. It tests `accum > end` (strict), or `accum < start` in reverse.
  - Uni-loop wrap carries the overshoot: `start + (accum-end)`. Bidirectional loops reflect the overshoot and toggle DIR.
  - BLE without LPE = stop. The comment "ES5505 : BLE is ignored when LPE = 0" is implemented as a stop.
  - IRQ is set if IRQE.
- The `start==end -> stop` special case is deliberately `#if 0` for the 5505 (L1058-1066), because VFX/SD-1 transwaves set start == end and expect the voice to keep running.
- `generate_irq` (L983-1003) latches a voice into IRQV only if the previous IRQ was acknowledged (`m_irqv & 0x80`). It clears that voice's IRQ bit and raises the line. Reading IRQV drops the line (`update_internal_irq_state`, L420-434).
- **Timing:** IRQs are produced only inside `sound_stream_update`, i.e. when the OTIS stream is brought up to date. That happens on every host read/write (`m_stream->update()`, L1858/L2075) or at the sound system's periodic update. The IRQ edge therefore fires late, at the next host access or stream flush, rather than at the sample where the loop actually crossed. The next queued voice IRQ after an acknowledge has the same delay.

### 1.7 Register read-back (L1877-2064)
- CR returns `| 0xf000`. Internally, writes also force bits 15:12 to 1 (L1577), harmless on the 5505.
- FC reads back with bit 0 = 0. START/END/ACC read back live from the internal accumulator.
- K1/K2 return the full 16 bits (low nibble 0). LVOL/RVOL return `vol << 8`.
- O1..O4 return `& 0xffff` of the unbounded s32. On a stopped voice, O1(n-1) is replaced with the raw ROM word (the Taito hack).
- Test page:
  - CH0L..CH3R channel accumulators: **always 0, not implemented** (L2029-2037).
  - SERMODE: stored with `|0x7f8`, but serial format bits (SONY/BB etc.) have no effect on output.
  - PAR: ADC callback `& 0xffc0`.
- `ACT`, `IRQV` (read clears) and `PAGE` are global.

### 1.8 Output mixing / bit depth
- Each voice adds `sample*vol>>11` into an s32 per CA channel. The sum is clamped to 20 bits: `put_int_clamp(c, i, cursample[c], 1<<19)` (L1080-1082), added in PR #14329 and issue #14362.
- In the pump, `s32(stream.get(..) * 32768.0)` (esqpump L74-79) **drops 4 LSBs by truncating toward zero**. The hardware serial port sends the top 16 of 20 bits, which floors. For negative values this is a 1-LSB magnitude-truncation error on everything entering the ESP.
- OTIS -> pump goes through MAME's float stream graph. The rates are equal once ACT is set, but they can differ before that (initial pump rate = 21 voices on `common`, OTIS stream = 32).

### 1.9 TODO/FIXME/hack comments (OTIS)
- L1207 / L1462: `TODO: 16.1 signed fixed point according to datasheet` (5506 filter storage).
- L1058-1066: start==end stop disabled for the 5505 (VFX transwaves).
- L1983-1993: Taito F3 hack, reading O1(n-1) on a stopped voice returns ROM data.
- L105: `RAINE_CHECK 0`, an alternate CR/END write behavior that is disabled.
- L1819: SERMODE MSB[4:0] "unknown purpose".

---

## 2. ESP (ES5510) — cpu/es5510/es5510.cpp

### 2.1 Execution model: per sample, one full program pass
The CPU is disabled (`set_disable`). `esq_5505_5510_pump_device` is a `STREAM_SYNCHRONOUS` stream (esqpump L37) whose callback runs once per output sample:
```cpp
// esqpump.cpp L74-96 (abridged)
m_esp->ser_w(0, s32(stream.get(2, 0) * input_scale));   // ... ser_w(1..5)
if (!m_esp_halted) { m_esp->run_once(); }
sound_stream::sample_t l = sound_stream::sample_t(m_esp->ser_r(6)) * output_scale;
```
```cpp
// es5510.cpp L1090-1107
void es5510_device::run_once()
{
	set_HALT(false); icount = 1; execute_run();   // HALTED -> RUNNING, pc = 0
	set_HALT(true);
	while (state != STATE_HALTED) { icount = 1; execute_run(); }  // until END
}
```
- It runs instructions from PC 0 until an `END` opcode (ALU op 0xF, L981-983, L1221-1244). There is **no cycle budget**: `execute_clocks_to_cycles = clocks/3` (L690-704) is irrelevant here.
- There is **no guard at PC 159**. `pc` is `uint8_t`, and `instr[]` has 160 entries and `gpr[]` 0xC0 (L590-591). A program without END reads out of bounds. Real hardware behavior at PC wrap is undocumented.
- **Batched timing:** "synchronous" only means one callback per sample. The pump still renders whole blocks whenever the sound system updates. `host_w` (GPR/INSTR writes) and ESPHALT (DUART output) do **not** force a pump stream update. Coefficient changes, program uploads and halt/unhalt edges therefore land at block granularity, not at the emulated sample. That can cause glitches on effect changes or partially loaded programs.
- When halted, the pump keeps outputting the **last SER3 values** (stale DC) (esqpump L85-96).
- The ESP's own halt/host-access handshake is bypassed. `run_once` forces HALT on and off around each pass.
- Alternative compile-time paths in esqpump.h L11-14: `PUMP_FAKE_ESP_PROCESSING` (sum the inputs, no ESP) and `PUMP_REPLACE_ESP_PROGRAM` (test program). Both are 0, so the real ESP runs.

### 2.2 Instruction decode (48-bit word, L354-364, L872-1002)
- `[47:40] D reg | [39:32] C reg | [31:24] B reg | [23:16] A reg | [15:12] ALU op | [11:8] operand select | [7] SKIP | [6] MAC accumulate | [5:3] RAM control`.
- The operand-select table (L269-286) chooses register or delay (DIL) as ALU/MAC source and register/DOL/both as destination.
- ALU ops (L247-264): ADD, SUB, ADDU, SUBU, CMP, AND, OR, XOR, ABS, MOV, ASL2, ASL8, LS15, DIFF, ASR, END.
- Skip: `skip = (ccr & cmr & FLAG_MASK) != 0`, inverted if `cmr.NOT` (L876-882). A skipped instruction suppresses MAC and ALU writeback. CMP is never skipped and always updates CCR (L978-979). Non-skippable instructions always update CCR.
- Modeled pipeline (L818-1031):
  - The MAC result of N-1 is written in T1 of N.
  - The ALU result of N-1 is written in T2 of N.
  - A RAM read issued at N lands in DIL at N+2 (`ram_pp`).
  - A RAM write at N-1 pops the 2-deep DOL FIFO.
  - The pipeline state carries across END -> PC 0 and across samples.

### 2.3 ALU precision / saturation (L82-153, L1246-1323)
- 24-bit two's complement with flags N, C, V, LT, Z.
- ADD/SUB **saturate** on V to 0x7FFFFF / 0x800000 (`saturate`, L121-130). ADDU/SUBU wrap.
- ASL2/ASL8 saturate.
- ABS uses **one's complement** (L1291-1292). The comment calls this deliberate.
- LS15 = `(b<<15) & 0x7fffff`. DIFF = `0x7fffff - b`. ASR is arithmetic and does not update Z.
- Several of these were fixed only in Feb 2026 (PR #14979: ABS operand, CMP skip, SIGREG mul shift, CCR/CMR encoding, ASL flags; that fixed SD-1 distortion). PR #15579 (Jun 2026) fixed `WRITE_REG/WRITE_REG16` macros that wrote the wrong variable.

### 2.4 MAC (L887-920)
```cpp
mulacc.product = mul_32x32(util::sext(mulacc.cValue, 24), util::sext(mulacc.dValue, 24)) << mulshift;
mulacc.result  = mulacc.accumulate ? mulacc.product + machl : mulacc.product;
mac_overflow   = (result < -(1<<47) || result >= (1<<47));
machl = mulacc.result;
int32_t tmp = mac_overflow ? (machl < 0 ? 0x00800000 : 0x007fffff)
                           : (mulacc.result & 0x0000ffffff000000ULL) >> 24;
```
- 24x24 multiply, shifted left by `mulshift` = 2 (default) or 1 (SIGREG bit 22, L1182). Accumulated into a "48-bit" MACL/MACH.
- The writeback takes bits 47:24: **truncation, no rounding**.
- Overflow saturates the 24-bit writeback and MACH/MACL reads (L1069-1070).
- `machl` itself keeps the **un-wrapped 64-bit value** after overflow, so it is never clamped or wrapped to 48 bits. Later accumulations continue from an impossible value.
- MACH/MACL host writes splice halves (L1147-1163).
- Open, unresolved accuracy bug: on the SD-1, patches using the **FLANGE+DLY+REV** algorithm (OMNIVERSE, ASCENSION) have a reverb tail that takes about 45 s to decay in MAME versus "a couple of seconds" on hardware. giulioz (PR #15527 discussion, Jun 2026) suspects "some weird data dependency with the MAC preload" and is building a hardware test harness. The cause is **not found**.

### 2.5 Delay line / DRAM (L846-864, L1004-1031, h L17-19, L177-179)
- DRAM is emulated as `int16_t[1<<20]` with a fixed size. `// TODO : Not verified, Most of games are using 128KB DRAM` (h L17), and `// TODO : Masked address?` (h L177). Accesses wrap at 1M words, not at the real installed size, so programs that rely on physical aliasing behave differently.
- Delay addressing: `((dbase + offset) % (dlength + memincrement)) & memmask) >> memshift`. `dbase` drops by `memincrement` at each END and wraps to `dlength` (L1231-1234). Table A/B use base + offset & memmask with no modulo.
- MEMSIZ write: `memshift = count of low 1-bits` (L1164-1171).
- DIL->ALU/MAC is `dil << 8`, so the low 8 bits are 0, as the datasheet says.
- DOL write (24->16) is `dol24 >> 8`, i.e. floor, with `DOL_ROUND_TOWARDS_ZERO 0` (L26-29, L99-119):
  - Round-toward-zero was tried in PR #15527 because the OMNIVERSE tail never reached zero.
  - It was reverted to floor in #15537 after giulioz's **hardware test on a real ES5510 rev 6**: for example, 0xFFFF80 reads back as 0xFFFF00, which proves the chip floors.
  - The patent US5517436 mentions optional "magnitude truncation" (HARD_CONF), but this ESP evidently lacks it.
- **I/O space is not implemented**: reads return 0 (L832-833) and writes are ignored (L1008-1011, "TODO: implement i/o writing").
- Host DRAM access (DADR, L460-470) is triggered by writing the DADR **high byte**. It uses `dadr_latch & DRAM_MASK` **without** the `>> memshift` that program accesses use. This inconsistency could be a bug.
- RAM clear (host control bit 1) happens only when halted and is instant (L476-489, "TODO: Timing, MEMSIZ and DLENGTH behavior").

### 2.6 Serial ports (L558-587, L1061-1068, L1131-1146)
- SER0-3 L/R are **16-bit** latches. Program reads return `value<<8`. Program writes store bits 23:8 (truncated).
- The host serial control register (0x18: master/slave, Sony/I2S, per-port direction) is **stored and ignored** (L496-505).
- Routing is fixed by the pump: SER0-2 in, SER3 out. Input and output are exchanged in the same sample, so there is zero frame latency. Real serial framing would add at least one frame of delay between Aux (dry) and Main (through the ESP).

### 2.7 Host interface (L381-556)
- GPR / INSTR / DIL / DOL / DADR byte latches. `0x80` read-select, `0xa0` GPR write, `0xc0` INSTR write, `0xe0` both.
- Read-select for 0xC0-0xE9 leaves `gpr_latch` stale.
- **Not implemented:**
  - Host Control read (0x12) always returns 0 (L409), so the "host access OK" bit is never visible. PR #7734 had noted that this flag is active-low.
  - The PC register (0x16) returns a constant 0x27.
  - The frame counter (read of 0x1f) returns 0.
  - The DIL latch low byte reads 0. The DOL latch low byte reads 0xff (per the documentation).
- 0x1f halt-enable only does something if the HALT line is asserted.

### 2.8 TODO/FIXME comments (ESP)
- L8-10:
  - "gunlock and clones: Glitch sound after game over once (MT07861)"
  - "DRAM Size isn't verified, differs per machines?"
- L26-28: "Should writes to DOL ... round towards zero? ... may not be what the hardware does." Since settled as floor by the hardware test.
- L56: FIXME about the `stpcpy_int` buffer overrun risk in the disassembly text helpers.
- L458: "TODO: docs says that this always returns 0xff" (DOL latch[0]).
- L480: "TODO: Timing, MEMSIZ and DLENGTH behavior" (RAM clear).
- L1009: "TODO: implement i/o writing."
- h L17: "TODO : Not verified, Most of games are using 128KB DRAM". h L177: "TODO : Masked address?".
- es5510d.cpp: the disassembler is an empty stub, so there is no ESP debugger disassembly.

---

## 3. Known issues / history (GitHub, quick search)

| Ref | Date | Summary |
|---|---|---|
| PR #14329 | 2025-10 | ES5505/6 produce 20-bit samples, not 16. The output scaling was changed. |
| #14362 / PR #14348 | 2025-10 | No output clamping in the ES5505 caused audible glitches on vfx/sd1/sd132. Fixed by clamping. |
| PR #14333 | 2025-10 | OTIS bus 0 routed to a separate Aux stereo out. |
| PR #14979 | 2026-02 | ESP opcode fixes (ABS, CMP-skip, SIGREG mulshift, CCR/CMR, ASL). Fixed SD-1 distortion. The author still notes the OMNIVERSE reverb "linger forever". |
| PR #15527 -> #15537 | 2026-06 | DOL rounding to zero, then reverted to floor after a real-chip test (table in the #15527 comments). **The FLANGE+DLY+REV long-tail bug remains** (about 45 s vs about 2 s on hardware). |
| PR #15579 | 2026-06 | WRITE_REG macros wrote the wrong value. |
| PR #16427 | 2026-10-16 | Bad VFX/SD-1 wave ROM dump (byte offset by 1) caused noise in some patches. Replaced. |
| PR #16271 (open) | 2026-09 | Finer ES5506 filter storage (18-bit). ES5505 unchanged. |
| MT07861 | — | ESP glitch in gunlock (Taito), unresolved. |

---

## 4. Weaknesses ranked by likely audible impact (KS-32/SD-1 context)

**ESP**
1. **Reverb/feedback networks decay wrong.** There is a known unexplained long tail in FLANGE+DLY+REV, suspected to be a MAC/pipeline data-dependency error. It affects every reverb-type algorithm to some degree. High impact.
2. **MAC overflow/precision.** `machl` is never wrapped or clamped to 48 bits after overflow, and the 47:24 writeback truncates. The exact MAC shift semantics (`mulshift`) are only partially verified. Medium-high: affects gain staging and feedback.
3. **Block-granular host timing.** Coefficient/program writes and ESPHALT edges are not synchronized to the pump stream, which can cause glitches on effect changes. When halted, the output freezes at the last value. Medium.
4. **Fixed 1M-word DRAM with no physical aliasing, and no I/O space.** Host DADR access ignores memshift. Low-medium; it matters only if a KS-32 program relies on wrap or I/O.
5. **Serial layer.** Formats are ignored, there is zero frame latency, 16-bit ports are truncated, and the routing is hard-wired from the VFX layout and unverified for KS-32. Low-medium (Aux/Main phase, routing correctness).
6. **Host readback gaps** (host control, frame counter, PC). Low impact on audio, but they could break firmware that polls them.

**OTIS**
1. **Wave data format for KS-32/SQ.** BS0 ROMs play as 8-bit with zero low bytes (no nibble ROM). BS1 byte order differs from the SD-1. If the hardware differs, this is a large timbral or noise error. The machine is flagged NOT_WORKING. High, pending verification.
2. **Stopped voices produce no output and their filters freeze.** The hardware keeps filtering and outputting the held sample (the Taito hack is evidence). This changes tails and causes clicks when a voice stops. Medium.
3. **Filter arithmetic:**
   - Truncating `/` instead of an arithmetic shift.
   - No 16-bit saturation or wrap of the filter state (HP stages can exceed range).
   - Readback masks instead of saturating.

   Medium-low: affects the low-level noise floor, overload character and DC.
4. **Volume table quantization at low exponents.** Coarse steps in fades and releases. Low-medium.
5. **IRQ timing quantized to stream updates.** Loop/transwave IRQs and any voice-timer IRQs are late or jittered. Indirect impact on envelope/LFO timing. Low-medium.
6. **OTIS -> ESP conversion truncates 20->16 bits toward zero** instead of flooring. A 1-LSB magnitude-truncation bias at the ESP input. Low, but relevant to tail behaviour.
7. **Missing read-backs.** Test-page channel accumulators read 0 and SERMODE has no effect. Low for audio.
8. **Linear interpolation is probably faithful**, and the 9-bit fraction is preserved. Not a weakness, listed for completeness.
