# Firmware Map Implementation Plan (Plan 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Map KS-32 OS v3.01 so that Plan 6 can build the machine from evidence. That means three things:

- a reproducible annotated listing generated from the user's own ROM;
- every hardware-facing routine named and explained;
- `docs/firmware/ks32-hardware-interface.md`, which answers design §8 with evidence and settles open items A–D where the firmware can.

**Architecture:** Everything is Python 3 standard library, run with `python -I`, under `docs/firmware/`, following the pattern of `docs/rom-investigation/tools/`.

- **`m68kdis.py`** is a stand-alone 68000 disassembler. It is a pure function from bytes to an instruction with its flow and references.
- **`annotations.py`** loads and validates the committed annotation files (`docs/firmware/annotations/*.json`). They hold labels, comments, evidence and data types, but no ROM content beyond pinned bytes of at most 16 bytes each.
- **`fwlist.py`** works in five steps:
  1. It merges the ROM and checks its hashes.
  2. It traces code by recursive descent from the vectors and the annotated entry points.
  3. It classifies every byte.
  4. It writes the listing to `build/firmware/`, which is git-ignored.
  5. It reports coverage, unresolved indirect jumps and an I/O audit.

  The reverse-engineering tasks then fill in the annotations one hardware area at a time. Ghidra is used for analysis and as an independent check of the disassembler. It is never part of the generator.

**Tech Stack:** Python 3.10+ (3.13 on this machine), standard library only, `unittest`. Ghidra 12.1.3 headless (`C:\ghidra_12.1.3_PUBLIC\support\analyzeHeadless.bat`) with one committed Java GhidraScript. PyMuPDF (already installed) only for rendering service-manual pages into the scratchpad.

**Spec:** `docs/design/2026-10-21-ks32-machine-and-juce-app-design.md` ("design"). Plan 4 is the first row of design §9 and delivers goal 1 (§2). It answers §8 and the evidence parts of §10 A–D. Earlier firmware facts are in `docs/rom-investigation/findings.md` ("findings") and `docs/rom-investigation/effect-params.md` ("effect-params"). Judgment calls go in `docs/decisions.md`. The service manual is `datasheets/ks32.pdf` ("[M]"). Manual page n is PDF page n + 3, so Figure 2 (manual p.1) is PDF page 4.

## Global Constraints

