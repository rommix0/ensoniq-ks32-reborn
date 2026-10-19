# OTIS + ESP Chip Cores Implementation Plan (Plan 1 of 3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build datasheet-accurate, integer-only C++17 emulators of the ES5505 (OTIS) and ES5510 (ESP). Each comes with:
- rule tests
- an ESP encoder/disassembler
- a trace-driven runner
- a MAME reference harness that diffs our output against MAME's chip code

**Architecture:** There are two static libraries, `ens_otis` and `ens_esp`. They have no OS calls, no globals and no file I/O; each chip is a plain class that talks to memory through an interface the board supplies. The command-line tools sit in `tools/` and the tests in `tests/`. Every behaviour the datasheet leaves open is implemented using the defaults in the **Decision Defaults** table below, and each one is recorded in `docs/decisions.md`.

**Tech Stack:** C++17 and CMake 3.20+. A small in-house test harness (`tests/check.h`), with no third-party libraries. It builds with MSVC 2022 x64 and with MinGW g++ 14 (i686) plus Ninja.

**Spec:** `docs/design/2026-10-19-otis-esp-emulator-design.md`. The datasheet facts are in:
- `docs/reference/es5505-otis-datasheet-notes.md` ("OTIS notes")
- `docs/reference/es5510-esp-datasheet-notes-part1.md` and `-part2.md` ("ESP notes")
- `docs/reference/mame-otis-esp-audit.md` ("MAME audit")

**Scope.** This is Plan 1 of 3. Plan 2 investigates the ROMs: wave format, sample map, ESP programs, clocks, routing and DRAM size. Plan 3 builds the MIDI test player and `play`/`render` app. Plans 2 and 3 are written after this one lands. Plan 2 only needs Task 10 (the disassembler) from this plan.

## Global Constraints

- C++17. No third-party libraries anywhere in the repository.
- Chip code (`src/`) has no OS calls, no threads, no file I/O and no mutable globals or statics. Each chip is an instantiable class.
- Chip code is integer-only: no `float` or `double` in `src/otis` or `src/esp`. Chip-rate output must be bit-identical across the MSVC x64 and MinGW i686 builds.
- Must build with Visual Studio 2022 x64 (`-G "Visual Studio 17 2022" -A x64`) and with MinGW g++ 14 plus Ninja.
- The datasheets are the reference. MAME is a comparison point only. No behaviour is copied from MAME just to match it.
- Every behaviour the datasheets leave open is marked at the point of use with a comment `// DECISION <id>: <one line>`, and has an entry in `docs/decisions.md` giving the options, the choice, the evidence and the confidence.
- ROM files (`ks-32/`, `Ensoniq-KS-32-*`) and PDFs (`datasheets/`) are git-ignored and never committed.

## Decision Defaults

Implement these. Each task that implements one also writes its `docs/decisions.md` entry, keyed by id. "Evidence" points into the reference notes.

