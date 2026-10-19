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

## O1: Loop arithmetic
- Options: (a) single-add per slot (`n = acc + FC>>1`) with the remainder kept across the boundary; (b) the literal text, which adds FC twice (steps 3 and 5); (c) compare with `>` instead of `>=`; (d) clamp the remainder to the loop length.
- Choice: `step = FC>>1` (1/512 sample units). Forward: `n = (acc + step)` wrapped to 29 bits; boundary when `n >= END29`. Reverse (DIR=1): `n = acc - step` (signed); boundary when `n <= START29`. At a boundary: LPE=0 sets STOP0 and `acc = boundary`; LPE=1/BLE=0 gives `acc = START29 + (n-END29)` (forward) or `END29 - (START29-n)` (reverse); LPE=1/BLE=1 reflects (`END29 - (n-END29)` and set DIR, or `START29 + (START29-n)` and clear DIR) and toggles DIR. START29/END29 = `((H<<16)|L) & 0x1FFFFFE0`. The accumulator wraps within 29 bits on store. The remainder is not clamped, so a step longer than the loop can land outside it (as one subtraction would in hardware). The IRQ request (O8) is raised at the boundary in every mode when IRQE=1.
- Evidence: OTIS notes §6.1 (steps 3-5 add FC twice; read as single-add; the text does not give >= vs >, the bidirectional remainder, or the one-shot final value), §4.7 CR/LPE/BLE/DIR/STOP0.
- Confidence: Medium.
- Status: Implemented in Otis::updateAddress (Task 3). Tests: otis_forward_step, otis_fraction_step, otis_oneshot_stops_at_end, otis_forward_loop_keeps_remainder, otis_bidi_*, otis_reverse_*, otis_accumulator_wraps_in_29_bits.

## O2: S2 fetch address
- Options: (a) `int + 1` in both directions; (b) `int - 1` when DIR=1; (c) cross into the other bank at the 20-bit wrap.
- Choice: S2 is read at `int + 1` regardless of DIR, from the same bank (BS), wrapping within 20 bits. Both fetches use the accumulator before the slot's address update.
- Evidence: OTIS notes §6.1 step 2 ("Add 1 to Accumulator"); the text does not mention DIR for this step.
- Confidence: Medium.
- Status: Implemented in Otis::stepVoice (Task 3). Tests: otis_s2_address_in_reverse_is_int_plus_1, otis_s2_wraps_in_20_bits, otis_bank_select_follows_bs.

## O3: Interpolation
- Options: (a) `SF = S1 + (((S2 - S1) * frac9) >> 9)` with an arithmetic (floor) shift; (b) truncate toward zero; (c) round to nearest; (d) interpolate from the other sample in reverse (S2 first).
- Choice: (a). frac9 is ACC[8:0] of the accumulator as it stood before this slot's address update; S1 is the word at the integer address and S2 the next word (O2), in both directions. The result always lies between S1 and S2, so it fits 16 bits.
- Evidence: OTIS notes §6.2 (`SF = S1 + ACCfr*(S2-S1)`, rounding unstated).
- Confidence: Medium.
- Status: Implemented as `otis_interp` and Otis::runSignalPath (Task 4). Tests: otis_interp_values, otis_interpolation_in_signal_path.

## O4: Filter arithmetic
- Options: (a) 16-bit state, floor shifts, each pole output saturated to int16; (b) wrap on overflow; (c) wider internal state; (d) round toward zero in the HP feedback term.
- Choice: (a). LP: `y = y1 + ((K12 * (x - y1)) >> 12)`. HP: `y = x - x1 + (((4096 + K12) * y1) >> 13)`, i.e. K = 0.5 + K12/8192. K12 is register bits 15:4; the same K register is read as LP or HP according to the pole's mode (CR LP3/LP4 table; poles 1 and 2 are always LP/K1; pole 3 = LP3 ? LP/K1 : (LP4 ? LP/K2 : HP/K2); pole 4 = LP4 ? LP/K2 : HP/K2). An HP pole's X(n-1) is the previous pole's previous output (O2(n-2) for pole 3, O3(n-2) for pole 4 in the register naming of §4.8). State registers are updated after pole 4 in the order O1, O2 history, O3 history, O4. Consequence of floor rounding: an LP pole under a DC step stops 1 LSB (K = 4095/4096) or a few LSB short of the input, and an HP pole's negative remainder is stranded (`floor(K*y) == y` for `-1/(1-K) < y < 0`) so HP poles settle at a small negative DC offset (-1 at K = 0.5, -3 at K = 0.75, up to thousands as K approaches 1). If hardware truncates toward zero this offset would not exist; option (d) is the likely fix once a reference capture is available.
- Evidence: OTIS notes §6.3 (K scaling x4096 for LP, (K-0.5) x 8192 for HP; saturation and rounding unstated), §4.8 (storage registers).
- Confidence: Medium.
- Status: Implemented as `otis_lp`, `otis_hp` and Otis::runSignalPath (Task 4). Tests: otis_lp_values, otis_hp_values, otis_filter_mode_table, otis_filter_matches_reference_chain_all_modes.