- **No ROM content is committed.** That covers listings, the merged image, Ghidra projects and exports, and any dumps (design §5). Only annotations, tools and documents are committed. The `expect` field is at most 16 bytes per entry, and the validator enforces that. `fwlist.py` refuses to write any output under `docs/`, `src/`, `tests/` or `tools/`.
- **Never commit** `docs/Ensoniq_KS32_Top.png`, `session_memory.json`, or any image rendered from the service manual. Rendered pages go to the session scratchpad only.
- **References.** The references are the firmware, [M] and the chip datasheets in `datasheets/`. MAME is a comparison point only (design §5). The top-view photo is orientation only (design §5, user's ruling).
- **Downloads.** Each needs the user's explicit permission, asked by the controller when the task reaches it:
  - the Motorola M68000 Programmer's Reference Manual (Task 1);
  - the MC68681 DUART data sheet (Task 6).

  Both go into `datasheets/` (git-ignored).
- **Ghidra.** Use only throw-away projects in the session scratchpad, deleted after use. Never open or modify the user's Ghidra projects or `~/ghidra_scripts`. The Ghidra MCP may be used only against a program imported for this plan.
- **Decisions.** Every judgment call is marked and logged. In annotations, use the `"decision": "<id>"` field. In Python, use `# DECISION <id>: <one line>`. Each one gets a `docs/decisions.md` entry under a new `## Firmware map` heading, using the existing template (options, choice, evidence, confidence, status). The ids are **F1–F8** (table below), then F9 onward in order of discovery.
- **Evidence.** Every hardware-facing annotation has a non-empty `comment` and `evidence`. Evidence cites instruction addresses (`0xC1xxxx`), manual pages (`[M p.17]`) or datasheet sections (`[ES5505 §…]`, `[ES5510 §3.5.2]`, `[MC68681 …]`). The hardware-interface document cites annotations as `` `fw:<label>` ``, and `fwlist.py --check --doc` verifies those citations.
- **Unknowns.** Questions the firmware cannot settle within a task's time-box are written up as **Unknown**, with what was tried and a confidence. They are never guessed silently.
- **ROM files.** The ROM folder is `ENS_ROM_DIR` (`C:/Users/abart/Desktop/Ensoniq KS-32/ks-32`, from `build/CMakeCache.txt`). The OS files are `ks32v301upper.bin` (even bytes, 131072 bytes, SHA-1 `8712cf2c63a31c00e98fa42e518093cac51f5214`) and `ks32v301lower.bin` (odd bytes, 65536 bytes, SHA-1 `fd5a25e23ff217a5926daae4f57dd966d392d26d`). They sit in the folder itself or in its `ks32/` sub-folder. Tests that need them read the `ENS_ROM_DIR` environment variable, falling back to `<repo>/ks-32`. When the ROMs are missing, those tests skip with "ROMs not found".
- **Tests.** Run them with `python -I -m unittest discover -s docs/firmware/tools -t docs/firmware/tools -v`. Discovery puts that directory on `sys.path`. `fwlist.py` adds its own directory to `sys.path` the same way `extract_effect_params.py:39` does.
- **Output.** All text output is UTF-8 with `\n` line endings. Output is deterministic: no timestamps or absolute paths, and everything is sorted by address.
- **Branch.** Work on branch `plan4-firmware-map` and commit after each task. Plans 4 and 5 are independent (design §9).

## Firmware-map decisions (F-ids)

| Id | Topic | Choice (my call unless noted) | Task |
|---|---|---|---|
| F1 | Listing generator | **A stand-alone Python first version**, not the Plan 5 disassembler. Design §9 runs Plans 4 and 5 in parallel, so Plan 4 cannot wait for Plan 5. A small pure-Python tool matches how the ROM investigation is reproduced, and it needs no C++ build to regenerate a listing. Ghidra cannot be the generator, because the listing would then depend on a 1 GB tool and on its analyser heuristics. Plan 5's C++ disassembler gets a ready test oracle: it must agree with `m68kdis.py` on every instruction the traversal decodes. The generator stays in Python after Plan 5 (the listing is documentation). | 2 |
| F2 | Annotation format | JSON, format id `ks32-fwmap/1`, one file per hardware area in `docs/firmware/annotations/`, schema in Task 2. JSON because Plans 6 and 7 can read the labels with the existing `ens::player::parseJson` (e.g. symbolic traces in `ks32emu`). | 2 |
| F3 | The byte-wide ROM area | The lower EPROM is a 27C512 (64 KB, `Ensoniq-KS-32-V3.01-Bin-And-Hex/readme.txt`), so the odd bytes of 0xC20000–0xC3FFFF have no chip of their own. The listing classifies them as `absent` and prints only the even bytes there (as findings 2b reads them). Task 4 records whether any instruction ever makes a word or long access into that range; Plan 6 uses that to choose open bus or a mirror. | 2, 4 |
| F4 | CPU and DUART clocks (open item A) | Settled by evidence in Tasks 6 and 11: the MIDI baud set-up, the counter/timer constants and the boot clock test. Starting point: 15.238 MHz and 4 MHz (design §7). | 6, 11 |
| F5 | Front-panel protocol gaps (open item B) | Whatever the 68000 side does not show (for example, the panel's reply timing) is chosen and logged here. | 8 |
| F6 | Interrupt map and timing precision (open item C) | Settled by evidence. The precision Plan 6 needs is logged here. | 4, 7, 11 |
| F7 | Battery RAM range and initial contents (open item D) | Settled by evidence. | 10 |
| F8 | Placement of controls that are not in Figure 2 | Figure 2 leaves out the tracks/transport section (zigzag break). Those controls are kept only where the firmware handles a button code for them. They are placed in the break region, in firmware code order, with the photo used only to orient that region. | 12 |

## Review Focus

1. **The wrong ROM.** This covers v3.00 (present on this machine), the SQ-2 2.03 OS, swapped upper and lower files, and the `.hex` files. The generator must refuse with a message naming the file and the expected SHA-1, not write a plausible wrong listing. Test: `test_wrong_rom_rejected_by_hash` (Task 2).
2. **A listing written where it could be committed.** `--out` or `--image` pointing into `docs/`, `src/`, `tests/` or `tools/` must be refused. Test: `test_refuses_output_in_tracked_dirs` (Task 2).
3. **Rare encodings decoded silently wrong.** These include 68010/68020-only opcodes, line-A and line-F words, and an odd branch target. Any of them would corrupt the code/data split. The disassembler must call them invalid, and the traversal must report them, never decode through. Tests: `test_every_opcode_word_decodes_without_error` and `test_68010_and_line_af_are_invalid` (Task 1), `test_invalid_or_odd_target_reported` (Task 2), and the Ghidra cross-check (Task 3).
4. **Annotation drift.** A label that lands inside an instruction, or a data entry that code runs into, after an edit. The check must fail and name both addresses. Tests: `test_label_inside_instruction_is_error` and `test_code_into_data_conflict_is_error` (Task 2).
5. **Windows paths and console encoding.** The repository path contains a space, comments contain non-ASCII text ("Up•Yes"), and the console is cp1252. The listing must be byte-identical between runs, written as UTF-8, and never crash on `print`. Test: `test_listing_deterministic_utf8` (Task 2).

---

### Task 1: 68000 disassembler

**Files:**
- Create: `docs/firmware/tools/m68kdis.py`
- Test: `docs/firmware/tools/test_m68kdis.py`

**Interfaces:**
- Produces:
  ```python
  @dataclass(frozen=True)
  class Insn:
      addr: int            # 24-bit
      length: int          # bytes, even, 2..10
      mnemonic: str        # lower case with size suffix: "move.l", "bne.s", "dbf", "illegal", "dc.w"
      operands: str        # Motorola syntax, see below; "" if none
      flow: str            # "next" | "branch" (conditional, incl. DBcc) | "jump" | "call" | "return" | "halt" | "invalid"
      targets: tuple       # static code targets (branch, jump or call destination); () for indirect
      refs: tuple          # ((address, size), ...) absolute or pc-relative operand addresses; size 1/2/4, 0 for lea/pea/movea #imm
  def decode(read16, addr: int) -> Insn    # read16(a) -> int 0..0xFFFF; never raises for any input words
  ```
- The syntax is Motorola:
  - Registers are `d0`–`d7` and `a0`–`a6`, with `a7` printed as `sp`, plus `sr`, `ccr`, `usp` and `pc`.
  - A number below 10 prints in decimal, anything else as lower-case `$hex`. A negative value prints with a leading `-` (`(-$4,a6)`). `trap` numbers always print in decimal.
  - Absolute operands print the 24-bit effective address: `($100).w`, `($ff8000).w` (sign-extended), `($280005).l`.
  - PC-relative operands print the target address: `($c10012,pc)`, `($c10012,pc,d0.w)`. Branch and DBcc targets print as a bare `$c10012`.
  - Index registers print as `d0.w` or `a1.l`. MOVEM lists print as ranges: `d0-d7/a0-a6`.
- Flow rules:
  - `rts`, `rte` and `rtr` are "return". `illegal` is "halt".
  - `trap` is "next": its handler returns, and inline arguments are an annotation matter (Task 2).
  - `jmp` and `jsr` with a computed EA have `targets == ()`.
  - Words that are not 68000 instructions are "invalid", with mnemonic `dc.w` and length 2. That includes 68010+ opcodes such as `movec`, `rtd` and `bkpt`, line-A (0xAxxx) and line-F (0xFxxx).

- [ ] **Step 0 (controller): Ask about the download.** Ask the user's permission to download the Motorola *M68000 Family Programmer's Reference Manual* (M68000PM/AD) into `datasheets/`. If they decline, write the disassembler from general 68000 knowledge and rely on Task 3's Ghidra cross-check as the independent reference. Note which applies in `m68kdis.py`'s module docstring.
- [ ] **Step 1: Write the failing tests.** The helper `dis(hexwords, addr=0xC10000)` builds `read16` from a word list and returns `decode(...)`.
  - `test_known_encodings`: each row is (words → mnemonic, operands, length, flow, targets, refs).
    - `4E71` → `nop`, "", 2, next
    - `4E75` → `rts`, "", 2, return
    - `4E73` → `rte`, return
    - `46FC 2700` → `move.w`, `#$2700,sr`, 4
    - `4E72 2700` → `stop`, `#$2700`, 4, next
    - `21FC 00C1 1138 0100` → `move.l`, `#$c11138,($100).w`, 8, refs ((0x100, 4),). These are the real bytes at 0xC110F0.
    - `13FC 0010 0028 0005` → `move.b`, `#$10,($280005).l`, 8, refs ((0x280005, 1),)
    - `0839 0002 0026 0025` → `btst`, `#2,($260025).l`, 8, refs ((0x260025, 1),)
    - `3038 8000` → `move.w`, `($ff8000).w,d0`, 4, refs ((0xFF8000, 2),)
    - `41F9 0028 0001` → `lea`, `($280001).l,a0`, refs ((0x280001, 0),)
    - `207C 0028 0001` → `movea.l`, `#$280001,a0`, refs ((0x280001, 0),)
    - `41FA 0010` → `lea`, `($c10012,pc),a0`, refs ((0xC10012, 0),)
    - `6100 0010` → `bsr.w`, `$c10012`, call, (0xC10012,)
    - `6604` → `bne.s`, `$c10006`, branch, (0xC10006,)
    - `60FE` → `bra.s`, `$c10000`, jump, (0xC10000,)
    - `51C8 FFFE` → `dbf`, `d0,$c10000`, branch, (0xC10000,)
    - `4EB9 00C1 1530` → `jsr`, `($c11530).l`, call, (0xC11530,)
    - `4EF9 00C1 0FD6` → `jmp`, `($c10fd6).l`, jump, (0xC10FD6,)
    - `4ED0` → `jmp`, `(a0)`, jump, ()
    - `4CDF 7FFF` → `movem.l`, `(sp)+,d0-d7/a0-a6`
    - `48E7 FFFE` → `movem.l`, `d0-d7/a0-a6,-(sp)`
    - `7001` → `moveq`, `#1,d0`
    - `4E40` → `trap`, `#0`, next
    - `4E4F` → `trap`, `#15`
    - `0000 0012` → `ori.b`, `#$12,d0`
    - `4AFC` → `illegal`, halt
  - `test_68010_and_line_af_are_invalid`: `4E7A 0801` (movec), `4E74 0000` (rtd), `A000` and `F000` each give flow "invalid", mnemonic `dc.w` and length 2.
  - `test_every_opcode_word_decodes_without_error`: for every w in 0..0xFFFF, `decode` with extension words 0x0000 and again with 0xFFFF returns an even length in 2..10. "invalid" always has length 2. No exception is raised.
  - `test_brief_extension_and_negative_displacement`: `3030 1004` → `move.w`, `(4,a0,d1.w),d0`. `302E FFFC` → `move.w`, `(-$4,a6),d0`.
- [ ] **Step 2: Run them.** Run the test command. Expected: they fail with an ImportError (no `m68kdis`).
- [ ] **Step 3: Implement `decode`.** Use table-driven decoding by opcode line (the top 4 bits), with a shared EA decoder that returns operand text, extension length and ref. Cover the full 68000 instruction set and addressing modes, including MOVEP, the BCD instructions, CHK, TAS, and MOVE to/from SR, CCR and USP.
- [ ] **Step 4: Run them.** Expected: all pass.
- [ ] **Step 5: Commit.** `feat(fwmap): stand-alone 68000 disassembler`

### Task 2: Annotation format, ROM loader and listing generator

**Files:**
- Create:
  - `docs/firmware/tools/annotations.py`
  - `docs/firmware/tools/fwlist.py`
  - `docs/firmware/annotations/rom.json`
  - `docs/firmware/README.md` (format and commands)
- Modify: `docs/decisions.md` (new `## Firmware map` heading; F1, F2, F3)
- Test: `docs/firmware/tools/test_fwlist.py`, `docs/firmware/tools/test_real_rom.py`

**Interfaces:**
- Consumes: `m68kdis.decode`, `Insn` (Task 1).
- Produces the annotation format, `ks32-fwmap/1`.
  - Every file is `{"format": "ks32-fwmap/1", "entries": [...]}`.
  - `rom.json` also holds `"files"` (name → size and SHA-1, from Global Constraints) and `"regions"`: a list of `{"start", "end", "kind", "name"}` (end exclusive). The kind is `rom`, `rom_even` (0xC20000–0xC40000), `ram` or `io`.
  - The initial `io` regions are `otis` 0x200000–0x200040, `esp` 0x260000–0x260200 and `duart` 0x280000–0x280020 (findings "What was tried"). Task 4 confirms or extends them.
  - Entry fields, all hex as strings without a prefix:

  | Field | Meaning |
  |---|---|
  | `addr` | Required. 24-bit address. |
  | `type` | `code`, `byte`, `word`, `long`, `ptr` (long), `ptr16` (word; needs `base`, either hex or `"rom_ptr"` for the 0xC117FA rule `(0xC00000 if w >= 0x50E4 else 0xC10000) \| w`), `jumptable` (word offsets from `base`), `text` (Ensoniq token string: 0x9E nn = token nn from the word table at 0xC05972, 0x9F = end), `ascii`, `fill`, `unknown`, `ram`, `io`. |
  | `count` | Element count for the data types; the default is 1. In `rom_even` regions it counts even bytes. |
  | `end` | Exclusive end, for `fill` and `unknown`. |
  | `size` | Byte size of a `ram` or `io` symbol. |
  | `to` | For `ptr`/`ptr16`/`jumptable`: `code` or `data`. |
  | `label` | Optional. Matches `[A-Za-z_][A-Za-z0-9_]*` and is unique across all files. |
  | `comment`, `evidence` | Optional strings. Required (non-empty) for routines that the I/O audit flags. |
  | `decision` | An F-id. |
  | `inline` | On a `code` entry: callers (`jsr`/`bsr`, or `trap #k` whose vector points here) continue at +n. The n bytes after each call are typed `word`. |
  | `targets` | On a `code` entry at an indirect `jmp`/`jsr`: a list of hex targets (it may be empty if the comment explains why). It marks the jump resolved. |
  | `expect` | At most 16 bytes as hex. The ROM bytes at `addr` must equal it. |
- Produces (Python):
  ```python
  @dataclass
  class Annotations: files: dict; regions: list; entries: dict; labels: dict   # entries: addr -> dict (normalised ints)
  def load_annotations(directory: str) -> tuple[Annotations, list[str]]  # errors; never raises on bad JSON/schema
  def load_rom(rom_dir: str, ann: Annotations) -> tuple[bytes | None, str]   # 0x40000-byte image of 0xC00000.., or (None, error)
  @dataclass
  class Analysis:
      insns: dict          # addr -> Insn
      byte_class: bytearray  # one class code per image byte
      errors: list         # strings, each naming the address(es)
      indirect: list       # addrs of unresolved indirect jmp/jsr
      io_audit: list       # (insn addr, routine addr or None, ref addr) for unannotated sites
      refs: dict           # non-ROM address -> list of (insn addr, size), for --refs
  def analyse(img: bytes, ann: Annotations) -> Analysis
  def render_listing(img: bytes, ann: Annotations, an: Analysis) -> str
  def main(argv: list[str]) -> int
  ```
- The CLI:
  ```
  python -I docs/firmware/tools/fwlist.py [rom_dir, default ks-32] [--ann docs/firmware/annotations]
      [--out build/firmware/ks32v301.lst] [--report] [--refs] [--check] [--doc FILE] [--image PATH]
  ```
  - `--check` exits 1 on any error and prints `fwmap check: OK (<n> entries, <n> insns)` on success.
  - `--report` prints:
    - bytes per class (code, data, text, fill, unknown, absent, unclassified), per region;
    - unresolved indirect jumps;
    - invalid instructions reached;
    - the I/O audit, per `io` region: `io audit: duart 0 unannotated of 37 sites`.
  - `--refs` prints every referenced address outside ROM, grouped by 4 KB block, with counts and access sizes.
  - `--doc` checks that every `` `fw:<label>` `` in FILE exists and that every 0xC0xxxx–0xC3xxxx address it cites is classified code or data.
  - `--image` writes the merged image (for Task 3).
- Analysis rules:
  - **Seeds.** The reset PC is the long at 0xC00004. The seeds are that PC, every distinct vector-table long at 0xC00008–0xC000FF that points into ROM, every `code` entry, every `ptr`/`ptr16`/`jumptable` element with `to: code`, and the `targets` lists.
  - **Recursive descent.** "branch" follows both paths, "call" follows the target and the fall-through (+`inline`), and "jump" follows its target only.
  - **Errors.** These are reported as errors:
    - decoding into a data entry or into absent bytes;
    - overlapping instructions;
    - a label or data entry inside an instruction;
    - an odd or out-of-ROM target;
    - invalid flow reached;
    - an `expect` mismatch;
    - a duplicate label;
    - overlapping data ranges.
  - **Fill.** Runs of 16 or more 0xFF bytes that nothing else claims are classed `fill`.
  - **Absent bytes.** The odd bytes of `rom_even` regions are `absent` (DECISION F3).
  - **Audit.** An instruction with a ref inside an `io` region belongs to the nearest labelled `code` entry at or below it, with no data entry in between. That entry needs a non-empty `comment` and `evidence`, or the site counts as unannotated.
- Listing format:
  - Entry comments come first as `; ` lines, then `; evidence: …` and `; decision: F…`, then a `label:` line.
  - Instruction lines are `%06X  <hex words, 24 wide>  <mnemonic padded to 8><operands>`.
  - Exact matches of operand addresses are replaced by labels. Addresses inside sized `ram`/`io` symbols print as `label+n`, with n in the number format of Task 1.
  - Unlabelled call targets get `sub_XXXXXX`, and other targets get `loc_XXXXXX`.
  - Data prints as `dc.b`, `dc.w` or `dc.l`. Text prints as `dc.b` with the decoded string in a trailing comment.

- [ ] **Step 1: Write the failing tests** (`test_fwlist.py`, synthetic images). The helpers are `make_img(words_at: dict)`, which returns a 0x40000-byte image filled with 0xFF with the reset vector at 0xC00004, and `ann_dir(tmp, files: dict)`, which writes annotation JSON.
  - `test_traversal_follows_branches_calls_and_vectors`: reset → `bne.s` → two paths, one `bsr.w` to a subroutine ending in `rts`, and vector 32 pointing to a third routine. All instruction addresses are in `insns`, and the bytes in between are `fill`.
  - `test_jumptable_targets_are_seeds`: a `jumptable` of 3 word offsets with `base` makes all 3 targets code.
  - `test_inline_args_skip_after_call`: a `jsr` to a routine with `"inline": 2` makes the next 2 bytes `word` data, and decoding resumes after them.
  - `test_indirect_jump_reported_until_targets_given`: `jmp (a0)` is listed in `indirect`. With a `code` entry there carrying `"targets": ["C10100"]`, it is not listed and 0xC10100 is code.
  - `test_invalid_or_odd_target_reported`: a branch to an odd address and a path into `A000` each produce an error naming the address.
  - `test_code_into_data_conflict_is_error`: code that falls through into a `word` entry gives an error with both addresses.
  - `test_label_inside_instruction_is_error`: a label at instruction address + 2 gives an error.
  - `test_duplicate_label_and_overlap_rejected`: the same label in two files, and two overlapping `word` ranges, each give an error from `load_annotations`.
  - `test_expect_mismatch_fails_check` and `test_expect_over_16_bytes_rejected`.
  - `test_io_audit_flags_uncommented_routine`: a routine containing `move.b #1,($280005).l` is flagged until its entry has a comment and evidence. A `lea ($280001).l,a0` site counts too.
  - `test_text_token_decoding`: bytes `"AB" 9E 01 9F` with token 1 = "Cd" decode to "ABCd".
  - `test_rom_even_region_and_absent_bytes`: in 0xC20000–0xC40000 the odd bytes are `absent`, and a `byte` entry with `count` 4 covers 4 even bytes.
  - `test_refuses_output_in_tracked_dirs`: `main([... "--out", "docs/x.lst"])` and `--image tools/x.bin` return 2 with a message, and no file is written.
  - `test_listing_deterministic_utf8`: render the listing twice, including a comment "Up•Yes", and get identical strings. Writing it with `--out` gives UTF-8 bytes. Labels replace `($280005).l` with `(duart_reg+4).l` for an `io` symbol at 0x280001 of size 0x20.
  - `test_wrong_rom_rejected_by_hash`: a temp dir with wrong-content files of the right names and sizes makes `load_rom` return None and an error naming the file and the expected SHA-1. Swapped upper/lower files are rejected by size or hash.
  - `test_doc_citations_checked`: a doc citing `` `fw:nosuch` `` fails `--doc`.

  `test_real_rom.py` (skips without ROMs):
  - `test_real_rom_hash_and_reset_vector`: `load_rom` succeeds. The SSP long at 0xC00000 is 0 and the reset PC is 0xC10FD6.
  - `test_real_rom_check_runs`: `analyse` completes and the reset PC is code.
- [ ] **Step 2: Run them.** Expected: they fail with ImportError.
- [ ] **Step 3: Implement** `annotations.py` and `fwlist.py` to the rules above. Mark the absent-byte rule `# DECISION F3`. Write `rom.json`. Write `README.md` with the format table, the commands, and the rule that the listing stays in `build/`.
- [ ] **Step 4: Log the decisions.** Write F1, F2 and F3 in `docs/decisions.md` under `## Firmware map`.
- [ ] **Step 5: Run the tests.** Expected: all pass, and the real-ROM tests run, not skip. Then `python -I docs/firmware/tools/fwlist.py ks-32 --report` prints a baseline. Record its class totals in the commit message body.
- [ ] **Step 6: Commit.** `feat(fwmap): annotation format and listing generator (F1-F3)`

### Task 3: Disassembler cross-check against Ghidra

**Files:**
- Create: `docs/firmware/tools/ghidra/ExportInstructions.java`, `docs/firmware/tools/compare_ghidra.py`
- Modify: `docs/firmware/README.md` (how to run the check)

**Interfaces:**
- `ExportInstructions.java` is a GhidraScript. Its arguments are `<insns.tsv> <functions.tsv>`. It writes one line per instruction (`addr<TAB>length<TAB>mnemonic<TAB>operands`, hex address) and one line per function start.
- `compare_ghidra.py <rom_dir> <insns.tsv>`:
  - It decodes each Ghidra address with `m68kdis`.
  - It compares length, mnemonic (lower case; a documented alias table such as Ghidra `dbra` = `dbf`) and normalised operands. Normalising strips `0x`/`$`, spaces and case, and turns PC-relative displacements into absolute targets on both sides.
  - It prints counts per category and the first 20 examples of each.
  - It exits 1 on any length mismatch or any unexplained difference.

- [ ] **Step 1: Write the merged image to the scratchpad.** Run `python -I docs/firmware/tools/fwlist.py ks-32 --image <scratch>/ks32v301.bin`.
- [ ] **Step 2: Run Ghidra headless.** Use a throw-away project in `<scratch>/gh`: `analyzeHeadless.bat <scratch>\gh ks32 -import <scratch>\ks32v301.bin -loader BinaryLoader -loader-baseAddr 0xC00000 -processor 68000:BE:32:default -scriptPath docs\firmware\tools\ghidra -postScript ExportInstructions.java <scratch>\ghidra_insns.tsv <scratch>\ghidra_functions.tsv -deleteProject`. Seed it at the vector targets, and enable the Aggressive Instruction Finder with a `-preScript` that sets that analysis option, as Plan 2 did (effect-params §7). Both TSVs stay in the scratchpad. They are used again in Task 5.
- [ ] **Step 3: Write and run `compare_ghidra.py`.** Expected: 0 length mismatches. Allowed differences come only from the alias table, plus Ghidra-valid opcodes that are 68010/68020-only, where `m68kdis` says invalid. Check each such opcode against the PRM (if Task 1 got it) and list it by opcode in the README. Fix any real `m68kdis` bug with a new row in `test_known_encodings` first.
- [ ] **Step 4: Run the unit tests.** Expected: all pass.
- [ ] **Step 5: Commit.** `test(fwmap): cross-check m68kdis against Ghidra on the OS image`

### Task 4: Memory map, vectors, boot and trap services (time-box: one session)

**Files:**
- Create:
  - `docs/firmware/annotations/vectors.json`
  - `docs/firmware/annotations/boot.json`
  - `docs/firmware/annotations/traps.json`
  - `docs/firmware/ks32-hardware-interface.md` (the skeleton, below, with §1 and the boot part of §2 filled in)
- Modify: `docs/firmware/annotations/rom.json` (the `ram` and `io` regions found), `docs/decisions.md` (F3 evidence, F6 start)

Skeleton of `ks32-hardware-interface.md`:

0. Sources and how to reproduce
1. Memory map
2. Interrupt map (item C)
3. DUART and clocks (item A)
4. MIDI
5. Front-panel protocol (item B)
6. Keyboard and analog controls
7. OTIS and ESP use
8. Battery RAM and memory card (item D)
9. Self-tests and error codes
10. Timing dependencies
11. Open items A–F: status table
12. What Plans 5 and 6 must implement

Each section has the parts **Answer**, **Evidence** (`fw:` labels and addresses), **Confidence** and **Unknown**.

Questions to answer, with evidence:

- [ ] **Step 1: Vector table.** Type 0xC00000–0xC000FF as `ptr to code` (64 longs). Label each handler:
  - the reset PC 0xC10FD6;
  - the error stubs 0xC11530–0xC11560;
  - the default stub 0xC11560 and the spurious/level-1 stub 0xC1155C;
  - traps 0–15 (0xC1150C, 0xC111FA, … 0xC112EC).

  The ROM table sends every autovector to the default stub, so find how the run-time table is built. Answer these:
  - Does boot copy vectors into RAM at 0x000000?
  - Which slots are written later? `move.l #…,($100).w` at 0xC110F0 and 0xC11138 writes vector 64 = 0x100, the first user vector, which is what a DUART IVR would select.

  List every write into 0x000000–0x0003FF.
- [ ] **Step 2: Reset path.** Name each step of the boot from 0xC10FD6 to the play screen as a `code` entry with a comment: SR and stack, RAM test or clear, each device init call, the clock test (findings B, 0xC110DC), battery-RAM validity, the panel hand-shake and the main loop.
- [ ] **Step 3: Memory map.** Run `fwlist.py --refs` and resolve every 4 KB block outside ROM:
  - **RAM.** Find its extent: the low RAM at 0 and the 0xFF0000–0xFFFFFF block (SSP = 0 wraps to the top). Also check whether they are one RAM seen twice, from the RAM-test bounds.
  - **Devices.** Confirm the device ranges and their register strides.
  - **Other blocks.** Add any other block as an `io` region `unknown_XXXXXX`, with the routine that touches it, and hand it to the task for its area.
  - **Byte-wide area (F3).** Check whether any word or long access goes into 0xC20000–0xC3FFFF.

  Write §1 as a table: range, kind, device, access width, evidence.
- [ ] **Step 4: Trap services.** For each of traps 0–15, give a label and a one-line purpose: register inputs and outputs, and whether it takes inline arguments (then add `inline`). Write the SR mask levels used (`move #$27xx,sr`, `ori/andi #…,sr` sites) into §2.
- [ ] **Step 5: Verify.** `fwlist.py ks-32 --check` prints OK. `--report` shows no unresolved indirect jumps inside the reset path. Run the unit tests.
- [ ] **Step 6: Commit.** `docs(fwmap): memory map, vectors, boot path and trap services`

### Task 5: Whole-image code/data separation (time-box: two sessions)

Design goal 1 asks for code and data separated for the whole of OS v3.01. Routines with no hardware access keep auto labels; they need only the right class.

**Files:**
- Create: `docs/firmware/annotations/code-data.json`

- [ ] **Step 1: Type the known data.** Each entry gets a comment citing its source, and an `expect` of the first 4–8 bytes:
  - FC table 0xC09F64 (768 words);
  - level table 0xC0A72C (128 words) and volume table 0xC0A62C (128 words);
  - loop-mode CR table 0xC0A83C;
  - token table 0xC05972;
  - the sample-map tables 0xC1E636 and 0xC1FA8E, and the rom3 directory copy 0xC1DDD2;
  - effect records 0xC1FB8A (13 × 16 bytes);
  - the ESP pointer table 0xC1FC5A (11 longs) and the DRAM tables 0xC1FC92 and 0xC1FD16;
  - the RAM-copy source 0xC1D872 (0x560 bytes);
  - the ESP programs (even bytes from 0xC20000);
  - the factory sounds (even bytes from 0xC38080).

  The sources are findings, effect-params, and the constants at the top of `docs/rom-investigation/tools/extract_effect_params.py`.
- [ ] **Step 2: Iterate on the unclassified ranges.** Repeat until `--report` shows 0 unclassified bytes:
  - For each unclassified range, use Ghidra's function starts (Task 3's `ghidra_functions.tsv`) and the cross-references.
  - Add a `code` seed only where decoding from it is valid and agrees with its callers.
  - Type pointer and jump tables, and resolve each indirect jump with `targets` or a `jumptable`.
  - Type strings as `text` or `ascii`, and constant tables as `byte`/`word`/`long`, with a short comment.
- [ ] **Step 3: Budget for the unknown.** Bytes that cannot be classified with evidence go into explicit `unknown` entries, each with a comment saying what was tried. If they total more than 2 KB, stop and report to the controller with the list before continuing.
- [ ] **Step 4: Verify.** `--check` is OK. `--report` shows unclassified 0, no unresolved indirect jumps and no invalid instructions reached, and prints the `unknown` total. Re-run `compare_ghidra.py`; it still passes.
- [ ] **Step 5: Commit.** `docs(fwmap): whole-image code/data separation`

### Task 6: DUART and MIDI

**Files:**
- Create: `docs/firmware/annotations/duart.json`, `docs/firmware/annotations/midi.json`
- Modify: `docs/firmware/ks32-hardware-interface.md` (§3, §4, the DUART part of §2), `docs/decisions.md` (F4 evidence)

- [ ] **Step 0 (controller): Ask about the download.** Ask the user's permission to download the MC68681 DUART data sheet into `datasheets/`. If they decline, name the registers from the standard MC68681 map. Mark every register-level claim "to confirm against the data sheet in Plan 6" and log it as an F-id.
- [ ] **Step 1: Register symbols.** Add `io` entries for the 16 DUART registers at 0x280001 + 2n, with read and write names, for example `duart_sra_csra` and `duart_isr_imr`.
- [ ] **Step 2: Initialisation.** Record every register value the firmware writes at init and later: MR1/MR2 A and B, CSR A and B, CR, ACR, CTUR/CTLR, IMR, IVR, OPCR, and OP set/reset. Each value gets an `expect` on its instruction.
- [ ] **Step 3: Interrupt handler.** Annotate the vector-64 handler(s) (0xC11138 and 0xC11734 are installed there) and their dispatch on ISR bits: RxRDY and TxRDY per channel, the counter/timer, and input-port change. Note what each branch does and the buffer it uses (`ram` entries).
- [ ] **Step 4: Ports.** Give a bit map for the output port (OP6 = ESPHALT, findings B; the rest from their use) and for the input port (from the IP and IPCR reads).
- [ ] **Step 5: Channels and clocks (item A).**
  - Settle which channel carries MIDI and which carries the panel (§8).
  - Derive the MIDI baud set-up from CSR/ACR/CTUR/CTLR. Then solve for the DUART X1 clock that gives exactly 31 250 baud, and compare it with 4 MHz.
  - Record the counter/timer use: rate, purpose and handler.
  - Write the evidence into F4.
- [ ] **Step 6: MIDI path.**
  - Receive: the receive buffer, its size, overrun handling, and the top-level dispatch (note on/off, controllers, program change, SysEx entry). Name the dispatch routines only; SysEx is not decoded.
  - Transmit: the transmit path.
  - Whether the firmware ever enables a hardware MIDI thru function.
- [ ] **Step 7: Verify.** `--check` OK, and `--report` shows `io audit: duart 0 unannotated`. Run the unit tests.
- [ ] **Step 8: Commit.** `docs(fwmap): DUART, clocks and MIDI path`

### Task 7: OTIS and ESP drivers; interrupt map

**Files:**
- Create: `docs/firmware/annotations/otis.json`, `docs/firmware/annotations/esp.json`
- Modify: `docs/firmware/ks32-hardware-interface.md` (§2 complete, §7), `docs/decisions.md` (F6)

- [ ] **Step 1: Register symbols.** Add `io` entries for the OTIS registers at 0x200000 (16-bit, `[ES5505]` register map, the page register) and the ESP host registers at 0x260001 + 2n (`[ES5510]`).
- [ ] **Step 2: Annotate the known routines** (findings and effect-params):
  - OTIS init 0xC1AB36;
  - voice set-up 0xC1ACA4;
  - voice start 0xC1AE56 and 0xC1B03E;
  - the zero-crossing sample read 0xC1B0B2;
  - release 0xC1A880;
  - the clock-test voice 0xC110DC–0xC11186;
  - effect select 0xC0F9C2;
  - effect load 0xC1C8C0, the DRAM tables 0xC1C966 and mix/unhalt/fade 0xC1CB76;
  - host write 0xC1CC80 and the ESPHALT routines 0xC1CD3C and 0xC1CD4A;
  - ESP boot 0xC1C800.

  Add every other routine the I/O audit flags for `otis` or `esp`.
- [ ] **Step 3: OTIS interrupts and analog input.** Find whether the firmware enables or services the OTIS IRQ (IRQV reads, its handler and level), and whether it reads the OTIS A/D or POT register `[ES5505]`. Hand any analog use to Task 9 with the routine labels.
- [ ] **Step 4: Interrupt map (item C).** Complete §2: every 68000 interrupt level in use, the device behind it, the handler `fw:` label, what the handler does, and the SR masks used around critical sections. Write F6 with the timing precision the evidence implies, for example "the OTIS IRQ is serviced per voice end, so the scheduler slice of one voice slot is enough" or the opposite.
- [ ] **Step 5: Verify.** `--check` OK. `io audit: otis 0 unannotated` and `esp 0 unannotated`. Run the unit tests.
- [ ] **Step 6: Commit.** `docs(fwmap): OTIS and ESP drivers, interrupt map (F6)`

### Task 8: Front-panel protocol (item B; time-box: two sessions)

**Files:**
- Create: `docs/firmware/annotations/panel.json`
- Modify: `docs/firmware/ks32-hardware-interface.md` (§5), `docs/decisions.md` (F5)

[M] gives the following. The keypad/display board is separate, connected by a 6-pin cable [M p.20]. When it is powered but not "in proper communication with the main board", it enters its own self-test and shows "BUTTON" and a number [M p.7]. Holding the 68000 in HALT (pin 17 to ground) forces that self-test [M p.7]. Together these point to a serial link with a panel controller. Task 6 says which DUART channel it uses.

- [ ] **Step 1: Link and hand-shake.** Find what the 68000 sends at boot and what it waits for, the time-outs, and what happens with no reply. That is the "proper communication" the panel's self-test depends on.
- [ ] **Step 2: Input messages.** Decode the button press and release codes, and any slider, data-entry or other values the panel sends. Find the dispatch table from button code to handler. Type it as a `jumptable` or `ptr16` with a label per code.
- [ ] **Step 3: Check against the manual's chords.** Trace the code that recognises each chord, and record the button codes they use:
  - Edit Seq/Preset, then hold Bank 9 + Screen 1: the version screen [M p.2];
  - Bank 9 + Screen 9: reinitialise [M p.1];
  - Bank 9 + Screen 0: Analog Tests [M p.17].

  The codes for Edit Seq/Preset, Bank 9, Screen 0, Screen 1, Screen 9 and Up•Yes must agree with Step 2's table. Also find what "Press any button" after the low-battery warning accepts [M p.9].
- [ ] **Step 4: Output messages.** Decode the following:
  - the display commands (cursor addressing, character write, clear, blink, cursor style);
  - the display geometry, from the cursor-address arithmetic and the line lengths of the display strings;
  - the character set: the codes used beyond ASCII and any user-defined characters;
  - the LED commands: bit or number per LED, and on, off and blink.

  List which buttons have LEDs according to the firmware, and compare that with Figure 2's LED squares.
- [ ] **Step 5: Build the button table.** In §5, map every button code to its control and handler `fw:` label, with the evidence for the mapping. The evidence can be a chord trace, a display string the handler shows, or the mode it enters. Codes that the firmware handles but Figure 2 does not show (expected: Track 1–8, Record, Stop, Play) are listed with their handler and the strings they show. Codes that cannot be matched to a control are listed as Unknown.
- [ ] **Step 6: Log F5.** Record the protocol details the 68000 side does not settle, for example reply latency, auto-repeat and what the panel sends at power-up.
- [ ] **Step 7: Verify.** `--check` OK. `--doc docs/firmware/ks32-hardware-interface.md` OK. Run the unit tests.
- [ ] **Step 8: Commit.** `docs(fwmap): front-panel protocol, button, LED and display map (F5)`

### Task 9: Keyboard, wheels, pedals, sliders and analog inputs

**Files:**
- Create: `docs/firmware/annotations/keyboard-analog.json`
- Modify: `docs/firmware/ks32-hardware-interface.md` (§6)

- [ ] **Step 1: Keyboard path.** Find how key events reach the 68000 (the panel link, a separate port, or an interrupt), the event format, how note-on velocity and release velocity are produced or measured, and the note-number range (76 keys [M p.2]). Annotate the routines from the hardware event to the voice allocator's entry.
- [ ] **Step 2: Analog controls.** Start from the Analog Tests page (the string near 0xC05E0E, reached by the Task 8 chord). It shows Mod and pitch wheel, CV pedal, `Vol=`, `Knob=` (Data Entry), `Bat=` and `Pressure=` [M p.17–18]. Trace each displayed value back to its source: a panel message, an OTIS A/D read, or a DUART input. Record the channel or select bits, the scaling to 0–127 or 0–255, the read rate, and the CV-pedal absent value of 127 [M p.17].
- [ ] **Step 3: Switches.** Find how the foot switch (sustain) and any other pedal input are read [M, Test Procedures 3, Foot Switch Test].
- [ ] **Step 4: Verify.** `--check` OK and `--doc` OK. Run the unit tests.
- [ ] **Step 5: Commit.** `docs(fwmap): keyboard, controller and analog input paths`

### Task 10: Battery RAM, memory card and reinitialisation (item D)

**Files:**
- Create: `docs/firmware/annotations/battery-card.json`
- Modify: `docs/firmware/ks32-hardware-interface.md` (§8), `docs/decisions.md` (F7)

- [ ] **Step 1: Battery RAM.** Find the battery-backed range ([M p.1] says 64 KB of RAM), the validity check at boot (signature or checksum and its location), the power-cycle counter behind "seven slow power cycles reinitialise" [M p.2], and the reinitialise path, both from the chord and automatic.
- [ ] **Step 2: Initial contents (item D).** List every block that reinitialisation copies or builds, as ROM source → RAM destination and length: for example 0xC38080 → 0xFF8C2E (0xC12860) and 0xC1D872 → 0xFFD1C6 (0xC11938), plus whatever else it does. With that list, Plan 6 can create a fresh battery image by running the firmware's own path.
- [ ] **Step 3: Battery-low check.** Find the analog channel (from Task 9), the threshold compare (the manual's normal range is 125–205 [M p.10]), and where "WARNING! Battery low" (0xC05DEB) is shown. Also find the card battery's variant [M p.26].
- [ ] **Step 4: Memory card.** Find the card's address range, how a card is detected (input port, a probe read, or something else), how card type and size are identified, how card battery-low is detected, and the read/write routines. Decide nothing about emulating the card (item E stays with Plan 6); record only the facts.
- [ ] **Step 5: Log F7.** Write F7 with the evidence and the recommendation for Plan 6's battery image: its range, and starting from all-0xFF or zero so the firmware's own reinitialisation runs.
- [ ] **Step 6: Verify.** `--check` OK and `--doc` OK. Run the unit tests.
- [ ] **Step 7: Commit.** `docs(fwmap): battery RAM, card and reinitialisation (F7)`

### Task 11: Self-tests, error codes and timing dependencies

**Files:**
- Create: `docs/firmware/annotations/selftest-timing.json`
- Modify: `docs/firmware/ks32-hardware-interface.md` (§9, §10, §11), `docs/decisions.md` (F4 and F6 status)

- [ ] **Step 1: Self-tests.** Annotate each boot or service test: RAM, ROM checksum (the readme's checksums are 8470 lower and 8269 upper; check whether the firmware computes them), the ESP test (error 33 [M]) and the clock test. Give the error-code table with the code that raises each error and the [M] page.
- [ ] **Step 2: Busy-wait loops.** List every one reachable from hardware code (DBcc and counted loops, and polls with time-outs): the address, the iteration count, what it waits for, and the 68000 cycles it implies at 15.238 MHz. Mark the loops whose outcome depends on CPU speed against a device. Those are the ones Plan 5's cycle timing and Plan 6's scheduler must satisfy (items C and F).
- [ ] **Step 3: The clock test (item A).** Re-derive the result of the clock test 0xC110DC–0xC11186 for the candidate clocks: OTIS = CPU = 15.238 MHz, DUART 4 MHz → 32 voices (findings B). Combine it with Task 6's baud result. Write F4 as decided, or as open with the remaining range.
- [ ] **Step 4: Status table.** Fill §11 with A–D (plus notes on E and F): the status (settled, partly or open), the decision id and the evidence.
- [ ] **Step 5: Verify.** `--check` OK and `--doc` OK. Run the unit tests.
- [ ] **Step 6: Commit.** `docs(fwmap): self-tests, error codes and timing dependencies (F4, F6)`

### Task 12: Revise the front-panel reference

**Files:**
- Modify: `docs/reference/ks32-front-panel.md`, `docs/decisions.md` (F8)

The user's ruling (design §5): the layout comes only from [M] Figure 2 (manual p.1, PDF page 4) and from the firmware code that reads the panel. The photo is orientation only.

- [ ] **Step 1: Read Figure 2 at high resolution.** Render PDF page 4 at 300 dpi into the scratchpad: `python -I -c "import pymupdf; pymupdf.open('datasheets/ks32.pdf')[3].get_pixmap(dpi=300).save(r'<scratch>/fig2.png')"`. Do not commit the image. Transcribe every control, label and LED square, with its position as group, row and column.
- [ ] **Step 2: Rewrite the document.** Change the following:
  - **Sources.** They become [M Fig. 2], [M p.n] text and [F] firmware (`fw:` labels). The photo is named once, as orientation only, and cited for no fact. Remove the photo-only label transcriptions:
    - the upper-row two-line labels;
    - "Lock";
    - "Continue";
    - the Default Preset Settings table;
    - the jack legend.
  - **Figure 2 controls.** One table row per control. The columns are: control, position in Figure 2, LED (yes or no, with the firmware LED id), button code [F], handler `fw:` label, and notes.
  - **Sliders and wheels.** They come from [M p.17] text plus the firmware's analog path (Task 9), not from the photo.
  - **Controls outside Figure 2.** Track 1–8, Record, Stop and Play appear only where Task 8 found their codes. They get the label text the firmware shows, or "label not established". Their placement follows DECISION F8.
  - **Chords (§8 of the document).** Each one names its button codes and the handler that recognises it.
  - **Open points (§9 of the document).** Each is replaced by its answer or by an explicit Unknown.
- [ ] **Step 3: Log F8.** Write F8.
- [ ] **Step 4: Check the citations.** `python -I docs/firmware/tools/fwlist.py ks-32 --doc docs/reference/ks32-front-panel.md` OK. Every Figure 2 control has a row with a button code or an explicit Unknown.
- [ ] **Step 5: Commit.** `docs: front-panel reference from Figure 2 and the firmware panel code (F8)`

### Task 13: Completion and verification

**Files:**
- Create: `docs/verification-plan4.md`
- Modify: `docs/firmware/ks32-hardware-interface.md` (§0, §12), `docs/decisions.md` (status lines)

- [ ] **Step 1: Hand-off list.** Fill §12 with what Plans 5 and 6 must implement:
  - the 68000 features the firmware uses (any MOVEP, TAS, STOP, or trace use; exception frames relied on);
  - the DUART registers and modes used;
  - the panel protocol to emulate;
  - the interrupt wiring;
  - the clocks;
  - the battery image recipe;
  - the timing loops.

  Also give the Plan 5 oracle: the C++ disassembler must match `m68kdis.decode` on every instruction `analyse` reaches.
- [ ] **Step 2: Coverage of §8.** For each §8 bullet, check that §1–§10 give an Answer with at least one `fw:` citation and a Confidence. Fix any gap, or mark it Unknown with what was tried.
- [ ] **Step 3: Run everything.**
  1. Run the unit tests (all pass, the ROM tests run).
  2. `fwlist.py ks-32 --check --doc docs/firmware/ks32-hardware-interface.md` and `--doc docs/reference/ks32-front-panel.md` (both OK).
  3. `fwlist.py ks-32 --out build/firmware/a.lst`, then `--out build/firmware/b.lst`, then `cmp` (identical).
  4. `--report`: unclassified 0, every `io` audit at 0, and the `unknown` total recorded.
  5. `compare_ghidra.py` passes.
- [ ] **Step 4: Repository hygiene.** `git ls-files docs/firmware` lists only `.json`, `.md`, `.py` and `.java` files. `git status` shows `docs/Ensoniq_KS32_Top.png` and `session_memory.json` still untracked. No `expect` is over 16 bytes (`--check` enforces it).
- [ ] **Step 5: Write `docs/verification-plan4.md`.** Include the class totals, the audit counts, the Ghidra comparison counts, the §8 checklist, the status of A–D, and the open F decisions.
- [ ] **Step 6: User review (the pass bar for Plan 4).** Ask the user to read `docs/firmware/ks32-hardware-interface.md` and the revised `docs/reference/ks32-front-panel.md`, and to generate the listing from their own ROM folder with the README command. Record their verdict in `docs/verification-plan4.md`.
- [ ] **Step 7: Commit and finish.** Commit `docs: Plan 4 verification`, then use superpowers:finishing-a-development-branch.
