# Plan 5 verification

Closing check of Plan 5 (the 68000 core `ens_m68k`): the test suite and Tom Harte's 68000
per-instruction suite on both required toolchains, cross-toolchain identity of the suite run,
the speed benchmark, the KS-32 firmware smoke run, and the decisions (M1 to M24), with the
judgment calls listed in one place. The user's acceptance (the pass bar) is recorded in the last
section.

## Test counts

| Toolchain | Configure | Result |
|---|---|---|
| MinGW-w64 g++ 14.2.0, i686 (DWARF exceptions), Ninja, Release | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_ROM_DIR=...` | `ctest --test-dir build --output-on-failure`: **2/2 passed** (`m68k_suite`, `ens_tests`); `build/ens_tests`: **283 passed, 0 failed** |
| MSVC 19.44.35227 (Visual Studio 2022), x64, Release | `cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DENS_ROM_DIR=...` | `ctest --test-dir build-msvc -C Release --output-on-failure`: **2/2 passed**; `build-msvc/Release/ens_tests.exe`: **283 passed, 0 failed** |

Both builds were configured with `-DENS_ROM_DIR="C:/Users/abart/Desktop/Ensoniq KS-32/ks-32"`, so
the ROM smoke test (`m68k_ks32_firmware_smoke`) runs and does not skip, and with the suite data
present at the default `ENS_M68K_TESTS_DIR`, so `m68k_suite` runs the whole suite. Both builds
compile without warnings (`-Wall -Wextra`, `/W4`).

## Per-instruction suite (`m68k_harte`)

All 124 files pass **8065/8065** on both builds, with **smp 0** in every file. Total:

```
total: 1000060/1000060 (smp 0, aef 31371, dev 3)
```

No `FAIL` and no `WARNING stale manual deviation` line. Files with a non-zero allowed class
(identical on both builds):

| File | Pass | aef | dev |
|---|---|---|---|
| ADD.l | 8065/8065 | 55 | 0 |
| ADD.w | 8065/8065 | 49 | 0 |
| ADDA.l | 8065/8065 | 135 | 0 |
| ADDA.w | 8065/8065 | 135 | 0 |
| AND.l | 8065/8065 | 97 | 0 |
| AND.w | 8065/8065 | 80 | 0 |
| ASL.b | 8065/8065 | 0 | 2 |
| BSR | 8065/8065 | 3980 | 0 |
| Bcc | 8065/8065 | 2200 | 0 |
| CHK | 8065/8065 | 145 | 0 |
| CMP.l | 8065/8065 | 124 | 0 |
| CMP.w | 8065/8065 | 109 | 0 |
| CMPA.l | 8065/8065 | 136 | 0 |
| CMPA.w | 8065/8065 | 128 | 0 |
| DBcc | 8065/8065 | 1964 | 0 |
| DIVS | 8065/8065 | 157 | 0 |
| DIVU | 8065/8065 | 138 | 1 |
| JMP | 8065/8065 | 3806 | 0 |
| JSR | 8065/8065 | 3882 | 0 |
| MOVE.l | 8065/8065 | 124 | 0 |
| MOVE.w | 8065/8065 | 129 | 0 |
| MOVEA.l | 8065/8065 | 137 | 0 |
| MOVEA.w | 8065/8065 | 150 | 0 |
| MOVEM.l | 8065/8065 | 115 | 0 |
| MOVEM.w | 8065/8065 | 111 | 0 |
| MOVEtoCCR | 8065/8065 | 159 | 0 |
| MOVEtoSR | 8065/8065 | 139 | 0 |
| MULS | 8065/8065 | 158 | 0 |
| MULU | 8065/8065 | 153 | 0 |
| OR.l | 8065/8065 | 82 | 0 |
| OR.w | 8065/8065 | 80 | 0 |
| RTE | 8065/8065 | 4054 | 0 |
| RTR | 8065/8065 | 4027 | 0 |
| RTS | 8065/8065 | 4057 | 0 |
| SUB.l | 8065/8065 | 58 | 0 |
| SUB.w | 8065/8065 | 57 | 0 |
| SUBA.l | 8065/8065 | 144 | 0 |
| SUBA.w | 8065/8065 | 117 | 0 |

The other 86 files pass 8065/8065 with smp 0, aef 0, dev 0: ABCD, ADD.b, ADDX.b/w/l, AND.b,
ANDItoCCR, ANDItoSR, ASL.w/l, ASR.b/w/l, BCHG, BCLR, BSET, BTST, CLR.b/w/l, CMP.b, EOR.b/w/l,
EORItoCCR, EORItoSR, EXG, EXT.w/l, LEA, LINK, LSL.b/w/l, LSR.b/w/l, MOVE.b, MOVE.q, MOVEP.w/l,
MOVEfromSR, MOVEfromUSP, MOVEtoUSP, NBCD, NEG.b/w/l, NEGX.b/w/l, NOP, NOT.b/w/l, OR.b, ORItoCCR,
ORItoSR, PEA, RESET, ROL.b/w/l, ROR.b/w/l, ROXL.b/w/l, ROXR.b/w/l, SBCD, SUB.b, SUBX.b/w/l, SWAP,
Scc, TAS, TRAP, TRAPV, TST.b/w/l, UNLINK.

### M1's allowed classes

- **smp (self-modifying prefetch): 0.** The class never occurs on the real suite. M1 says a class
  that never occurs is deleted; the runner still has it, and its removal is left open for the
  controller (it changes the runner's output format, which this task does not own).
- **aef (address-error frame): 31371**, all from two decisions that follow the manuals where the
  suite's generator differs, and both differences sit in the first 8 bytes of the 14-byte frame:
  - 3401 are the M12 function code of PC-relative operand reads (program space as in PRM 2.0; the
    suite records data space). Example: MOVE.w `3e3a [MOVE.w (d16, PC), D7] 759`. They are spread
    over the arithmetic, logic, MOVE, MOVEA, MOVEM, CHK, MUL/DIV and MOVEtoSR/CCR files above.
  - 27970 are the I/N bit of a fault on the first fetch at an odd jump target (UM 6.3.9.1: I/N = 0,
    the processor is processing an instruction; the suite sets 1): Bcc 2200, BSR 3980, DBcc 1964,
    JMP 3806, JSR 3882, RTS 4057, RTR 4027, RTE 4054.

  With both choices switched to the suite's, every file shows aef 0 (measured in Tasks 3 to 6),
  so nothing else hides in the class.
- **dev (manual deviation by name): 3.** `80ef [DIVU (d16, A7), D0] 5745` (M15: the stacked PC of
  divide by zero is the next instruction's, UM 6.3.5), and `e502 [ASL.b Q, D2] 1583` and
  `e502 [ASL.b Q, D2] 1761` (M19: the suite's recorded initial and final states are inconsistent).

## Cross-toolchain identity

`m68k_harte "C:/Users/abart/Desktop/Ensoniq KS-32/testdata/680x0/68000/m68t"` was run with each
build and its whole output (125 lines: 124 files and the total) captured to a file outside the
repository, both through the same PowerShell redirection. `fc /b` reports no differences, and the
SHA-256 of both files is
`AB730D05E0BCE64CA83F6B2332A56498C2BF675C48F33CDD28B3F1124D0EE844`. The per-file pass and
allowed-class counts are therefore identical between 32-bit MinGW and 64-bit MSVC. The
benchmark below also ends in the same state on both (`d0 F12AB415, pc 001010`, 300000646
cycles), and the smoke run prints the same line on both.

## Speed (`m68k_harte --bench`)

A fixed synthetic mix runs from a 64 KB RAM bus for 300 M cycles, in `run(1000)` slices: MOVE.l
and MOVE.w across register, `(An)`, `(An)+`, `-(An)`, `(d16,An)`, `(d8,An,Xn)`, absolute long and
immediate modes, ADD, CMP, Bcc, ADDQ, JSR/RTS, LSL and MULU in a DBF loop of 1000 passes, with
data that keep changing (MULU takes the loop counter, so its data-dependent time is typical, not
its worst case). The tool exits 1 if the mix takes any exception. Three runs each:

| Build | M cycles/s (three runs) | Target |
|---|---|---|
| MSVC x64 Release | 663.9, 666.1, 666.6 | at least 61 (4 x 15.238 MHz) |
| MinGW i686 Release | 633.1, 631.8, 626.1 | (recorded only) |

That is about 11 times the target on MSVC and about 10 times on 32-bit MinGW, so no profiling or
hot-path change was made. Two costs earlier reviews asked about:

- The address-error abort is a C++ exception (`AddressFault`) thrown only on an odd word or long
  access, with a `try` in `step()`. The MinGW toolchain is built with DWARF-2 unwinding
  (`i686-msvcrt-posix-dwarf`, `--disable-sjlj-exceptions`), and MSVC x64 uses table-based
  unwinding, so entering the `try` costs nothing on the normal path on either build. The
  benchmark figures (a mix with no fault) agree.
- `shift()` loops once per place (up to 63). The mix's LSL.l #3 is three iterations; a long shift
  costs 2 clocks per place on the 68000 as well, so the loop's host time per emulated cycle stays
  in proportion. Not a hot spot at these figures.

## KS-32 firmware smoke run (`m68k_ks32_firmware_smoke`)

The test runs the KS-32 OS (`ks32/ks32v301upper.bin` as the even bytes, `ks32/ks32v301lower.bin`
as the odd bytes, merged at 0xC00000) on the CPU alone, 50 M cycles in `run(1000)` slices. The
firmware is run and never studied: the test prints only the counts below. Its bus is the brief's:
the reset vectors at 0-7 are read from 0xC00000-0xC00007, RAM is at 0x000008-0x00FFFF and
0xFF0000-0xFFFFFF, and everything else reads 0 and ignores writes. The lower EPROM covers only the
odd bytes of 0xC00000-0xC1FFFF; the odd bytes of 0xC20000-0xC3FFFF are absent and read **0**,
like any unmapped address.

Result on both builds (the test passes):

```
  distinct opcode words 9, last instruction at 0xC10FE4, halted no
