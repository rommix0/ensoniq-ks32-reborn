# Decision log

Behaviour the datasheets leave open. Each entry is keyed by id and referenced at the
point of use with `// DECISION <id>: <one line>`.

Entry template:

```
## <Id>: <title>
- Options:
- Choice:
- Evidence:
- Confidence:
- Status:
```

## OTIS

## O9: Unused/reserved bits
- Options: (a) unused bits read 0; (b) unused bits read 1 where the bit table shows "1"; (c) return the last written value.
- Choice: CR reads `0xF000 | cr[11:0]`; SERMODE reads `0x07F8 | bits[15:11,2:0]`; PAGE keeps 7 bits; unlisted registers (voice reg 12, filter-page regs 0 and 7-12, channel-page reg 9 PAR and above) read 0 and ignore writes. Bits outside a register's justification (FC bit 0, STRT-L/END-L [4:0], K1/K2 [3:0], LVOL/RVOL [7:0], ACCH [15:13]) are not stored and read 0.
- Evidence: OTIS notes §4.5 (bit table shows "1" in unused CR/SERMODE positions, "presumably read back as 1"), §4.2-§4.4 justifications.
- Confidence: Medium.
- Status: Implemented in src/otis/otis.cpp (Task 2). PAR (page 64+ reg 9) is not modelled and reads 0.

## O10: Reset state
- Options: (a) everything 0; (b) documented bits only; (c) CR starts with the voice stopped.
- Choice: all voice registers 0 except CR = STOP1|STOP0 (0x0003); ACT = 31 (32 voices); PAGE = 0; IRQV free (bit 7 = 1, IRQ pin released); SERMODE = 0x0001 (A/D = 1); channel accumulators 0.
- Evidence: OTIS notes §4.9 (A/D set on chip reset). Everything else is not documented.
- Confidence: Low (only A/D is documented).
- Status: Implemented in Otis::reset() (Task 2).

## O11: Channel register reads
- Options: (a) CH0-CH2 read back the accumulator; (b) CH0-CH2 read 0; (c) bus error.
- Choice: CH0L/R-CH2L/R are write-only (test mode, SERMODE.TEST=1) and read 0; writes are ignored unless TEST=1, and a test-mode write forms the 21-bit accumulator from SERMODE.MSB[4:0] (guard bits) and the data word. CH3L/R read back the low 16 bits of their last latched output or serial-input value (setSerialIn latches only when A/D = 1).
- Evidence: OTIS notes §4.9 ("can not be read" for CH0-2; CH3 "readable in any mode").
- Confidence: Medium.
- Status: Implemented in Otis::read/write/setSerialIn (Task 2).

## ESP
