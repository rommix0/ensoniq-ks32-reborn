# Trace format

A trace is a UTF-8 text script that drives one ES5505 (OTIS) and one ES5510 (ESP) through a fixed
sequence of host writes and frames. Our cores (`ens_trace`) and the MAME reference harness
(Task 12) read the same file, so both chips get identical input and their PCM output can be
compared with `pcm_diff`.

The parser is `ens::parseTrace` in `src/trace/trace.h` (library `ens_trace_parse`). It does not
depend on the chip classes, so the MAME harness reuses it.

## Syntax

- One command per line. Tokens are separated by spaces or tabs. CRLF line ends are accepted.
- Blank lines and lines whose first non-blank character is `#` are ignored. There are no inline
  comments (a `#` after a command is a parse error), because wave paths may contain it.
- Command names are lower case.
- Decimal fields (`bank`, `reg`, `n`, `words`, level) are plain digits. Hex fields are hex digits
  with an optional `0x` prefix, in either case.
- Any error stops the parse and names the line: `<trace>:<line>: <message>`.

## Commands

```text
wave <bank 0|1> <path>     raw little-endian uint16 word image (up to 1M words)
dram <words>               ESP DRAM size in words (default 65536)
otis <reg 0-15> <hex16>    OTIS host write
esp <addr hex8> <hex8>     ESP host write
esphalt <0|1>              ESP HALT pin
espidle <n>                n ESP idle steps (halted instruction periods)
frames <n>                 run n frames and emit their output
```

- `wave`: the file is the exact image `docs/rom-investigation/tools/convert_wave_roms.py` writes
  (`bank0.bin`, `bank1.bin`): word `i` is bytes `2i` (low) and `2i+1` (high), indexed by the OTIS
  20-bit word address. Shorter images are fine; reads past the end return 0. The path is the rest
  of the line (spaces allowed). A relative path is resolved against the directory of the trace
  file (`parseTrace`'s `baseDir`). An unreadable file, an odd byte count or more than 1M words is
  an error reported on the `wave` line. A bank that is not given reads as all zero.
- `dram`: a power of two from 1 to 2^24 words (24-bit ESP word addresses). The default is 65536,
  the KS-32 value (MEMSIZ = 0x0000FF, `docs/rom-investigation/findings.md` section D). Addresses
  the ESP core generates are masked to the size; I/O space reads 0 and ignores writes.
- `wave` and `dram` must come before the first `frames`.
- `otis`: `Otis::write(reg, value)`. Voice and page selection is the usual PAGE register (reg 15).
- `esp`: `Esp::hostWrite(addr, value)`, the 8-bit host map (GPR `$00-$02`, INSTR `$03-$08`, DIL,
  DOL, DADR, host control `$12`, RAM control `$14`, serial control `$18`, halt enable `$1F`,
  select registers `$80/$A0/$C0/$E0`).
- `esphalt 1` raises the HALT pin. If the ESP is still running, the runner also writes host `$1F`
  (halt enable), as the KS-32 firmware does, so the ESP stops at once instead of at the next END;
  the in-flight pipeline is discarded (decision E24). `esphalt 0` lowers the pin; a halted ESP
  resumes at program word 0.
- While the ESP is halted, every `esp` write is followed by one `Esp::idleStep()`, so select
  register transfers (GPR/INSTR) land and the host RAM access of a DADR write completes. Nothing
  else advances while halted, so use `espidle <n>` to give the ESP more idle steps, for example to
  finish a RAM clear (DLENGTH/unit + 1 steps; 256 for DLENGTH 0x00FF00 with MEMSIZ 0xFF). At most
  2^24 steps per command.
- Idle steps also run the DRAM refresh model (DBASE decrements once per step unless host control
  bit 0, Refresh Disable, is set). Set that bit if a program depends on DBASE staying put.
- `frames`: at most 2^25 frames in total per trace. Each frame is one `Otis::runFrame()`, routing
  (below), one `Esp::runSample()` and one output record. While the ESP is halted `runSample()`
  does nothing and the main output keeps the last SER3 value (0 until a program has written it).

## Routing (decision TR1)

| Source | Destination |
|---|---|
| OTIS ch0 (L, R) | aux out |
| OTIS ch1 (L, R) | ESP SER0 in |
| OTIS ch2 (L, R) | ESP SER1 in |
| OTIS ch3 (L, R) | ESP SER2 in |
| ESP SER3 out (L, R) | main out |

The KS-32 voices use CA 1, 2, 3 and the ESP reads SER0-2 and writes only SER3 (see
`docs/decisions.md`, TR1). Voices on CA 3 only reach the ESP when SERMODE.A/D is 0 (page 0x40,
reg 8); with A/D = 1 OTIS port 3 is an input and its serial input is never driven by the runner,
so CH3 stays 0. The firmware writes SERMODE = 0.

## Output

`ens_trace <trace> <out.pcm>` writes 4 little-endian int16 per frame, in this order:
`auxL auxR mainL mainR` (8 bytes per frame; the file size is `frames * 8`). The run starts from
chip reset (ESP running, HALT pin low, all memories zero), so the same trace always gives the
same file.

## Tools

```text
ens_trace <trace> <out.pcm>
pcm_diff <a.pcm> <b.pcm> [--channels N]
```

- `ens_trace`: exit 0 on success, 1 for an unreadable file or a trace error
  (`<trace>:<line>: message`), 2 for bad arguments (message plus usage).
- `pcm_diff`: N interleaved channels, default 4. Prints the frame count of each file, the first
  differing frame, the maximum absolute difference and the RMS difference per channel, and
  `identical` or `DIFFERENT`. Files of different length compare over the common part and count as
  different. Exit 0 when identical, 1 when different or unreadable, 2 for bad arguments.

## Example

`tests/data/smoke.trace` (with the synthetic `smoke_wave.bin`): two voices, one on CA 0 (aux) and
one on CA 1, an ESP program that copies SER0 to SER3, 64 frames.