## O5: Volume
- Options: (a) `p = (SF * (16 + mant)) >> 5` then `exp == 0 ? 0 : (int16)p >> (15 - exp)`; (b) a rounding shift; (c) exp = 0 passes the sign bits (the table row for exp 0 is all zeros, so no).
- Choice: (a). LVOL/RVOL bits [15:12] are the exponent and [11:8] the mantissa. exp = 0 mutes; exp = F passes the value unshifted; the right shifts are arithmetic.
- Evidence: OTIS notes §6.4 shift table (exp 0 row is all zeros; exp 1..F rows are sign-extended shifts).
- Confidence: High for the shift; rounding is floor.
- Status: Implemented as `otis_vol` (Task 4). Tests: otis_vol_table, otis_channel_saturation.

## O6: Channel sum and saturation
- Options: (a) 21-bit accumulators saturated to 16 bits on output; (b) modular 16-bit sums; (c) 20-bit output as in MAME PR #14329.
- Choice: (a). Each voice adds its L/R volume results into CH[CA]. At the end of slot ACT the sums are latched: if bits 20..15 are not all equal the output is 0x7FFF or 0x8000 by bit 20, otherwise bits 15..0. That is a clamp of the signed sum to int16. The sums are then cleared. 32 voices cannot overflow 21 bits (32 x 32767 < 2^20). With SERMODE.TEST = 1 accumulation is off and the frame carries the host-written CH registers (21 bits with the SERMODE.MSB guard bits, so the overflow detector is testable). With A/D = 1, CH3 is a serial input: its registers keep the input value and are not overwritten by the sum. CH reads of a non-test latch return the saturated value.
- Evidence: OTIS notes §4.9 (SERMODE MSB, TEST, A/D), §6.4, §6.5.
- Confidence: High.
- Status: Implemented in Otis::runSignalPath and Otis::latchFrame (Task 4). Tests: otis_channel_saturation, otis_channel_sums_are_cleared_each_frame, otis_serial_test_mode_latches_channel_regs, otis_ch3_reads_back_latched_output.

## O7: Stopped voice
- Options: (a) a stopped voice is muted and does not fetch; (b) STOP0/STOP1 freeze only the address ALU and the voice keeps fetching, filtering and mixing.
- Choice: (b). STOP0 and STOP1 freeze only the address ALU (accumulator and DIR); the voice still performs both wave fetches every slot. The rest of the signal path follows the same rule: a stopped voice still interpolates, filters, updates O1(n-1)..O4(n-1), applies volume and accumulates into its channel. STOP1 also blocks the loop-end IRQ because that is raised by the address ALU.
- Evidence: CR STOP1 text ("stop the address ALU from modifying any bits"); MAME audit OTIS-2 (Taito hack); the KS-32 firmware reads O1(n-1) of a stopped voice to find zero crossings (docs/rom-investigation/findings.md, "Other findings").
- Confidence: High.
- Status: Implemented in Otis::stepVoice (Task 3, address/fetch) and Otis::runSignalPath (Task 4, signal path). Tests: otis_stop1_freezes_address, otis_oneshot_stops_at_end, otis_stopped_voice_still_outputs, otis_stopped_voice_o1_tracks_acc.

## O8: IRQ and IRQV
- Options: (a) the model in OTIS notes §7 steps 1-5; (b) clear CR.IRQ on the IRQV read itself; (c) latch regardless of IRQE.
- Choice: (a). A voice that hits its loop end with IRQE=1 sets CR.IRQ. In its slot, a voice with CR.IRQ=1 and IRQE=1 latches its number into IRQV and pulls IRQ low only if IRQV is free; otherwise it waits (stacks) in CR.IRQ and retries on later frames. The IRQV read returns `0x60 | (pending ? 0 : 0x80) | vec` (bits 6:5 read 1), frees the vector, releases IRQ and marks that voice so its CR.IRQ is cleared at the start of its next slot (before that slot's address update, so a new boundary hit in the same slot can set it again). A read with nothing pending marks nothing. A host write of IRQE|IRQ forces an interrupt at that voice's next slot. Latching happens after the address update in the same slot, so a boundary hit raises IRQ in that very frame.
- Evidence: OTIS notes §7, §4.6 (IRQV description).
- Confidence: Low (bits 6:5 unknown; the exact reload cycle is not documented).
- Status: Implemented in Otis::read(14), stepVoice, serviceIrq (Task 3). Tests: otis_irq_sequence, otis_irq_two_voices_stack, otis_irq_not_replaced_while_pending, otis_irq_requires_irqe, otis_irq_raised_in_loop_mode, otis_forced_irq.

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

## O12: Output width
- Options: (a) 16-bit signed per channel (the serial word); (b) 20-bit as in MAME PR #14329.
- Choice: (a). `OtisFrame::ch` is `int16_t[4][2]`, the value after the O6 saturation.
- Evidence: OTIS notes §5.4 (two 16-bit words per serial frame), §4.9 (channel registers are 16 bits plus guard bits).
- Confidence: High.
- Status: Implemented in Otis::latchFrame (Task 4).

## O13: ACT below 7
- Options: (a) allow it, with frames of ACT+1 voices and no serial-timing model; (b) clamp ACT to at least 7; (c) refuse to produce frames.
- Choice: (a). The datasheet says fewer than 8 voices starves the 32-bit serial transfer (§6.5); the emulator has no serial-timing model, so the frame is simply latched after slot ACT. ACT is 5 bits so the range is 0..31; ACT = 0 gives one voice per frame.
- Evidence: OTIS notes §6.5 timing check (8 voices x 1.6 us = 32 bits at 2.5 MHz). Review focus 4.
- Confidence: Medium (the behaviour of real hardware below 8 voices is undocumented).
- Status: Implemented in Otis::stepVoice (Task 4). Test: otis_act_below_8_still_produces_frames.

## ESP