```

The CPU did not halt and took none of vectors 2, 3, 4, 8, 10 and 11, so no assertion had to be
relaxed. The run reaches at least 50 M cycles.

**What the 9 means.** The coverage is small, and the cause is the memory map, not the CPU. Seen
from the CPU's own state and bus activity (a throwaway counting harness, not committed; no code was
listed or disassembled): from cycle 110 after reset the CPU runs a loop of 6 instructions whose
only data accesses are word writes to 0x000000-0x000006 and reads of the same window, with all
registers unchanged across 1 M cycles. That is a write/read-back check of address 0. On this bus
those reads always return the ROM vectors and the writes are ignored, so the check can never
pass. On the KS-32 the window at 0 is presumably RAM after reset with only the reset vectors
coming from ROM. That mapping is Plan 6's (the brief's bus says so), so this is the expected
"missing devices" outcome, not a CPU fault.

For information only (the same counting harness, not the committed test): with 0-7 made
writable RAM right after `reset()`, the run gets much further. It executes 222 distinct opcode
words at 437 distinct instruction addresses, finds its last new instruction address at about
8.16 M cycles, and then polls 8 unmapped addresses (device registers, reading 0) until the end.
It never halts and never takes vectors 2, 3, 4, 8 or 11. It does take vector 10 (line A) 40 times
at 2 instruction addresses, each returned from by RTE, and TRAP #0 (vector 32) once. Deciding
whether that line-A use is the firmware's own design (a system-call mechanism) or a symptom of
the absent odd bytes or the missing devices means looking at the firmware, which this plan does
not do. Plan 6 should look at it once the real memory map is in place, because the smoke
assertion `exceptionCount(10) == 0` would then fail.

## Decisions (`docs/decisions.md`, `## 68000 core`)