| Id | Topic | Default | Evidence / confidence |
|---|---|---|---|
| O1 | Loop arithmetic | `step = FC>>1` (units of 1/512 sample). Forward: `n = acc + step`; boundary hit when `n >= END29`. Reverse (DIR=1): `n = acc - step`; boundary hit when `n <= START29`. At a boundary: LPE=0 → `STOP0=1`, `acc = boundary`. LPE=1, BLE=0 → `acc = other_boundary ± overshoot` (forward: `START29 + (n-END29)`; reverse: `END29 - (START29-n)`). LPE=1, BLE=1 → reflect (`acc = END29 - (n-END29)` and set DIR, or the mirror image) and toggle DIR. `START29 = (STRT_H<<16 \| STRT_L) & 0x1FFFFFE0` (24-bit position, low 5 fraction bits 0); END29 likewise. | OTIS notes §6.1. The text adds FC twice; read as single-add. Medium. |
| O2 | S2 fetch address | `int + 1` in both directions, same bank. Wraps within 20 bits. | §6.1 step 2. Medium. |
| O3 | Interpolation | `SF = S1 + (((S2 - S1) * frac9) >> 9)`, arithmetic shift (floor). | §6.2. Rounding unstated. Medium. |
| O4 | Filter arithmetic | 16-bit state, each pole output saturated to int16. LP: `y = y1 + ((K12 * (x - y1)) >> 12)`. HP: `y = x - x1 + (((4096 + K12) * y1) >> 13)`. K12 = register bits 15:4. The same register is read as LP or HP by the pole's mode. | §6.3 (scales ×4096 LP, (K−0.5)×8192 HP). Saturation unstated. Medium. |
| O5 | Volume | `p = (SF * (16 + mant)) >> 5`, then `exp==0 ? 0 : (int16)p >> (15 - exp)`. | §6.4 table. High for the shift; rounding is floor. |
| O6 | Channel sum | 21-bit accumulators per CH[L/R]. On output: if bits 20..15 are not all equal, saturate to 0x7FFF or 0x8000 by bit 20; else take bits 15..0. Cleared each frame. | §4.9 SERMODE MSB. High. |
| O7 | Stopped voice | STOP0/STOP1 freeze only the address ALU (accumulator and DIR). The voice still fetches, interpolates, filters (updating O1(n−1)…O4), applies volume and accumulates. | CR STOP1 text; MAME audit §OTIS-2 (Taito hack); **KS-32 firmware depends on it**: it reads page\|0x20 reg 6 (O1(n−1)) of a stopped voice to find zero crossings (`docs/rom-investigation/findings.md`, "Other findings"). High. |
| O8 | IRQ/IRQV | Use the model in OTIS notes §7, steps 1-5. IRQV read: `0x60 \| (pending?0:0x80) \| vec`; bits 6:5 read as 1. The read sets the "vector free" state. The serviced voice's CR.IRQ is cleared on its next slot after the read. | §7. Bits 6:5 unknown; low confidence. |
| O9 | Unused/reserved bits | Reads: CR returns `0xF000 \| cr12`; SERMODE `0x07F8 \| bits`; PAGE 7 bits; unlisted registers read 0 and ignore writes. | §4.5. Medium. |
| O10 | Reset state | All voice registers 0 except CR = `STOP1\|STOP0` (0x0003). ACT=31, PAGE=0, IRQV free (bit7=1), SERMODE=0x0001 (A/D=1). | Only A/D is documented. Low. |
| O11 | Channel register reads | CH0–CH2 reads return 0 (write-only for test). CH3L/R read back their last latched output or input value. | §4.9. Medium. |
| O12 | Output width | Per-channel serial output is 16-bit signed after O6 (the datasheet's serial word). It is not 20-bit as in MAME PR #14329. | §5.4. High. |
| E1 | Carry polarity | ADD/ADDU: C = unsigned carry out of bit 23. SUB/SUBU/CMP/DIFF: C = unsigned borrow (1 when A < B). This makes the LO/HS masks mean what their names say. | ESP notes §3.3.2 conflict. Medium. |
| E2 | DIFF | Unsaturated: `A = (0x7FFFFF - B) & 0xFFFFFF`. V = signed overflow. | Text over table. Medium. |
| E3 | ABS | Negative → one's complement (`~B`). N forced 0, C = sign of B, V/LT/Z unchanged. | §3.3.1 and the flag table. Medium-high. |
| E4 | CMP and skip | CMP ignores the Skip bit: it always evaluates and always sets the CCR. Other instructions with Skip=1 never set the CCR. | §3.3.1 and §4.2.1; agrees with MAME after PR #14979. Medium. |
| E5 | What a skip suppresses | Suppresses ALU and MUL register writes, DOL pushes and the accumulator update. The RAM-control action of that instruction word still happens. | §4.2.1 "maintains consistency of pipelines". Medium. |
| E6 | MAC model | `acc` is a 50-bit signed value (48 + 2 guard) that wraps at 50 bits. `product = sext24(X) * sext24(Y) << (SIGREG bit22 ? 1 : 2)`. Readout `sat48(acc)` clamps to [−2^47, 2^47−1]. MACH = sat48>>24, MACL = sat48 & 0xFFFFFF. Z-bus result = MACH. ALU write MACH: `acc = (sext24(v)<<24) \| (acc & 0xFFFFFF)`. ALU write MACL: `acc = (acc & ~0xFFFFFF) \| v`. | §3.4 and §4.3.3. The "MACL dest duplicates upper half" sentence is read as concerning the multiplier C-destination and not implemented. Low-medium. |
| E7 | AGEN units | The 20-bit AGEN registers (DLENGTH, ABASE, BBASE, DBASE, MEMSIZ) hold 24-bit-aligned values. Writes keep bits 23..4 and reads return low 4 bits 0; MEMSIZ keeps its written value. `unit = MEMSIZ + 1`, `shift = count of trailing 1 bits of MEMSIZ`. Delay: `s = (DBASE + off) & 0xFFFFFF`; if `s > DLENGTH` then `s -= DLENGTH + unit`; `phys = (s & ~MEMSIZ & 0xFFFFFF) >> shift`. Table: `phys = ((BASE + off) & 0xFFFFFF & ~MEMSIZ) >> shift`. I/O: `phys = (off & 0xFFFFFF) >> 4`. | §3.5.2-3.5.4. The wrap subtracts DLENGTH+unit so the buffer stays circular with DBASE's period; the text says DLENGTH. MAME does the same. Medium. |
| E8 | SER registers | 16-bit, left-justified at bits 23..8. A program read returns `v<<8`; a program write stores bits 23..8. | Same convention as DIL. Medium. |
| E9 | Addresses 192..233 | Read 0; writes ignored. | Undocumented. Low. |
| E10 | Host DOL low byte | Reads 0x00 (the text), not 0xFF (the table). | Low. |
| E11 | Program without END | After executing word 159, behave as if word 159 were END. | Undocumented. Low. |
| E12 | Constants 252..255 | Read −1, 0x800000, 0x7FFFFF, 0. Writes discarded. | §3.2. High. |
| E13 | ASL2/ASL8 | `A = B << n`. If the bits shifted out plus the new bit 23 are not all equal to the old bit 23, saturate to 0x7FFFFF/0x800000 by the old sign and set V=1, else V=0. N and Z are set from the result. C and LT are unchanged. | Flag table. Medium-high. |
| E14 | LS15 | `A = (B << 15) & 0x7FFFFF`. N=0. C = bit 8 of B (the bit that would land at bit 23). Z, V and LT are unchanged. | Flag table note (3). Medium. |
| E15 | ASR / logic flags | ASR: `A = B >> 1` (arithmetic); N from the result; C = bit 0 of B; Z, V and LT unchanged. AND/OR/XOR: N and Z from the result; C, V and LT unchanged. | Flag table. Medium. |
| E16 | ADD/SUB flags | N and Z from the stored (saturated) result. V = saturation occurred. LT = sign of the infinite-precision result (equals N_wrapped XOR V_overflow). C as in E1. ADDU/SUBU/CMP/DIFF: N and Z from the wrapped result, V = signed overflow, LT = N XOR V. | §3.3.2 and §3.3.3. Medium. |
| E17 | Execution order within step k | (1) Commit MUL(k−1)'s Z result (C register and/or DOL push). (2) MUL(k) reads X/Y operands and forms the product. (3) Commit ALU(k−1): register write, DOL push, CCR, MACH/MACL preload. (4) Evaluate the skip for step k from CCR/CMR. (5) ALU(k) reads operands and computes; the result is pending. (6) MUL(k) accumulates: `acc = (MAC ? acc : 0) + product`, unless skipped. (7) RAM cycle for the word issued at step k−1 (see E18). (8) The AGEN computes the address for word k from `GPR[k]` and RAM-ctrl(k). | §4.3.1–4.3.3 timing figure. High for ordering; medium for the MAC preload slot. |
| E18 | RAM pipeline | At step k, RAM-ctrl and address of word k−1 run. Read: `DIL = mem << 8` at the end of step k, visible to operand reads from step k+1, i.e. two words after the issuing word. Write: pop the DOL FIFO (from empty, re-use the last pushed value) and write. Dump: pop and discard. The pipeline carries across END into the next sample. An END taken while HALT is high empties the FIFO. | §4.3.4 and §3.6.3. Medium. |
| E19 | END step | The END word executes like a MOV-free ALU NOP; its MUL and RAM fields still execute. After step (8) of the END step: DBASE −= unit, reloading DLENGTH when it goes below 0. Then perform ≤1 pending host GPR/INSTR transfer and sample HALT. The next step is PC 0. | §5.2.1. Medium. |
| E20 | MEMSIZ write side effect | Writing MEMSIZ also sets `DBASE &= ~MEMSIZ`. | "Writing MEMSIZ affects DBASE directly". Low. |
| E21 | Skip formula vs. TRUE/FALSE labels | Implement the literal hardware equation `SKIP = NOTm xor ((N&Nm)\|(Z&Zm)\|(V&Vm)\|(C&Cm)\|(LT&LTm))`. Under it the table's "TRUE" mask (0x04) skips always and "FALSE" (0x00) never skips. The other 14 masks behave as "execute when the condition holds". | ESP notes §4.2.1 vs §4.2.2. The labels conflict with the equation; the equation wins. Medium. |

## Review Focus

These are the inputs most likely to break a user and that no task's main tests target; each is pinned by a test in the task named.

1. **A host writes OTIS registers in the middle of a frame.** Voices processed after the write must see it; earlier voices must not. Pinned in Task 4: `otis_midframe_write_affects_later_voices_only`.
2. **An ESP program that never reaches END,** or a mangled instruction word. It must not hang or read out of bounds. Pinned in Task 8: `esp_runsample_terminates_without_end`.
3. **An ESP feedback loop that overflows.** For example a reverb with gain > 1: it must clip, not wrap and not run away. This targets MAME bug #1/#2. Pinned in Task 8: `esp_feedback_loop_saturates_not_wraps`.
4. **ACT set very low** (fewer than 8 voices, which the datasheet says starves the serial port). The runner must still produce frames; record it as a decision. Pinned in Task 4: `otis_act_below_8_still_produces_frames`.
5. **Corrupt or truncated trace files given to the tools.** They must report a line-numbered error and exit non-zero, never crash. Pinned in Task 11: `trace_parse_error_reports_line`.

---

## File Structure

```
CMakeLists.txt                     root: options, warnings, subdirs, ctest
.gitignore                         ignores build*/, ks-32/, Ensoniq-KS-32-*/, datasheets/, *.wav
src/common/bits.h                  sext(), sat24(), sat16(), mask helpers (header-only)
src/otis/otis.h, otis.cpp          ens::Otis + ens::WaveMemory + ens::OtisFrame
src/esp/esp_alu.h, esp_alu.cpp     ens::esp::alu() pure function + Flags
src/esp/esp_mac.h, esp_mac.cpp     ens::esp::Mac
src/esp/esp_agen.h, esp_agen.cpp   ens::esp::Agen + ens::esp::DolFifo
src/esp/esp.h, esp.cpp             ens::Esp core (execution, SPRs, serial, host interface)
src/esp/esp_isa.h, esp_isa.cpp     ens::esp::Instr encode/decode + disassemble()
src/trace/trace.h, trace.cpp       ens::Trace parser + ens::TraceRunner (drives Otis+Esp)
tools/ens_trace/main.cpp           CLI: run a trace on our chips -> raw PCM
tools/esp_dis/main.cpp             CLI: disassemble a raw ESP program image
tools/pcm_diff/main.cpp            CLI: compare two raw PCM files, report differences
tools/mame_ref/                    vendored MAME chip sources + shim + CLI (separate CMake target, opt-in)
tests/check.h, tests/main.cpp      tiny test registry and runner
tests/test_bits.cpp, test_otis_*.cpp, test_esp_*.cpp, test_trace.cpp
docs/decisions.md                  decision log (ids from the table above)
```

---

### Task 1: Repository scaffold, test harness, bit helpers

**Files:**
- Create: `CMakeLists.txt`, `.gitignore`, `src/common/bits.h`, `tests/check.h`, `tests/main.cpp`, `tests/test_bits.cpp`, `docs/decisions.md`

**Interfaces:**
- Produces:
  - `namespace ens`:
    - `int32_t sext(uint32_t v, int bits)`
    - `int32_t sat(int64_t v, int bits)` (clamps to a signed `bits`-wide range)
    - `uint32_t mask24(int64_t v)`
  - `tests/check.h`:
    - `TEST(name)` registers a function
    - `CHECK(cond)`
    - `CHECK_EQ(a, b)` prints both values on failure, as hex for integers
  - Test executable `ens_tests`, registered with ctest as `ens_tests`. `ens_tests <substring>` runs only the matching tests.
  - CMake targets `ens_otis` and `ens_esp`, initially empty static libraries that link `src/common`.

- [ ] **Step 1:** Run `git init` in the project root. Write `.gitignore` with: `build*/`, `ks-32/`, `Ensoniq-KS-32-V3.01-Bin-And-Hex/`, `datasheets/`, `*.wav`, `*.pcm`.
- [ ] **Step 2: Write the failing tests** in `tests/test_bits.cpp`:
  - `bits_sext`: `CHECK_EQ(sext(0x800000,24), -8388608); CHECK_EQ(sext(0x7FFFFF,24), 8388607); CHECK_EQ(sext(0x1F,5), -1);`
  - `bits_sat`: `CHECK_EQ(sat(40000,16), 32767); CHECK_EQ(sat(-40000,16), -32768); CHECK_EQ(sat(1LL<<30,24), 0x7FFFFF);`
  - `bits_mask24`: `CHECK_EQ(mask24(-1), 0xFFFFFFu);`
- [ ] **Step 3:** Configure and build:
  ```bash
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  ```
  Expected: compile errors (`sext` is not declared).
- [ ] **Step 4:** Implement `bits.h` (header-only), `check.h` and `main.cpp`. The runner exits non-zero on any failure and prints `N passed, M failed`.

  Root `CMakeLists.txt`:
  - `cmake_minimum_required(3.20)`, `CMAKE_CXX_STANDARD 17`
  - warnings `-Wall -Wextra` or `/W4`
  - `enable_testing()`
  - option `ENS_BUILD_MAME_REF` (default OFF)
- [ ] **Step 5:** Build, then run:
  ```bash
  ctest --test-dir build --output-on-failure
  ```
  Expected: `bits_*` pass.
- [ ] **Step 6:** Create `docs/decisions.md` with a header, the entry template (`## <Id>: <title>` / Options / Choice / Evidence / Confidence / Status) and empty sections "OTIS" and "ESP".
- [ ] **Step 7:** Commit:
  ```bash
  git add -A
  git commit -m "chore: scaffold CMake project, test harness, bit helpers"
  ```

---

### Task 2: OTIS host register interface and paging

**Files:**
- Create: `src/otis/otis.h`, `src/otis/otis.cpp`, `tests/test_otis_regs.cpp`
- Modify: `CMakeLists.txt` (add the sources to `ens_otis`), `docs/decisions.md` (O9, O10, O11)

**Interfaces:**
- Produces (`namespace ens`):

```cpp
struct WaveMemory { virtual ~WaveMemory() = default;
                    virtual uint16_t read(int bank, uint32_t wordAddr) = 0; }; // bank 0/1, addr 20-bit
struct OtisFrame  { int16_t ch[4][2]; };            // [channel][0=L,1=R]
class Otis {
public:
  explicit Otis(WaveMemory& mem);
  void     reset();
  uint16_t read(uint8_t reg);                       // reg = A3..A0 (0..15)
  void     write(uint8_t reg, uint16_t data);
  bool     irqAsserted() const;                     // true = IRQ pin pulled low
  int      activeVoices() const;                    // ACT+1
  // Task 3/4 add: void stepVoice(); bool frameReady() const; OtisFrame takeFrame(); OtisFrame runFrame();
  void     setSerialIn(int16_t l, int16_t r);       // SER3 input when SERMODE.A/D=1 (latched into CH3L/R)
};
```

  Register map: OTIS notes §4.2–§4.5 exactly. Page bits: 0–31 voice, 32–63 filter storage, 64–127 global channel/serial. Regs 13/14/15 are global in every page.

- [ ] **Step 1: Write the failing tests:**
  - `otis_reset_state`:
    - after `reset()`: `write(15,0); read(0)==0xF003`
    - `read(13)==0x001F` (ACT, upper bits 0)
    - `read(14)==0x00E0` (IRQV free, bits 6:5=1)
    - `write(15,64); read(8)==(0x07F8|1)` (SERMODE A/D=1)
  - `otis_page_selects_voice`: write FC=0x1234 on page 3 and FC=0x5678 on page 4; reading back each page returns its own value; `read(1)` on page 3 equals `0x1234 & 0xFFFE`, since FC uses bits 15:1.
  - `otis_masks`:
    - STRT-H writes keep `[12:0]`; STRT-L keeps `[15:5]`
    - K1/K2 keep `[15:4]`; LVOL keeps `[15:8]`
    - ACCH keeps `[12:0]`; ACCL keeps all 16 bits
    - `write(0,0xFFFF); read(0)==0xFFFF` (CR 12 bits plus the 0xF000 read-as-1)
  - `otis_filter_storage_page`: on page 32+5 write regs 1..6 = 0x1111..0x6666; read back equal; page 5's voice FC is unaffected.
  - `otis_global_regs_visible_on_all_pages`: `write(13,7)` on page 70 → `read(13)==7` on page 0, and `activeVoices()==8`.
  - `otis_ch_regs_test_mode`:
    - page 64 with SERMODE TEST=0: writing CH0L is ignored and reads return 0 (O11)
    - SERMODE A/D=1 with `setSerialIn(0x1234,-2)` → page 64 `read(6)==0x1234`, `read(7)==0xFFFE`
- [ ] **Step 2:** Build and run:
  ```bash
  cmake --build build
  build/ens_tests otis_
  ```
  Expected: FAIL (unresolved `ens::Otis`).
- [ ] **Step 3:** Implement the `Otis` register storage per voice: `cr, fc, strtH, strtL, endH, endL, k2, k1, lvol, rvol, acch, accl`, plus `o1, o2_1, o2_2, o3_1, o3_2, o4`. Also the globals and the channel latches. Add DECISION comments O9, O10 and O11.
- [ ] **Step 4:** Run `build/ens_tests otis_`. Expected: PASS.
- [ ] **Step 5:** Commit `"feat(otis): host register interface and paging"`.

---

### Task 3: OTIS address generation, looping and IRQ

**Files:**
- Modify: `src/otis/otis.h`, `src/otis/otis.cpp`, `docs/decisions.md` (O1, O2, O7, O8)
- Test: `tests/test_otis_addr.cpp`

**Interfaces:**
- Consumes: Task 2 `Otis`.
- Produces:
  - `void Otis::stepVoice()` processes the next voice slot (0..ACT): address update and IRQ in this task, signal path in Task 4. After slot ACT it marks the frame ready.
  - `bool Otis::frameReady() const`
  - `OtisFrame Otis::takeFrame()` returns the latched channels and clears ready.
  - `OtisFrame Otis::runFrame()` steps until ready, then takes the frame.
  - Test-only accessor: `uint32_t Otis::acc29(int voice) const`.

  Helper for tests: a `FakeWave : WaveMemory` returning `addr & 0xFFFF` and recording the addresses read.

- [ ] **Step 1: Write the failing tests.** Set the voice up through `write()`. Positions are given in samples; `acc29 = samples << 9`.
  - `otis_forward_step`: FC=0x0400 (step 1.0), acc=10.0, LPE=0, END=100 → after one `runFrame()`, `acc29(0) == 11<<9`.
  - `otis_fraction_step`: FC=0x0300 (0.75): from 0, after 4 frames `acc29 == 3<<9`.
  - `otis_oneshot_stops_at_end`: FC=0x0400, acc=98, END=100, LPE=0 → after 2 frames, CR has STOP0 set and `acc29 == 100<<9`. After a 3rd frame acc is unchanged (O1, O7).
  - `otis_forward_loop_keeps_remainder`: START=10, END=20, LPE=1, FC=0x0600 (1.5), acc=19.0 → next `acc29 == (10<<9) + (512/2)`, i.e. 10.5.
  - `otis_bidi_reflects_and_toggles_dir`: START=10, END=20, LPE=1, BLE=1, FC=0x0600, acc=19.0 → acc = 19.5 and CR.DIR=1. The next frame gives acc = 18.0.
  - `otis_reverse_loop`: DIR=1, LPE=1, BLE=0, START=10, END=20, FC=0x0400, acc=10.0 → acc = 19.0 (`END - (START - n)` with n=9).
  - `otis_stop1_freezes_address`: STOP1=1 → acc unchanged after a frame, but FakeWave still saw reads at `int` and `int+1` (O7, O2).
  - `otis_s2_address_in_reverse_is_int_plus_1`: DIR=1, acc=50.0 → reads include 50 and 51.
  - `otis_irq_sequence`:
    1. voice 3 with IRQE, one-shot hitting END → after the frame, `irqAsserted()`, `read(14) & 0x1F == 3`, bit7 was 0 in that read, and `irqAsserted()==false` after the read.
    2. The next frame clears voice 3's CR.IRQ.
    3. Two voices (2 and 5) hitting END in the same frame → the first read returns 2. After the next frame, IRQ is asserted again and the read returns 5.
  - `otis_forced_irq`: the host writes CR with IRQE|IRQ set → the next frame raises an IRQ with that voice number.
- [ ] **Step 2:** Run `build/ens_tests otis_`. Expected: the new tests FAIL.
- [ ] **Step 3:** Implement exactly per Decision Defaults O1, O2, O7 and O8.
  - Loop compare values are 29-bit with the low 5 fraction bits 0.
  - The accumulator wraps within 29 bits.
- [ ] **Step 4:** Run the tests. Expected: PASS.
- [ ] **Step 5:** Write the O1, O2, O7 and O8 entries in `docs/decisions.md`. Commit `"feat(otis): address generation, loop modes, IRQ"`.

---

### Task 4: OTIS signal path: interpolation, filter, volume, channel output

**Files:**
- Modify: `src/otis/otis.cpp`, `docs/decisions.md` (O3–O6, O12, plus a new entry O13 for ACT < 7)
- Test: `tests/test_otis_signal.cpp`

**Interfaces:**
- Consumes: Task 3 `stepVoice/runFrame`. Uses `ens::sat`.
- Produces:
  - Frame outputs per O6/O12.
  - Pure helpers exposed in `otis.h` for testing:
    - `int16_t otis_interp(int16_t s1, int16_t s2, uint32_t frac9)`
    - `int16_t otis_lp(int16_t x, int16_t y1, uint16_t k12)`
    - `int16_t otis_hp(int16_t x, int16_t x1, int16_t y1, uint16_t k12)`
    - `int32_t otis_vol(int16_t x, uint8_t vol8)`

- [ ] **Step 1: Write the failing tests:**
  - `otis_interp_values`:
    - `otis_interp(0,512,256)==256`
    - `otis_interp(100,-100,511)==-100` (computes 100 + ((−200·511) >> 9) = 100 + (−200) = −100)
    - `otis_interp(-1,0,1)==-1` (floor)
  - `otis_lp_values`:
    - `otis_lp(1000,0,4095)==999` ((4095·1000) >> 12 = 999)
    - `otis_lp(1000,0,0)==0`
    - `otis_lp(-32768,32767,4095)==-32753` ((4095·(−65535)) >> 12 = −65520 with floor, plus 32767 = −32753)
  - `otis_hp_values`: `otis_hp(1000,0,0,0)==1000`; `otis_hp(0,0,1000,0)==500`; `otis_hp(0,0,1000,4095)==999`.
  - `otis_vol_table`:
    - `otis_vol(0x4000,0xF0)==0x4000*16>>5`
    - `otis_vol(x,0x00)==0` for all x
    - `otis_vol(-32768,0x10)` == `(-32768*16>>5)>>14` == −1 (sign fill)
    - mantissa 0xF: `otis_vol(1024,0xFF)==(1024*31)>>5`
  - `otis_filter_mode_table`: CR LP4/LP3 = 00, 01, 10, 11. With K1=4095 and K2=0, feed a DC step through one voice and assert which poles pass DC, per the OTIS notes §4.7 table. For 00 (pole 3 HP/K2, pole 4 HP/K2), output decays to 0. For 11 (both LP), output settles to the low-passed DC.
  - `otis_channel_saturation`: 4 voices on CA=1, each producing 0x7000 (max volume, DC input) → `frame.ch[1][0]==0x7FFF`; same negative → 0x8000; channel 0 is unaffected.
  - `otis_stopped_voice_still_outputs`: a voice with STOP1 on a DC sample still contributes to its channel (O7).
  - `otis_stopped_voice_o1_tracks_acc`: this is the KS-32 zero-crossing search.
    - Voice with STOP0|STOP1, K1=4095, a FakeWave ramp.
    - Host sets ACC to sample 100, runs 8 frames, then reads page 32+v reg 6. The result is within 2 LSB of the sample at 100.
    - Moving ACC to 200 and running 8 more frames makes reg 6 follow that sample.
  - **Review focus** `otis_midframe_write_affects_later_voices_only`: ACT=7. Step slots 0..3, then write LVOL=0 on voices 2 and 6 (via PAGE), then finish the frame. Voice 2's contribution is present and voice 6's is absent.
  - **Review focus** `otis_act_below_8_still_produces_frames`: ACT=2 (3 voices) → `runFrame()` returns after 3 slots with valid output.
- [ ] **Step 2:** Run `build/ens_tests otis_`. Expected: the new tests FAIL.
- [ ] **Step 3:** Implement per O3–O6 and O12. The per-slot order is: fetch S1/S2 → interpolate → pole 1 (LP, K1) → pole 2 (LP, K1) → pole 3 (per table) → pole 4 (per table) → store the state registers → volume L/R → add into CH[CA]. On slot ACT, latch the 8 channel outputs (saturated per O6) into the frame and clear the accumulators. While SERMODE.TEST=1, accumulation is off and the latched values are the host-written CH registers. Record ACT<7 as decision O13 (allowed; no serial-timing model).
- [ ] **Step 4:** Run the tests. Expected: PASS. Pin the exact expected values computed in Step 1 and make sure none are left approximate.
- [ ] **Step 5:** Commit `"feat(otis): interpolation, 4-pole filter, volume, channel saturation"`.

---

### Task 5: ESP ALU (pure)

**Files:**
- Create: `src/esp/esp_alu.h`, `src/esp/esp_alu.cpp`, `tests/test_esp_alu.cpp`
- Modify: `docs/decisions.md` (E1–E3, E13–E16, E21)

**Interfaces:**
- Produces (`namespace ens::esp`):

```cpp
enum class Op : uint8_t { ADD, SUB, ADDU, SUBU, CMP, AND, OR, XOR, ABS, MOV, ASL2, ASL8, LS15, DIFF, ASR, END };
struct Flags { bool n, c, v, lt, z; };                 // CCR bits 23,22,21,20,19
struct AluResult { uint32_t value; Flags flags; bool writes; };  // writes=false for CMP/END
AluResult alu(Op op, uint32_t a, uint32_t b, Flags prev);       // a,b,value are 24-bit
uint32_t  packCcr(const Flags&);                       // -> bits 23..19
Flags     unpackCcr(uint32_t);
bool      skipCondition(uint32_t ccr, uint32_t cmr);  // ESP notes §4.2.1 formula; CMR NOT = bit 18
```

  CMR bits: N 23, C 22, V 21, LT 20, Z 19, NOT 18. The ESP notes' 8-bit mask-table values (for example EQ = 0x0C) are the CMR value `>> 16`, so test `cmr = mask << 16`.

- [ ] **Step 1: Write the failing tests.** Each pins value and flags:
  - `alu_add_saturates`: `alu(ADD,0x7FFFFF,1)` → value 0x7FFFFF, v=1, n=0, lt=0. `alu(ADD,0x800000,0xFFFFFF)` → 0x800000, v=1, n=1, lt=1.
  - `alu_addu_wraps`: `alu(ADDU,0x7FFFFF,1)` → 0x800000, v=1, n=1, lt=0 (N xor V), c=0. `alu(ADDU,0xFFFFFF,1)` → 0, z=1, c=1.
  - `alu_sub_borrow` (E1): `alu(SUB,1,2)` → 0xFFFFFF, c=1, n=1, lt=1. `alu(SUB,2,1)` → 1, c=0.
  - `alu_cmp_does_not_write`: `writes==false`, flags as for SUBU.
  - `alu_logic_keeps_cvlt`: with prev c=v=lt=1, `alu(AND,0xF0F0F0,0x0F0F0F)` → 0, z=1, n=0, and c, v, lt still 1.
  - `alu_abs_ones_complement`: `alu(ABS,_,0xFFFFFF)` → 0, n=0, c=1, z unchanged from prev. `alu(ABS,_,0x800000)` → 0x7FFFFF.
  - `alu_mov_no_flags`: prev flags are returned unchanged.
  - `alu_asl2`: `alu(ASL2,_,0x100000)` → 0x400000, v=0. `alu(ASL2,_,0x200000)` → 0x7FFFFF, v=1. `alu(ASL2,_,0xE00000)` → 0x800000, v=0.
  - `alu_asl8`: `alu(ASL8,_,0x00007F)` → 0x007F00. `alu(ASL8,_,0x008000)` → 0x7FFFFF, v=1.
  - `alu_ls15`: `alu(LS15,_,0x0001FF)` → 0x7F8000, n=0, c=1.
  - `alu_diff_unsaturated` (E2): `alu(DIFF,_,0)` → 0x7FFFFF. `alu(DIFF,_,0x800000)` → 0xFFFFFF, v=1.
  - `alu_asr`: `alu(ASR,_,0x800001)` → 0xC00000, c=1, n=1.
  - `skip_masks`, using `skipCondition(ccr, mask<<16)` and the literal formula `SKIP = NOT xor OR(flag & mask)` (E21). The masks mean "execute when the condition holds":
    - EQ 0x0C skips when z=0
    - NEQ 0x08 skips when z=1
    - LT 0x14 skips when lt=0
    - GT 0x18 skips when lt=1 or z=1
    - "TRUE" 0x04 always skips; "FALSE" 0x00 never skips
- [ ] **Step 2:** Run `build/ens_tests alu_`. Expected: FAIL.
- [ ] **Step 3:** Implement per E1–E3 and E13–E16, with DECISION comments.
- [ ] **Step 4:** Run the tests. Expected: PASS.
- [ ] **Step 5:** Commit `"feat(esp): ALU operations and condition codes"`.

---

### Task 6: ESP multiplier/accumulator

**Files:**
- Create: `src/esp/esp_mac.h`, `src/esp/esp_mac.cpp`, `tests/test_esp_mac.cpp`
- Modify: `docs/decisions.md` (E6)

**Interfaces:**
- Produces (`namespace ens::esp`):

```cpp
class Mac {
public:
  int64_t  product(uint32_t x24, uint32_t y24, bool shiftOne) const; // sext*sext << (shiftOne?1:2)
  void     accumulate(int64_t product, bool mac);                     // acc = wrap50((mac?acc:0)+product)
  uint32_t readHigh() const;  uint32_t readLow() const;               // saturated MACH / MACL (24-bit)
  void     writeHigh(uint32_t v); void writeLow(uint32_t v);          // ALU preload per E6
  int64_t  raw() const;                                               // 50-bit signed, for tests
};
```

- [ ] **Step 1: Write the failing tests:**
  - `mac_fractional_half_times_half`: `product(0x400000,0x400000,true)` → acc (mac=false) → `readHigh()==0x200000`, `readLow()==0`.
  - `mac_shift_two`: same operands with shiftOne=false → `readHigh()==0x400000`.
  - `mac_neg_full_scale_squared_saturates`: 0x800000 × 0x800000, shift 1 → `readHigh()==0x7FFFFF`, `readLow()==0xFFFFFF`.
  - `mac_guard_bits_recover`: accumulate +0.9 three times then −0.9 twice (0.9 = 0x733333). Intermediate reads are saturated; the final `readHigh()` equals 0x733333 shifted per mode, within 1 LSB of truncation. Pin the exact value.
  - `mac_wraps_at_50_bits` (fixes MAME bug #2): accumulate a large positive product until past 2^49 → `raw()` wraps negative, and `readHigh()` saturates according to the wrapped value.
  - `mac_preload`: `writeHigh(0x100000); writeLow(0x000001);` then `accumulate(0,true)` → `readHigh()==0x100000`, `readLow()==1`.
  - `mac_plain_multiply_discards_preload`: `writeHigh(0x100000)` then `accumulate(p,false)` → result is p only.
- [ ] **Step 2:** Run `build/ens_tests mac_`. Expected: FAIL.
- [ ] **Step 3:** Implement per E6. The wrap50 is `((v << 14) >> 14)` on `int64_t`.
- [ ] **Step 4:** Run the tests. Expected: PASS.
- [ ] **Step 5:** Commit `"feat(esp): multiplier/accumulator with guard bits and saturation"`.

---

### Task 7: ESP address generator and DOL FIFO

**Files:**
- Create: `src/esp/esp_agen.h`, `src/esp/esp_agen.cpp`, `tests/test_esp_agen.cpp`
- Modify: `docs/decisions.md` (E7, E20, and FIFO behaviour as E18 part)

**Interfaces:**
- Produces (`namespace ens::esp`):

```cpp
enum class RamOp : uint8_t { ReadDelay=0, WriteDelay=1, ReadTableA=2, WriteTableA=3,
                             ReadTableB=4, ReadDelayDump=5, ReadIo=6, WriteIo=7 };
struct Agen {
  uint32_t memsiz=0, dlength=0, abase=0, bbase=0, dbase=0;   // 24-bit aligned per E7
  uint32_t unit() const; int shift() const;
  uint32_t physical(RamOp op, uint32_t offset24) const;      // word address
  bool     isIo(RamOp op) const; bool isWrite(RamOp op) const;
  void     endOfSample();                                    // DBASE -= unit; reload DLENGTH below 0
  void     writeReg(int spr, uint32_t v24);                  // spr: 244 MEMSIZ,245..248; applies E7/E20
  uint32_t readReg(int spr) const;
};
class DolFifo { public: void push(uint32_t v24); bool pop(uint32_t& out); void dump(); void reset();
                        int size() const; };                 // pop on empty returns last pushed, true
```

- [ ] **Step 1: Write the failing tests:**
  - `agen_memsiz_64k`: memsiz=0x0000FF → unit=0x100, shift=8.
  - `agen_delay_no_wrap`: dlength=0x00FF00, dbase=0x001000, off=0x000200 → `physical(ReadDelay)==0x12`.
  - `agen_delay_wraps_circular`: dlength=0x00FF00, dbase=0x00FF00, off=0x000200 → sum 0x010100 > dlength → 0x010100 − 0x010000 = 0x000100 → phys 0x01.
  - `agen_dbase_decrements_and_reloads`: dbase=0x000000 → `endOfSample()` → dbase == dlength. dbase=0x000100 → 0.
  - `agen_period_matches_wrap`: for dlength=0x0FF00 and any off ≤ dlength, the address seen by a fixed offset recurs every `dlength/unit + 1` samples. Check by stepping `endOfSample()`.
  - `agen_table_not_masked_by_dlength`: abase=0x020000, off=0x000300 → phys 0x203. ReadTableB uses bbase.
  - `agen_io_absolute`: off=0x123450 → `physical(ReadIo)==0x12345`.
  - `agen_reg_alignment`: `writeReg(245,0xFFFFFF)` → `readReg(245)==0xFFFFF0`. `writeReg(244,0xFF)` → memsiz 0xFF and dbase masked (E20).
  - `fifo_semantics`: push a, b, c → size 2; pop gives b, then c. Pop on empty gives c again (true). dump on empty does nothing. After a reset, size is 0.
- [ ] **Step 2:** Run `build/ens_tests agen_`. Expected: FAIL.
- [ ] **Step 3:** Implement per E7 and E20 and the FIFO rules (ESP notes §3.6.3).
- [ ] **Step 4:** Run the tests. Expected: PASS.
- [ ] **Step 5:** Commit `"feat(esp): address generator and DOL FIFO"`.

---

### Task 8: ESP execution core

**Files:**
- Create: `src/esp/esp.h`, `src/esp/esp.cpp`, `tests/test_esp_core.cpp`
- Modify: `docs/decisions.md` (E4, E5, E8–E12, E17–E19)

**Interfaces:**
- Consumes: Tasks 5–7 (`alu`, `Mac`, `Agen`, `DolFifo`). Instruction field layout per ESP notes §4.1: D 47:40, C 39:32, B 31:24, A 23:16, op 15:12, opsel 11:8, skip 7, mac 6, ram 5:3.
- Produces (`namespace ens`):

```cpp
struct EspBus { virtual ~EspBus() = default;
  virtual uint16_t read(bool io, uint32_t wordAddr) = 0;
  virtual void     write(bool io, uint32_t wordAddr, uint16_t v) = 0; };
class Esp {
public:
  explicit Esp(EspBus& bus);
  void     reset();
  int      runSample();                       // executes from PC until END (E11 cap); returns steps run
  void     setSerialIn(int port, int16_t l, int16_t r);   // ports 0..3, latched into SERnL/R
  void     serialOut(int port, int16_t& l, int16_t& r) const;
  bool     halted() const;
  // direct state (tests, trace runner):
  uint32_t gpr(int addr) const;  void setGpr(int addr, uint32_t v24);  // 0..255 incl. SPR semantics
  uint64_t instr(int i) const;   void setInstr(int i, uint64_t w48);
  const esp::Mac& mac() const;   const esp::Agen& agen() const;
  // Task 9 adds the host interface.
};
```

  **Operand-select decode:** use the ESP notes §4.1.3 table (rows 0–F) verbatim. In 1-operand ALU ops (ABS, MOV, ASL2, ASL8, LS15, DIFF, ASR) the ALU input is B only, and B=244 reads the DIL (case ii).

  **Step algorithm (E17/E18):**

```text
per step k (word w = instr[pc]):
 1 commitMul(pendingMul)            // C reg write and/or DOL push, unless pendingMul.skipped
 2 mulX = opsel.mulSrcDil ? DIL : read(w.C); mulY = read(w.D); prod = mac.product(mulX, mulY, sigreg.bit22)
 3 commitAlu(pendingAlu)            // A reg write and/or DOL push, CCR update, MACH/MACL preload
 4 skip = w.skip && op != CMP && skipCondition(CCR, CMR)
 5 aluA = opsel.aluSrcDil ? DIL : read(w.A); aluB = read(w.B); pendingAlu = alu(op, aluA, aluB, CCR) + dest + skip flag
 6 if !skip: mac.accumulate(prod, w.mac); pendingMul = {mac.readHigh(), dest from opsel, skipped=skip}
 7 if ramPending: perform RAM op of previous word (read -> DIL = data<<8; write/dump via FIFO)
 8 ramPending = {op=w.ram, addr=agen.physical(w.ram, gpr[pc])}   // gpr[pc] read at this point
 9 if op == END or pc == 159: agen.endOfSample(); host transfer (Task 9); sample HALT; pc = 0; stop
   else pc++
```

  The pipeline state (`pendingMul`, `pendingAlu`, `ramPending`) persists across `runSample()` calls. CCR is only written in commitAlu, and only if `!skipped || op==CMP`.

- [ ] **Step 1: Write the failing tests.** Build instruction words with a local `w48(D,C,B,A,op,opsel,skip,mac,ram)` helper; Task 10 later replaces it with `esp::Instr::encode`.
  - `esp_mov_and_end`: `[0] MOV B=1 → A=2`, `[1] END`; gpr[1]=0x123456 → after `runSample()`, gpr[2]==0x123456 (committed at the END step, per ordering 3) and the return value is 2.
  - `esp_alu_result_not_visible_to_next_mul`: `[0] ADD gpr1+gpr2→gpr3` while `[1] MUL C=3, D=4 → C=3` is in the same program. MUL at step 1 reads the old gpr3. Assert gpr3 after the sample equals the multiply of the old value (E17).
  - `esp_mul_result_visible_to_next_alu`: `[0] MUL C=5×D=6→C=5`; `[1] MOV B=5→A=7` → gpr7 holds the product's MACH.
  - `esp_mac_preload_slot`: `[0] MOV B=10 → A=MACH(243)`; `[1] MUL 0×0 with MAC=1 → C=11` → gpr11 == gpr10's value.
  - `esp_skip_suppresses_writes_not_ram`: CMR = NEQ mask (0x08<<16); CCR z=1, so skip; `[0]` skippable `ADD`, which is not written; the same word's RAM field (write delay) still performs a write (E5), so the bus fake records it.
  - `esp_cmp_ignores_skip` (E4): `[0]` CMP with skip=1 and CMR=TRUE still updates the CCR.
  - `esp_delay_line_roundtrip`:
    - Program: `[0]` MOV SER0L → FIFO (opsel 5) with RAM=WriteDelay at offset 0; `[1]` RAM=ReadDelay at offset 0x000400 (4 samples at MEMSIZ 0xFF); `[3]` MOV DIL → SER3L; END.
    - Set memsiz=0xFF, dlength=0x00FF00.
    - Feed SER0L = 1000, 2000, 3000, … per sample. SER3L output equals the input from 4 samples earlier, give or take the documented pipeline offset. Pin the exact lag observed and assert it equals 4.
  - `esp_ser_regs_left_justified` (E8): setSerialIn(0, −2, 5) → gpr(235)==0xFFFE00, gpr(234)==0x000500.
  - `esp_constants_and_holes` (E9, E12): writes to 252..255 are discarded; gpr(200) reads 0.
  - **Review focus** `esp_runsample_terminates_without_end`: 160 NOP words with no END → `runSample()` returns 160 and the next call starts at pc 0 (E11).
  - **Review focus** `esp_feedback_loop_saturates_not_wraps`: a one-sample feedback loop with loop gain > 1: `y = sat(x + y_prev + y_prev·0.5)`, built from MUL (0x400000 coefficient) plus two saturating ADDs. With x = 0.5 full scale, SER3L reaches 0x7FFF within 10 samples and is never negative over 100 samples.
- [ ] **Step 2:** Run `build/ens_tests esp_`. Expected: FAIL.
- [ ] **Step 3:** Implement `Esp` exactly per the step algorithm, with DECISION comments E4, E5, E8–E12 and E17–E19. SPR reads and writes go through one `readReg`/`writeReg` pair shared by ALU, MUL and host.
- [ ] **Step 4:** Run the tests. Expected: PASS.
- [ ] **Step 5:** Commit `"feat(esp): instruction execution core with documented pipeline"`.

---

### Task 9: ESP host interface, halt, RAM access and RAM clear

**Files:**
- Modify: `src/esp/esp.h`, `src/esp/esp.cpp`, `docs/decisions.md` (E10 and any new ones)
- Test: `tests/test_esp_host.cpp`

**Interfaces:**
- Consumes: Task 8 `Esp`.
- Produces:
  - `uint8_t Esp::hostRead(uint8_t addr)` and `void Esp::hostWrite(uint8_t addr, uint8_t v)`, using the map in ESP notes part 2 §5.1.1: $00–$02 GPR, $03–$08 INSTR, $09–$0B DIL, $0C–$0E DOL, $0F–$11 DADR, $12 host control, $14 RAM access, $16 PC, $17 refresh, $18 serial control, $1F halt enable/frame counter, $80/$A0/$C0/$E0 selects.
  - `void Esp::setHaltPin(bool high)`.
  - `void Esp::idleStep()` advances one instruction period while halted. It performs one host transfer, one RAM-clear write or one refresh decrement, per §5.4.1.

- [ ] **Step 1: Write the failing tests:**
  - `host_write_gpr_running`: write $00–$02 = 12 34 56, then `$A0 = 0x10` → `hostRead($12) & 4` (OK/ busy) == 4 → `runSample()` → gpr(0x10)==0x123456 and busy == 0.
  - `host_read_instr`: setInstr(5, X); `$80 = 5`; `runSample()`; $03–$08 return X's bytes, with $08 = bits 7..0 & 0xF8.
  - `host_write_instr_and_gpr`: `$E0` writes both at the next END.
  - `host_one_transfer_per_sample`: two queued writes; only the first lands per `runSample()`. The second request overwrites the first, matching the hardware's single address latch, and the test documents this.
  - `halt_at_end`: `setHaltPin(true)` → `runSample()` completes and `halted()` is true. A further `runSample()` returns 0. `setHaltPin(false)` resumes from pc 0. The DOL FIFO is empty after halting.
  - `halt_enable_immediate`: with the pin high, writing $1F halts immediately mid-program; the next resume starts at pc 0.
  - `host_ram_write_read_halted`:
    1. Halted; `$14 = 0x40` (write, RAM); DOL $0C/$0D = AB CD; DADR $10 = 0x12, $11 = 0x30, then $0F = 0x00.
    2. With memsiz=0xFF, the bus fake sees a write of 0xABCD at word address 0x12 (DADR 24-bit = 0x001230 >> 8).
    3. Then `$14 = 0xC0` (read) with the same DADR → $09/$0A = AB CD and $0B = 0.
  - `ram_clear`: halted, memsiz=0xFF, dlength=0x00FF00; host control = 2; call `idleStep()` until bit1 clears → the bus fake recorded zero writes to all 256 addresses 0..0xFF and dbase==0.
  - `refresh_disable_freezes_dbase`: host control = 1, halted, 10 `idleStep()`s → dbase unchanged. With 0 → dbase decrements once per step.
  - `serial_control_register`: writes to $18 read back with bits 1:0 = 1.
- [ ] **Step 2:** Run `build/ens_tests host_`. Expected: FAIL.
- [ ] **Step 3:** Implement per §5.1–§5.4.
- [ ] **Step 4:** Run the tests. Expected: PASS.
- [ ] **Step 5:** Commit `"feat(esp): host interface, halt, host RAM access, RAM clear"`.

---

### Task 10: ESP instruction encode/decode, disassembler and `esp_dis` tool

**Files:**
- Create: `src/esp/esp_isa.h`, `src/esp/esp_isa.cpp`, `tools/esp_dis/main.cpp`, `tests/test_esp_isa.cpp`
- Modify: `tests/test_esp_core.cpp` (replace the local `w48` helper with `Instr::encode`), `CMakeLists.txt`

**Interfaces:**
- Produces (`namespace ens::esp`):

```cpp
struct Instr { uint8_t d,c,b,a; Op op; uint8_t opsel; bool skip, mac; RamOp ram;
               static Instr decode(uint64_t w48); uint64_t encode() const; };
std::string disassemble(const Instr&, int pc);       // one line, stable format below
std::string regName(uint8_t addr);                   // "gpr12", "SER0L", "MACH", "DIL", "-1", ...
```

  Line format:

  `PC  OP    srcB, srcA > dest[,DOL] | MUL D x C > dest[,DOL] [+MAC] | RAM <op> [SKIP]`

  Here srcA/dest show `DIL` and `DOL` according to the operand-select table. `regName` covers every address 0..255 using the names in ESP notes §3.2.

  CLI: `esp_dis <file> [--offset N] [--count N] [--be|--le]` reads 6-byte words (big-endian by default) and prints one disassembled line per word, plus the raw hex.

- [ ] **Step 1: Write the failing tests:**
  - `isa_roundtrip`: for 10,000 pseudo-random 48-bit words with the low 3 bits cleared, `decode(w).encode()==w`.
  - `isa_fields`: `decode(0x0102'0304'D5A8)` → d=1, c=2, b=3, a=4, op=DIFF(0xD), opsel=5, skip=1, mac=0, ram=5 (bits 5:3 of 0xA8 = 0b101).
  - `dis_line`: pin the exact disassembly strings of 3 hand-built words: a MOV, a MUL+MAC with DIL source, and an END.
  - `regname_all_spr`: `regName(243)=="MACH"`, `regName(244)=="DIL/MEMSIZ"`, `regName(250)=="CCR"`, `regName(200)=="?200"`.
- [ ] **Step 2:** Run `build/ens_tests isa_`. Expected: FAIL.
- [ ] **Step 3:** Implement. Switch the Task 8 tests over to `Instr`.
- [ ] **Step 4:** Run all tests (`build/ens_tests`). Expected: all PASS.
- [ ] **Step 5:** Commit `"feat(esp): instruction encode/decode, disassembler, esp_dis tool"`.

---

### Task 11: Trace format, `TraceRunner`, `ens_trace` and `pcm_diff` tools

**Files:**
- Create: `src/trace/trace.h`, `src/trace/trace.cpp`, `tools/ens_trace/main.cpp`, `tools/pcm_diff/main.cpp`, `tests/test_trace.cpp`, `docs/trace-format.md`
- Modify: `CMakeLists.txt` (`ens_trace` library, links otis+esp)

**Interfaces:**
- Consumes: `Otis` (Tasks 2–4), `Esp` (Tasks 8–9).
- Produces the trace text format, documented in `docs/trace-format.md`. Lines are `#` comments or:

```text
wave <bank 0|1> <path>          raw little-endian uint16 word image (up to 1M words)
dram <words>                    ESP DRAM size in words (default 65536: KS-32 MEMSIZ=0x0000FF, per docs/rom-investigation/findings.md §D)
otis <reg 0-15> <hex16>         OTIS host write
esp <addr hex8> <hex8>          ESP host write
esphalt <0|1>                   ESP HALT pin
frames <n>                      run n frames: per frame OTIS runFrame(); route; ESP runSample(); emit
```

  Routing is fixed for this plan and recorded as **TR1**. The KS-32 firmware confirms it: host serial control = 0x63, voices use CA 1–3, and CH0 is unused (`docs/rom-investigation/findings.md` §C). It also matches the MAME audit:
  - OTIS ch0 → aux out
  - OTIS ch1–3 → ESP SER0–2 in
  - ESP SER3 out → main out

  Output per frame: 4 int16 LE values `auxL auxR mainL mainR` to `<out>.pcm`. API:

```cpp
struct TraceError { int line; std::string msg; };
class TraceRunner {                      // owns Otis, Esp, wave + DRAM storage
public:
  bool load(const std::string& text, const std::string& baseDir, TraceError& err);
  void run(std::vector<int16_t>& outInterleaved4);   // executes all commands in order
};
```

  CLI:
  - `ens_trace <trace> <out.pcm>`
  - `pcm_diff <a.pcm> <b.pcm> [--channels 4]` prints the frame count, the first differing frame, the maximum absolute difference per channel and the RMS difference per channel. It exits 0 when identical, 1 otherwise.

- [ ] **Step 1: Write the failing tests:**
  - `trace_runs_dc_voice`: an inline trace whose wave is synthetic (a temporary file with constant 0x4000). It sets one voice (FC=0x400, LVOL=RVOL=0xF0, CA=0, LPE loop) and runs `frames 4` → the aux output is non-zero and stable from frame 2 on.
  - `trace_esp_passthrough`: an ESP program (via `esp` host writes, while halted) that copies SER0 to SER3; an OTIS voice on CA=1 → the main output equals the OTIS ch1 output delayed by the pipeline lag that Task 8 pinned.
  - **Review focus** `trace_parse_error_reports_line`: the text `"otis 3 zz\n"` → `load` returns false with `err.line==1`. A missing wave file reports its line too.
  - `pcm_diff_identical`: identical buffers report zero differences (test the comparison function `pcmCompare` directly).
- [ ] **Step 2:** Run `build/ens_tests trace_`. Expected: FAIL.
- [ ] **Step 3:** Implement. Tools print usage on bad arguments and exit with code 2.
- [ ] **Step 4:** Run the tests. Expected: PASS. Also run:
  ```bash
  build/ens_trace tests/data/smoke.trace out.pcm
  ```
  Expected: `out.pcm` size = frames × 8 bytes. Commit `tests/data/smoke.trace` and its synthetic wave file, under 64 KB.
- [ ] **Step 5:** Commit `"feat(trace): trace format, runner, ens_trace and pcm_diff tools"`.

---

### Task 12: MAME reference harness

**Files:**
- Create: `tools/mame_ref/CMakeLists.txt`, `tools/mame_ref/PINNED_COMMIT.txt`, `tools/mame_ref/vendor/` (`es5506.cpp`, `es5506.h`, `es5510.cpp`, `es5510.h`, plus their license headers kept intact), `tools/mame_ref/shim/` (minimal stand-ins for the MAME framework types those files use), `tools/mame_ref/main.cpp`, `docs/mame-ref.md`

**Interfaces:**
- Consumes: the trace format from Task 11. The harness reuses `ens::Trace` parsing only; it does not use our chips.
- Produces: `mame_ref <trace> <out.pcm>`. It has the same output layout as `ens_trace` and is built only when `-DENS_BUILD_MAME_REF=ON`.

- [ ] **Step 1:** Fetch the four files from `https://raw.githubusercontent.com/mamedev/mame/<commit>/src/devices/...`. ES5505/6 live in `sound/`; ES5510 lives in `cpu/es5510/`. Pin the current `master` commit hash and record it, with the date and the source URLs, in `PINNED_COMMIT.txt`. The user approved fetching MAME.
- [ ] **Step 2:** Write the shim headers. They must be just enough of `emu.h`, `device_t`, `cpu_device`, `sound_stream`, `address_space` and the logging macros for the vendored files to compile unmodified. The harness drives the chips like this:
  - OTIS host writes go through the device's `write` handler.
  - Each frame calls the ES5505 sound-stream update for one sample.
  - Routing to the ES5510 serial registers follows `esqpump.cpp`.
  - The ES5510's `run_once()` is called once per frame, the way `esqpump` does.

  **Fallback** if the shim grows past about 800 lines: hand-port only the per-sample paths (ES5505 voice generation plus filters; ES5510 `execute_run` and the host register handlers) into `vendor/ported_*.cpp`. Keep the arithmetic line-for-line identical and list every deviation in `docs/mame-ref.md`.
- [ ] **Step 3:** Build and run against the Task 11 smoke trace:
  ```bash
  cmake -S . -B build-ref -G Ninja -DENS_BUILD_MAME_REF=ON
  cmake --build build-ref
  build-ref/mame_ref tests/data/smoke.trace mame.pcm
  build/ens_trace tests/data/smoke.trace ours.pcm
  build/pcm_diff ours.pcm mame.pcm
  ```
  Expected: both produce the same frame count. `pcm_diff` reports the differences.
- [ ] **Step 4:** Write `docs/mame-ref.md`. It explains how to build and run the harness, the pinned commit, any shim or port deviations, and a table of the differences on the smoke trace. Each row is either labelled with the decision id or fixed MAME bug that explains it, or flagged "unexplained".
- [ ] **Step 5:** Commit `"tools: MAME reference harness for OTIS/ESP comparison"`.

---

### Task 13: Effect-style validation and dual-toolchain verification

**Files:**
- Create: `tests/test_esp_programs.cpp`, `tests/data/reverb_tank.trace`, `docs/verification-plan1.md`

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Write the tests:**
  - `esp_allpass_reverb_decays`: a hand-assembled feedback-delay reverb using `Instr::encode`. It has 4 allpass stages plus one feedback delay with gain 0.7 (Dattorro-style topology, ESP2 document Part III §7). Feed an impulse on SER0L for 1 sample, then zeros. The output RMS over a 0.1 s window falls below −60 dBFS within 3 s at 29,761 Hz. Pin the measured time to the nearest 0.1 s. This is the guard against MAME bug #1.
  - `esp_chorus_interp_lsb`: LS15-based linear interpolation between two delay taps (ESP2 document Part III §1.2 kernel, re-expressed for ES5510 opcodes) produces a monotonic ramp when the tap fraction sweeps 0→1.
- [ ] **Step 2:** Run them. Expected: PASS. Run the same program through `mame_ref` with `reverb_tank.trace` and record MAME's decay time in `docs/verification-plan1.md`. If MAME's tail is much longer, that reproduces its known bug outside the SD-1.
- [ ] **Step 3:** Build and test with MSVC x64:
  ```bash
  cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
  cmake --build build-msvc --config Release
  ctest --test-dir build-msvc -C Release --output-on-failure
  ```
  Expected: all PASS.
- [ ] **Step 4:** Check the output is identical across toolchains. Run `ens_trace` on `smoke.trace` and `reverb_tank.trace` with both the MinGW and MSVC builds, then compare with `pcm_diff`. Expected: identical (exit 0).
- [ ] **Step 5:** Write `docs/verification-plan1.md`: the test counts, the decay times (ours vs MAME), the cross-toolchain result, and a list of open decisions with Low confidence for Plan 2 to revisit. Commit `"test: effect-style ESP validation, dual-toolchain verification"`.
