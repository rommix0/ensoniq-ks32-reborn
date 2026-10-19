# MAME reference harness (`mame_ref`)

`mame_ref` runs a trace (`docs/trace-format.md`) on MAME's own ES5505 (OTIS) and ES5510 (ESP)
code and writes PCM in the same layout as `ens_trace`. `pcm_diff` then shows where our cores
and MAME disagree on identical input. MAME is a comparison point only: the outputs are not meant
to match, and nothing in `src/` is changed to match them.

## Build and run

The harness is opt-in. The default build does not configure it.

```bash
cmake -S . -B build-ref -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_BUILD_MAME_REF=ON
cmake --build build-ref
build-ref/mame_ref tests/data/smoke.trace mame.pcm
build/ens_trace tests/data/smoke.trace ours.pcm
build/pcm_diff ours.pcm mame.pcm
```

```text
mame_ref <trace> <out.pcm>
```

- The output is 4 little-endian int16 per frame: `auxL auxR mainL mainR`.
- The exit codes match `ens_trace`: 0 ok, 1 for an unreadable file or a trace error
  (`<trace>:<line>: message`), 2 for bad arguments.
- The harness also builds with Visual Studio 2022 x64. The MSVC and MinGW i686 builds give
  byte-identical output on the smoke trace.

## Layout

| Path | Contents |
|---|---|
| `tools/mame_ref/vendor/` | `es5506.cpp/.h` and `es5510.cpp/.h` from MAME, unmodified. They are BSD-3-Clause and keep their license headers. They are compiled with warnings off; nothing else is. |
| `tools/mame_ref/PINNED_COMMIT.txt` | The MAME commit, its date, the source URLs and SHA-256 of the vendored files. |
| `tools/mame_ref/shim/` | Minimal stand-ins for `emu.h`, `logmacro.h`, `es5510d.h`, `corestr.h` and `cpu/m68000/m68000.h`, about 340 lines. |
| `tools/mame_ref/main.cpp` | The CLI. It does what MAME's `esq5505` driver and `esq_5505_5510_pump` device do. |

The harness links only `ens_trace_parse`, our chip-free trace parser. It never links
`ens_otis` or `ens_esp`.

**Pinned commit:** `7e7a99fd7b5a725a5c60ebc632c166cc2c655335` (mamedev/mame master, 2026-10-19).
This is the same file revision as the audit in `docs/reference/mame-otis-esp-audit.md`
(es5506.cpp 2134 lines, es5510.cpp 1323 lines), so the audit's line numbers apply.

## Shim

The vendored files compile without changes. The shim needed far less than the plan's
~800-line limit, so the hand-port fallback was not used. Every per-sample arithmetic path is
MAME's own code.

**What the shim reproduces.** These are copied from MAME at the pinned commit and affect the
output:

| Item | MAME behaviour reproduced | Source |
|---|---|---|
| `sound_stream` | `sample_t = float`. `put_int_clamp(c, i, v, max)` stores `float(double(clamp(v, -max, max-1)) / max)`. | `emu/disound.h` |
| `util::sext` | Same template | `lib/util/coretmpl.h` |
| `mul_32x32` | Same | `osd/eminline.h` |
| `ACCESSING_BITS_0_7`, `ACCESSING_BITS_8_15` | Same | `emu/emumem.h` |
| `BIT` | `(x >> n) & 1` | — |
| Word-addressed ROM space | `memory_access<20,1,-1,BIG>::cache::read_word(a)` returns the word installed by `install_rom` at word address `a`, or 0 when unmapped (MAME's default unmap value, `addrmap.cpp`). | `emu/emumem.h` |

**What is stubbed (no effect on the samples):**

- Save states (`save_item`, `save_pointer`) are no-ops.
- Debugger state registration is a no-op.
- `logerror` and the `LOG` macros are compiled but discarded.
- Callbacks are unbound: IRQ out, sample-rate-changed and the PAR port read. An unbound PAR
  read returns 0, which is MAME's `isunset()` path.
- `set_sample_rate` and `sound_stream::update()` are no-ops. The harness renders exactly one
  sample per frame, and all host writes fall between frames.
- `string_format` returns its format string unformatted. Only `es5510_device::list_program`
  uses it, and the harness never calls that debug listing.
- `es5510d.h` is a stub disassembler. MAME's own `es5510d.cpp` is also a stub.
- `device_t::start()` and `reset()` call `device_start()` and `device_reset()` in MAME's order:
  every start, then every reset.

**Assumed and not modelled:** MAME's sound graph between OTIS and the pump. The harness assumes
the following, which hold for KS-32/`common32`:

- The two stream rates are equal (both `clock/(16*32)` before ACT is written, and the pump
  follows ACT through `sample_rate_changed`).
- The route gain is 1.0.
- The graph adds no resampling and no latency.

**Host timing is sample-exact.** The harness applies every host write and ESPHALT edge exactly
at the frame boundary where the trace puts it. Real MAME applies them at audio-block
granularity, because `host_w` and the DUART output do not force a pump stream update (audit
§2.1, ESP weakness 3). The output is therefore "sample-exact MAME", not what a MAME session
would play.

## How the trace conventions map onto MAME

| Trace / ruling | Our runner (`src/trace/trace.cpp`) | `mame_ref` |
|---|---|---|
| Reset | ESP running, HALT pin low, memories zero | `device_start` then `device_reset` of both devices. The pump flag "ESP halted" starts **false**. esqpump's constructor default is `true`; on hardware the firmware's DUART write sets it. MAME's OTIS reset differs from O10: LVOL/RVOL start at 0x80, not 0. |
| `wave <bank> <path>` | `WaveMemory` image | The image becomes MAME ROM region 0 or 1, installed with `install_rom(0, words-1)`. Word *i* of the file is OTIS word address *i*. **No byte-order adaptation**: the little-endian file is decoded to host-order 16-bit words, which is how MAME holds a 16-bit region. A bank with no `wave` is unmapped and reads 0, as in ours. The audit §0 ROM-format questions (KS-32 BS0 8-bit, BS1 byte order) are bypassed, because the trace supplies the words directly. |
| `dram <words>` | ESP DRAM size, addresses masked | **Ignored.** MAME's DRAM is fixed at 1M words (`es5510.h` `DRAM_SIZE`, audit §2.5). |
| `otis <reg> <v>` | `Otis::write` | `es5505_device::write(reg, v, 0xffff)`. MAME's own `m_stream->update()` inside `write` is a no-op here, because the write already falls between samples. |
| `esp <addr> <v>` | `Esp::hostWrite`, then one `idleStep()` while halted (R15) | `es5510_device::host_w(addr, v)` only. MAME applies host writes at once: select-register transfers (`$80/$A0/$C0/$E0`) and DADR RAM accesses complete inside `host_w`. MAME has no idle-step counterpart. |
| `esphalt 1` (TR2) | Pin high. If the ESP is running, also write `$1F`, so it halts immediately. | Sets the pump's halted flag, which is what esq5505's `duart_output` ESPHALT does. `run_once()` is atomic, so MAME can never stop mid-program and the halt is always immediate. If the ESP was not halted, `host_w(0x1F, 0xFF)` is also passed. That write is a no-op in MAME here: the ESP is already HALTED between `run_once()` calls (and RUNNING with its HALT line low before the first one, where `$1F` does nothing either). It is kept only so that MAME sees the same host write sequence. |
| `esphalt 0` | Pin low; resumes at word 0 | Clears the pump flag. The next `run_once()` starts at PC 0. |
| `espidle <n>` | `n` idle steps (host transfers, RAM clear, DBASE refresh) | **No-op.** MAME has no halted-state cycles. RAM clear (`$12` bit 1) is instant but only when the ESP's state is HALTED (audit §2.5), and DBASE does not move while halted. |
| `frames <n>` (TR1) | `Otis::runFrame`, route, `Esp::runSample` | One OTIS stream update of one sample, then esqpump's `sound_stream_update`. `ser_w(n, s32(get(2+n) * 32768.0f))` for n = 0..5, i.e. OTIS ch1..ch3 to SER0..SER2 (L, R). Then `run_once()` unless halted, then main = `ser_r(6)`, `ser_r(7)` (SER3 L/R). Aux = OTIS ch0. **Program without END:** before each `run_once()` the harness checks MAME's own program memory (the public `_instr()` accessor). If none of the 160 words has ALU opcode END, it skips `run_once()` for that frame, treating the ESP as halted, and prints one warning on stderr: `ESP program has no END; MAME would read out of bounds (audit section 2.1) - ESP treated as halted`. The exit status stays 0. Without this, MAME's `run_once()` runs past `instr[159]` and crashes. This happens with any trace that runs frames before loading a program, because the reset program is all zeros. MAME itself only escapes because esqpump starts halted. |
| OTIS output word | O6/O12: bits 15..0 of the 21-bit sum, saturated (floor of the 20-bit value / 16) | `put_int_clamp` to 20 bits, then esqpump's `s32(float * 32768)`, i.e. **truncation toward zero** of the 20-bit value / 16. This is reproduced as MAME does it, not fixed (audit §1.8). MAME's aux output stays float all the way to the speaker; the harness converts it with the same truncating expression the pump uses for the ESP inputs. |
| Main output word | SER3 latch | `ser_r(6/7)`. The pump's `* (1/32768)` to the speaker and back is exact, so the latch is used directly. |

## Differences on `tests/data/smoke.trace`

The smoke trace has two looped sine voices. Voice 0 is on CA 0 (aux) and voice 1 on CA 1
(through the ESP's SER0 to SER3 copy to main). Both use 4 low-pass poles with K1 = K2 = 4095.

`pcm_diff ours.pcm mame.pcm` reports:

```text
frames: a=64 b=64
first differing frame: 6
ch0: max|diff|=3 rms=1.7854
ch1: max|diff|=2 rms=1.1859
ch2: max|diff|=3 rms=1.6863
ch3: max|diff|=2 rms=0.8101
DIFFERENT
```

Both files have 64 frames. 154 of the 256 samples differ, by 1 to 3 LSB. Every difference has
ours below MAME (ours − MAME ∈ {−1, −2, −3}). Both mechanisms below round negative quantities
toward zero in MAME and toward −∞ in ours.

To attribute them, `tools/mame_ref/analysis/smoke_model.py` models this trace's OTIS path with
each rounding choice switchable. It is pure Python:

```bash
python -I tools/mame_ref/analysis/smoke_model.py tests/data/smoke_wave.bin ours.pcm mame.pcm
```

It reproduces both files exactly: 0 mismatches against `ours.pcm` with floor everywhere, and 0
against `mame.pcm` with MAME's truncations. Switching one knob at a time from our model gives
the split below. "Samples" counts the frames where that output differs.

| Output | Samples differing (of 64) | Max \|diff\| | Cause | Label |
|---|---|---|---|---|
| aux L (OTIS ch0, voice 0) | 43 | 3 | Filter rounding (32 samples, up to 2 LSB) plus output-word rounding (24 samples, 1 LSB) | Filter: **O4** (floor `>> 12`) vs MAME's truncating `/ 4096` in `apply_lowpass` (audit §1.4 "Precision issues", OTIS weakness 3). Output word: **O5/O12** (ours floors at the volume shift and emits that 16-bit word) vs truncation toward zero. On aux this truncation is a **harness conversion (mirrors esqpump `s32`)**, because MAME's aux stays float. |
| aux R (OTIS ch0, voice 0) | 45 | 2 | Same two causes (filter 32 samples ≤1; output 28 samples ≤1) | **O4** vs audit §1.4; **O5/O12** vs **harness conversion (mirrors esqpump `s32`)** |
| main L (ESP SER3 ← SER0 ← OTIS ch1, voice 1) | 39 | 3 | Same two causes on voice 1 (filter 31 samples ≤2; output 18 samples ≤1). The ESP copy is exact in both, so it adds nothing. | **O4** vs audit §1.4; **O5/O12** vs esqpump's `s32()` truncation of the ESP input (audit §1.8, OTIS weakness 6) |
| main R (same path) | 27 | 2 | Same (filter 13 samples ≤1; output 20 samples ≤1) | **O4** vs audit §1.4; **O5/O12** vs audit §1.8 |

No difference is unexplained.

These parts of the path agree exactly on this trace:

- **O3 interpolation.** MAME's `(s1*(2048-a) + s2*a) >> 11` with the low 2 fraction bits always 0
  equals our floor form.
- **O5 volume.** The smoke volumes have mantissa 0 and exponent 13 to 15, where MAME's
  pre-shifted volume table is exact.
- **O1 loop.** MAME wraps on `acc > END`, ours on `n >= END`. MAME's loop is therefore one
  sample longer whenever a step lands exactly on END. Voice 0 does land on END at frame 64, but
  the smoke wave has word 64 equal to word 0, so the extra sample is invisible. Running the trace
  for 80 frames shows no loop-related difference.
- **TR1/TR2 conventions.** The ESP program load, halt and resume give identical main output
  wherever the OTIS input agrees.

## Differences on `tests/data/esp_mix.trace`

This trace runs the ES5510 paths that the smoke trace's two MOVs do not. Its 13-word program,
hand-encoded and checked with `esp_dis`, does the following:

- **MUL**: SER1L × −0.75, with SIGREG bit 22 set so the multiplier shift is 1.
- **ADD**: adds SER0L to give `mix`, and pushes `mix` to the DOL.
- **Delay line through DRAM**: MEMSIZ 0xFF, DLENGTH 0xFF00. `mix` is written at DBASE + 0 and
  read back two samples later through DIL into SER3L.
- **Saturating ADD**: `0x7F0000 + mix`.
- **Conditional skip**: CMP `mix`, 0, then a NOP, then a skippable MOV with CMR = N. SER3R is the
  saturated sum when `mix` ≥ 0 and `mix` otherwise.

The input waves are synthetic non-decreasing ramps (`esp_mix_fast.bin`, `esp_mix_slow.bin`,
written by `tools/mame_ref/analysis/make_esp_mix_waves.py`). With only non-negative operands,
our floors and MAME's truncations agree, so the OTIS outputs, and with them the ESP inputs, are
the same in both. Voices 2 and 3 copy the two ESP inputs onto CA 0, so aux L and aux R show this
directly. Any main-output difference is therefore the ESP's own.

Over the 96 frames, `mix` runs negative, turns positive at frame 73 and then makes the ADD
saturate (SER3R 32636, 32761, then 32767).

```text
frames: a=96 b=96
first differing frame: none
ch0: max|diff|=0 rms=0.0000
ch1: max|diff|=0 rms=0.0000
ch2: max|diff|=0 rms=0.0000
ch3: max|diff|=0 rms=0.0000
identical
```

| Output | Samples differing | Label |
|---|---|---|
| aux L/R (ESP inputs) | 0 | — (OTIS identical by construction) |
| main L (MUL, ADD, DOL, delay write/read, DIL) | 0 | — |
| main R (saturating ADD, CMP, skip) | 0 | — |

On these paths, MAME and our core agree bit for bit:

- MUL truncation, with E6 and MAME both taking bits 47..24 rounded toward −∞.
- ADD saturation.
- DOL floor (E18 FIFO part, and MAME after PR #15537).
- Delay addressing with MEMSIZ 0xFF and DLENGTH 0xFF00, including the first DBASE reload (E7/R10).
- The DIL timing of two words (E18).
- The CMP/skip formula (E21, E4).

The program deliberately avoids the following, which do differ (see the next table):

- A skippable word directly after the CMP.
- A one-operand op with a DIL operand select. The DIL is read as register 244 instead.
- Any MAC overflow.

An exploratory variant of this program, run on the smoke sines instead of ramps, showed how the
OTIS rounding differences above propagate. The input differences of a few LSB carry through the MUL and ADD (up to 6 LSB on main L)
and are not amplified by them. But where `mix` is −2 in ours and 0 in MAME the conditional skip goes the other way,
and that one frame differs by 32514 on SER3R. Data-dependent ESP code can turn an LSB-level OTIS
difference into a full-scale one.

## Differences to expect on other traces

The two committed traces do not exercise these. They are listed so future diffs can be labelled
quickly.

| Area | Ours | MAME | Label |
|---|---|---|---|
| Loop boundary | Wrap at `n >= END` / `n <= START` | Wrap at `acc > END` / `acc < START` (one sample later on an exact hit) | **O1** vs audit §1.6 |
| Stopped voices | Keep filtering, applying volume and outputting (KS-32 relies on it) | Produce nothing; filters frozen; Taito O1(n−1) read hack | **O7** vs audit §1.4 / OTIS weakness 2 |
| Filter state | 16-bit, saturated per pole | Unbounded `s32`, HP up to about 2x full scale | **O4** vs audit §1.4 |
| Low volume exponents | Shift after the multiply | Pre-shifted table loses the mantissa at low exponents | **O5** vs audit §1.5 / OTIS weakness 4 |
| Several voices on one channel | Per-voice floor, then 21-bit sum, then O6 saturation | 20-bit sum, clamp, one truncation | **O5/O6** vs audit §1.8 |
| Reset LVOL/RVOL | 0 | 0x80 | **O10** |
| CA 3 with SERMODE A/D = 1 | CH3 is an input, so the ESP gets 0 | Always routed to SER2 (SERMODE ignored) | **TR1** note / audit §1.7 |
| ESP MAC overflow | 50-bit wrap, sat48 readout | `machl` unwrapped 64-bit | **E6** vs audit §2.4 / ESP weakness 2 |
| ESP DRAM size | Trace `dram`, addresses masked | Fixed 1M words | audit §2.5 / ESP weakness 4 |
| Halted ESP | Idle steps run transfers, RAM clear and the DBASE refresh model | Instant transfers; RAM clear only when HALTED; DBASE frozen | **TR2/E25/E28** |
| Program without END | Word 159 treated as END | Real MAME reads past `instr[159]` (undefined, crashes). The harness instead skips `run_once()` for the frame, treats the ESP as halted (main keeps the last SER3 value) and warns once on stderr. | **E11** vs audit §2.1 |
| Host writes while the ESP runs | At most one GPR/INSTR select transfer per END (E19) | Every `host_w` applies at once, between `run_once()` calls | **E19/TR2** |
| Skippable word directly after the CMP/ALU word that sets the flags | Skip uses the CCR committed in the same step (E17: commit ALU(k−1), then evaluate skip k) | Skip is evaluated at T1, before ALU(N−1) writes CCR at T2, so it sees the flags one word older | **E17** vs es5510.cpp `execute_run` |
| One-operand ALU op (MOV, ABS, …) with an operand select of 9..F (ALU source = DIL) | DIL replaces only operand A, so one-operand ops still read B (ESP notes §4.1.3) | The single operand reads DIL | ESP notes part 1 §4.1.3 (no decision id) |
| ALU flags and skip | E1–E21 | es5510.cpp `alu_operation` / skip test | The E-id that the differing instruction exercises |

## Differences on `tests/data/reverb_tank.trace`

The Task 13 reverb (four lattice allpasses and a feedback-0.7 tank, impulse from OTIS) decays
to −60 dBFS in 1.3 s in both. Main L/R differ by at most 5 LSB, and only because the OTIS
outputs feeding the ESP differ (O4/O5 rounding, MAME's non-decaying truncated filter residue,
O7). Fed MAME's ESP input, our ESP core reproduces MAME's main output exactly. See
`docs/verification-plan1.md`.
