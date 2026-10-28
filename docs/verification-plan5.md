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

- **smp (self-modifying prefetch): 0.** The class never occurs on the real suite. It is kept on
  purpose (final review, M1): it guards self-modifying prefetch cases the suite happens not to
  contain and keeps the runner's output format stable.
- **aef (address-error frame): 31371**, all from two decisions that follow the manuals where the
  suite's generator differs. Since the final review the class accepts nothing else: vector 3
  taken, every register, the prefetch words and the cycles exact, and the only RAM difference is
  the low byte of the frame's status word (SSP + 1) in bits 3..0; the 31371 cases still pass:
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
  Since the final review each entry names the fields it may differ in, and any other difference
  is a FAIL: DIVU 5745 only RAM 0x7FF (the stacked PC's low byte: expected 0x00, got 0x04); ASL.b
  1583 D2 and the condition codes (SR 0x2713 expected, 0x270A got); ASL.b 1761 D2 only. All
  three still pass as dev with the same output on both builds.

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

The CPU did not halt and took none of vectors 2, 3, 4, 8, 10 and 11, and the run reaches at
least 50 M cycles. The assertions pass, but they cover little: the run stays in a short loop that
starts early after reset.

The following was observed by running the firmware in a scratch harness (bus addresses and
program-counter values only); no firmware code was read, disassembled or listed. The harness is
not committed.

- With the test's bus: no new instruction address appears after cycle 110 from reset (9 distinct
  instruction addresses in all). From then on the program counter cycles through 6 addresses.
  The only data accesses are word writes to 0x000000-0x000006 and word reads of the same
  addresses, repeated, and the registers do not change across 1 M cycles. On this bus those reads
  return the ROM vectors and the writes are ignored. The mapping of 0-7 after reset is Plan 6's.
- With 0-7 made writable RAM right after `reset()` (a variant, not the committed test): 222
  distinct opcode words at 437 distinct instruction addresses. No new instruction address appears
  after about 8.16 M cycles, and from then on the run reads 8 unmapped addresses repeatedly. No
  halt, and none of vectors 2, 3, 4, 8 or 11. Vector 10 (line A) is taken 40 times, at 2
  instruction addresses, and each is followed by an RTE. TRAP #0 (vector 32) is taken once. With a
  real memory map the smoke assertion `exceptionCount(10) == 0` may therefore fail. This is an
  input to Plan 6 (ruling R15).

## Decisions (`docs/decisions.md`, `## 68000 core`)

| Id | Topic | Confidence |
|---|---|---|
| M1 | Suite pass criterion (exact, plus smp, aef and dev by name) | Medium (smp never occurs and is kept as a guard; aef is fully explained by M12 and limited to status bits 3..0; dev is 3 named cases limited to named fields) |
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

### Judgment calls: suite evidence against, beyond or without the manuals