| Id | Topic | Confidence |
|---|---|---|
| M1 | Suite pass criterion (exact, plus smp, aef and dev by name) | Medium (smp never occurs; aef is fully explained by M12; dev is 3 named cases) |
| M2 | Undefined flags and results follow the suite | High for BCD, DIV overflow and CHK N/V/C; Medium for divide by zero (one case) and CHK Z for Dn = 0 (no case) |
| M3 | PC and prefetch convention (suite's) | High |
| M4 | Interrupt timing: 44 cycles, order of the acknowledge cycle | High for 44 and the frame; Medium for the acknowledge cycle's position |
| M5 | No bus errors, no function codes on `Bus` | Medium (until a KS-32 BERR source is found) |
| M6 | Run slices; halted or stopped CPU idles | High |
| M7 | Interrupt sampling; level 7 edge | High for levels 1 to 6, the edge and the order against trace; Medium for the held-7 re-take |
| M8 | RESET instruction: 132 cycles, `resetPeripherals()` | High |
| M9 | Double fault halts | High |
| M10 | DIVU/DIVS cycle algorithm | High for DIVU and non-overflow DIVS; Medium for the DIVS signed-range overflow; Low for the -32768 edge |
| M11 | Address-error frame contents and state at the fault | Medium (the UM calls it unpredictable; the suite is one model) |
| M12 | Function code of PC-relative reads (program); I/N at an odd jump target (0) | Medium |
| M13 | Bus order of long accesses and stores | High |
| M14 | ADDQ/SUBQ to An cycles (word 8, long 6) | Medium-Low |
| M15 | Divide-by-zero stacked PC (next instruction) | Medium |
| M16 | CHK trap cycles (38 / 40 by cause) | Medium |
| M17 | BCHG/BSET/BCLR register cycles | High |
| M18 | ASR by more places than the size: C = X = 0 | Medium-High |
| M19 | Two inconsistent ASL.b suite cases | High |
| M20 | TAS memory cycles (10 + <ea>) | High |
| M21 | DBcc expiry: the third read's address | Medium-Low for the third read and the odd-target fault; High for the 14 cycles |
| M22 | Prefetch queue of a stopped CPU | Medium |
| M23 | Bus reads of RTR and MOVE to CCR (suite's) | High |
| M24 | Acknowledge answer outside 0..255 is spurious | Medium |

No new decision was needed in Task 9 (M25 stays free).

### Judgment calls: suite evidence against or beyond the manuals

These are the decisions where the UM/PRM and the suite disagree, or where the manuals say
nothing and the suite (or no evidence at all) decides. Ruling R8 is the rule applied: an explicit
manual statement wins unless the manual contradicts itself on that point, or the suite gives
overwhelming consistent evidence that the manual's single figure cannot cover a data-dependent
case.

| Id | Manual says | Suite says | Chosen | Why |
|---|---|---|---|---|
| M12 (FC) | PRM 2.0: PC-relative operand reads are program references (FC 6/2) | data space (FC 5/1) | **manual** | explicit PRM statement; shows only in an address-error frame (aef 3401) |
| M12 (I/N) | UM 6.3.9.1: an instruction is being processed, I/N = 0 | I/N = 1 at an odd jump target's first fetch | **manual** | explicit UM statement; shows only in the frame (aef 27970) |
| M14 | UM table 8-5: ADDQ.w An 4, ADDQ.l / SUBQ.w / SUBQ.l An 8 (self-contradictory) | word 8, long 6 (1522 cases) | **suite** | the table contradicts itself; MAME and the Yacht tables give 8 for long, so 6 has no second source |
| M15 | UM 6.3.5: a trap stacks the next instruction's address | DIVU's only divide-by-zero case stacks the instruction's own address | **manual** | explicit UM statement against one case; listed as dev |
| M16 | UM table 8-14: CHK trap 40 + <ea> | 38 when Dn > bound, 40 when Dn < 0 (1000 register cases) | **suite** | one UM figure for two causes, split consistently by the suite |
| M18 | PRM: C = last bit shifted out (silent once every bit has left) | ASR by more than the size: C = X = 0 (3736 cases) | **suite** | the PRM does not cover the case; the suite is consistent |
| M19 | (not a manual question) | two ASL.b cases whose recorded initial and final states cannot belong together | **listed as dev** | the core follows the recorded initial state and the PRM |
| M20 | UM table 8-6: TAS 14(2/1) + <ea> | 10 + <ea> (14 for (An)) | **suite** | the table counts the operand read twice |
| M21 | UM table 8-9: DBcc expired 14(3/0), not which reads | no expired case | **no evidence**: the dropped read is at the branch target, as in the looping case | the only reading with 3 reads that reuses the looping path's fetch |
| M23 | UM table 8-12: RTR 20(2/0), MOVE to CCR 12(1/0) (self-contradictory) | RTR 5 reads, MOVE to CCR 2 reads | **suite** (totals equal) | the table contradicts itself (RTE pops the same frame in 5 reads) |

## User acceptance

The pass bar (design section 3, Plan 5 Step 6): the user is shown the suite totals, the benchmark
and the smoke result above.

User verdict: (pending — recorded by the controller)

## Reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_ROM_DIR="C:/Users/abart/Desktop/Ensoniq KS-32/ks-32" && cmake --build build
ctest --test-dir build --output-on-failure
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DENS_ROM_DIR="C:/Users/abart/Desktop/Ensoniq KS-32/ks-32"
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

Suite, identity and benchmark (PowerShell, from the worktree root; the MinGW executables need
`C:\winlibs\mingw32\bin` on `PATH`):

```powershell
$t = "C:/Users/abart/Desktop/Ensoniq KS-32/testdata/680x0/68000/m68t"
build\m68k_harte.exe $t > $env:TEMP\suite_mingw.txt
build-msvc\Release\m68k_harte.exe $t > $env:TEMP\suite_msvc.txt
fc.exe /b $env:TEMP\suite_mingw.txt $env:TEMP\suite_msvc.txt
build-msvc\Release\m68k_harte.exe --bench
build\m68k_harte.exe --bench
build\ens_tests.exe m68k_ks32
```