This table lists every decision whose choice does not rest on an explicit manual statement alone.
That covers three kinds: the suite is chosen against the UM/PRM ("against"); the manuals are
silent, call the behaviour undefined or unpredictable, or give only totals or maxima, and the
suite decides ("beyond"); or neither the manuals nor the suite decide it ("no evidence"). It also
includes the cases where a manual statement was kept against the suite ("manual kept against
suite"), since those are judgment calls too. Ruling R8 is the rule applied: an explicit manual
statement wins unless the manual contradicts itself on that point, or the suite gives
overwhelming consistent evidence that the manual's single figure cannot cover a data-dependent
case.

| Id | Kind | Manual says | Suite says | Chosen | Reason | Confidence |
|---|---|---|---|---|---|---|
| M2 | beyond | PRM: flags "undefined" (BCD N/V, DIV overflow N/Z, divide by zero N/Z/V, CHK) | consistent values in thousands of cases | **suite** | undefined in the PRM, so the suite decides; CHK Z for Dn = 0 has no case either way | High (BCD, DIV overflow, CHK N/V/C); Medium (divide by zero: one case; CHK Z: no case) |
| M3 | beyond | UM: the IR/IRC queue, but no programmer-visible PC/prefetch convention | README: `pc` = next instruction, `prefetch` = the two queued words | **suite** | an interface convention the manuals do not define; maps onto the suite 1:1 | High |
| M4 | no evidence (order) | UM table 8-14: interrupt 44(5/3), totals only | no interrupt case | UM total; the position of the acknowledge among the internal clocks is chosen | the closest order to the suite's group-1 entry that fits the UM total | High (44, frame); Medium (order) |
| M6 | no evidence | (an emulator interface) | (none) | whole instructions per `run`; a halted or stopped CPU idles 4 cycles per `step` | makes results independent of slicing | High |
| M10 | beyond | UM table 8-4: only maxima (DIVU 140, DIVS 158) | data-dependent counts (every register case) | **suite** | the UM gives bounds, not the rule | High (DIVU, DIVS in range); Medium (DIVS signed-range overflow); Low (-32768 edge, no case) |
| M11 | beyond | UM: stacked PC "unpredictable", status bits 15..5 undefined | one consistent model (tens of thousands of fault cases) | **suite** | the UM leaves the frame open | Medium |
| M12 (FC) | manual kept against suite | PRM 2.0: PC-relative operand reads are program references (FC 6/2) | data space (FC 5/1) | **manual** | explicit PRM statement; shows only in an address-error frame (aef 3401) | Medium |
| M12 (I/N) | manual kept against suite | UM 6.3.9.1: an instruction is being processed, I/N = 0 | I/N = 1 at an odd jump target's first fetch | **manual** | explicit UM statement; shows only in the frame (aef 27970) | Medium |
| M13 | beyond | UM: access counts only, not their order | the transaction order (high/low word, store against closing fetch) | **suite** | the UM is silent on order; it decides fault frames and self-modifying stores | High |
| M14 | against | UM table 8-5: ADDQ.w An 4, ADDQ.l / SUBQ.w / SUBQ.l An 8 (self-contradictory) | word 8, long 6 (1522 cases) | **suite** | the table contradicts itself; MAME and the Yacht tables give 8 for long, so 6 has no second source | Medium-Low |
| M15 | manual kept against suite | UM 6.3.5: a trap stacks the next instruction's address | DIVU's only divide-by-zero case stacks the instruction's own address | **manual** | explicit UM statement against one case; listed as dev | Medium |
| M16 | against | UM table 8-14: CHK trap 40 + <ea> | 38 when Dn > bound, 40 when Dn < 0 (1000 register cases) | **suite** | one UM figure for two causes, split consistently by the suite | Medium |
| M17 | beyond | UM table 8-8: BCHG/BSET/BCLR Dn figures marked "maximum value" | 2 clocks less for bit numbers below 16 (every case) | **suite** | the UM gives the maximum without the rule | High |
| M18 | beyond | PRM: C = last bit shifted out (silent once every bit has left) | ASR by more than the size: C = X = 0 (3736 cases) | **suite** | the PRM does not cover the case; the suite is consistent | Medium-High |
| M19 | suite data | (not a manual question) | two ASL.b cases whose recorded initial and final states cannot belong together | **listed as dev** | the core follows the recorded initial state and the PRM | High |
| M20 | against | UM table 8-6: TAS 14(2/1) + <ea> | 10 + <ea> (14 for (An)) | **suite** | the table counts the operand read twice | High |
| M21 | no evidence | UM table 8-9: DBcc expired 14(3/0), not which reads | no expired case | the dropped read is at the branch target, as in the looping case | the only reading with 3 reads that reuses the looping path's fetch | Medium-Low (third read, odd target); High (14 cycles) |
| M22 | no evidence | PRM/UM: STOP's timing and PC, nothing on the queue | no STOP file | the queue keeps the STOP words until the ending exception refills it | the only choice with the table's 4(0/0) | Medium |
| M23 | against | UM table 8-12: RTR 20(2/0), MOVE to CCR 12(1/0) (self-contradictory) | RTR 5 reads, MOVE to CCR 2 reads | **suite** (totals equal) | the table contradicts itself (RTE pops the same frame in 5 reads) | High |

Not in the table, because each rests on an explicit manual statement (the suite agrees or has no
case), on the test method, or on the board:

- M1: the test method itself (how suite results are judged), not a CPU behaviour.
- M5: no KS-32 source of bus errors or function-code decoding is known (the board, not the CPU;
  UM 3.8 and 5.4.1 describe the features).
- M7: UM 6.3.2 and 6.3.9. The held-7 re-take is a UM sentence with no suite case (Medium).
- M8: PRM RESET and UM table 8-12, and all 8065 suite cases agree.
- M9: UM 6.3.9.1 and 6.3.10 (the suite has no double fault).
- M24: UM 6.3.4 (a failed acknowledge takes the spurious vector); how out-of-range answers map
  onto that is an interface convention (Medium).

## User acceptance

The pass bar (design section 3, Plan 5 Step 6): the user is shown the suite totals, the benchmark
and the smoke result above.

User verdict: accepted, 2026-10-28 ("Accept, finish").

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
