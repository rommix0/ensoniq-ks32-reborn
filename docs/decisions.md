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
- Choice: `step = FC>>1` (1/512 sample units). Forward: `n = acc + step` compared UNWRAPPED (wide type), boundary when `n >= END29`; the 29-bit mask is applied only when the result is stored. Reverse (DIR=1): `n = acc - step` (signed); boundary when `n <= START29`. At a boundary: LPE=0 sets STOP0 and `acc = boundary`; LPE=1/BLE=0 gives `acc = START29 + (n-END29)` (forward) or `END29 - (START29-n)` (reverse); LPE=1/BLE=1 reflects (`END29 - (n-END29)` and set DIR, or `START29 + (START29-n)` and clear DIR) and toggles DIR. START29/END29 = `((H<<16)|L) & 0x1FFFFFE0`. The remainder is not clamped, so a step longer than the loop can land outside it (as one subtraction would in hardware). The IRQ request (O8) is raised at the boundary in every mode when IRQE=1. Rejected: wrapping to 29 bits first and then comparing, which makes a fast one-shot near the top of the bank skip END, wrap to ~0 and play the whole bank as garbage.
- Evidence: OTIS notes §6.1 (steps 3-5 add FC twice; read as single-add; the text does not give >= vs >, the bidirectional remainder, or the one-shot final value), §4.7 CR/LPE/BLE/DIR/STOP0. KS-32 sample map (`docs/rom-investigation/data/sample_map.csv`): sample end addresses lie at word 0xFFFF4..0xFFFFD (internal waves 120 and 57 one-shot, rom3 wave 0 zone 1 bidirectional, top_key 127), within a few samples of 2^20, so a step past END can carry past bit 28; the boundary must be seen before the wrap.
- Confidence: Medium.
- Status: Implemented in Otis::updateAddress (Task 3; boundary-before-wrap fix in the final review). Tests: otis_forward_step, otis_fraction_step, otis_oneshot_stops_at_end, otis_forward_loop_keeps_remainder, otis_bidi_*, otis_reverse_*, otis_forward_boundary_compares_before_29_bit_wrap, otis_oneshot_at_top_of_bank_stops, otis_bidirectional_at_top_of_bank_reflects.

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
- Choice: CR reads `0xF000 | cr[11:0]`; SERMODE reads `0x07F8 | bits[15:11,2:0]`; PAGE keeps 7 bits; unlisted registers (voice reg 12, filter-page regs 0 and 7-12, channel-page regs 10 and above) read 0 and ignore writes. PAR (channel-page reg 9) reads the POT conversion given by `setPot(v10)` as `v10 << 6` ([15:6], bits 5:0 read 0) and ignores writes; it starts at 0 and `reset()` leaves it. Bits outside a register's justification (FC bit 0, STRT-L/END-L [4:0], K1/K2 [3:0], LVOL/RVOL [7:0], ACCH [15:13]) are not stored and read 0.
- Evidence: OTIS notes §4.5 (bit table shows "1" in unused CR/SERMODE positions, "presumably read back as 1"), §4.2-§4.4 justifications.
- Confidence: Medium.
- Status: Implemented in src/otis/otis.cpp (Task 2). PAR added in Plan 6 Task 6 (`Otis::setPot`, OTIS notes §4.9 "PAR"); the KS-32 drives it from its analog multiplexer (K8). Test: otis_par_reads_pot_value.

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

## E1: Carry polarity
- Options: (a) C = unsigned carry out of bit 23 for ADD/ADDU, unsigned borrow (1 when A < B) for SUB/SUBU/CMP/DIFF; (b) C = carry for every op, i.e. "no borrow" after a subtract (the literal §3.3.2 definition "carry (or no-borrow)").
- Choice: (a).
- Evidence: ESP notes §3.3.2 defines C as "carry (or no-borrow)", but the §4.2.2 tables use C alone for "Lower than (unsigned)" and NOT(C) for "Higher or same", which requires C = borrow after A-B. (a) makes the LO/HS masks mean what their names say.
- Confidence: Medium.
- Status: Implemented in ens::esp::alu (Task 5). Tests: alu_sub_borrow, alu_addu_wraps.

## E2: DIFF
- Options: (a) unsaturated, `A = (0x7FFFFF - B) & 0xFFFFFF`, V = signed overflow; (b) saturated, as the function table says "with Overflow Protection".
- Choice: (a). Flags are those of SUBU(0x7FFFFF, B).
- Evidence: §3.3.1 DIFF paragraph says "The subtraction is unsaturated"; the §3.3.3 flag table groups DIFF with CMP (plain V).
- Confidence: Medium.
- Status: Implemented in ens::esp::alu (Task 5). Test: alu_diff_unsaturated.

## E3: ABS
- Options: (a) negative B gives `~B` (one's complement), N forced 0, C = sign of B, V/LT/Z unchanged; (b) true two's-complement negation.
- Choice: (a).
- Evidence: §3.3.1 "Negative arguments are negated with One's complement results"; §3.3.3 flag table row ABS (0, (2), -, -, -).
- Confidence: Medium-high.
- Status: Implemented in ens::esp::alu (Task 5). Test: alu_abs_ones_complement.

## E13: ASL2/ASL8
- Options: (a) saturate when the sign changes (result out of 24-bit range) to 0x7FFFFF/0x800000 by the old sign, V = saturation occurred, N/Z from the result, C and LT unchanged; (b) V as a plain overflow flag with wrapping.
- Choice: (a).
- Evidence: §3.3.1 ("Saturation will occur if the sign bit changes"), flag table row ASL2,ASL8 (X, -, (1), -, X) where (1) means V indicates saturation.
- Confidence: Medium-high.
- Status: Implemented in ens::esp::alu (Task 5). Tests: alu_asl2, alu_asl8.

## E14: LS15
- Options: (a) `A = (B << 15) & 0x7FFFFF`, N = 0, C = bit 8 of B (the bit that would land in the sign position), Z/V/LT unchanged; (b) recompute Z.
- Choice: (a).
- Evidence: §3.3.1 LS15 paragraph; flag table row LS15 (0, (3), -, -, -) with note (3).
- Confidence: Medium.
- Status: Implemented in ens::esp::alu (Task 5). Test: alu_ls15.

## E15: ASR and logic-op flags
- Options: (a) ASR: `A = B >> 1` arithmetic, N from result, C = bit 0 of B, Z/V/LT unchanged; AND/OR/XOR: N and Z from result, C/V/LT unchanged; (b) also update Z for ASR.
- Choice: (a).
- Evidence: §3.3.3 flag table rows AND,OR,XOR (X,-,-,-,X) and ASR (X,(4),-,-,-).
- Confidence: Medium (the Z column cells of ABS/LS15/ASR are the least legible in the scan).
- Status: Implemented in ens::esp::alu (Task 5). Tests: alu_asr, alu_logic_keeps_cvlt.

## E16: ADD/SUB and unsaturated flags
- Options: (a) ADD/SUB: N and Z from the stored (saturated) result, V = saturation occurred, LT = sign of the infinite-precision result (= N_wrapped xor V_overflow); ADDU/SUBU/CMP/DIFF: N and Z from the wrapped result, V = signed overflow, LT = N xor V; (b) LT = N xor V with N from the saturated result.
- Choice: (a).
- Evidence: §3.3.2 (LT = N xor V), §3.3.3 flag table footnote (1) "V indicates whether saturation as opposed to overflow occurred".
- Confidence: Medium.
- Status: Implemented in ens::esp::alu (Task 5). Tests: alu_add_saturates, alu_addu_wraps, alu_sub_borrow.

## E21: Skip formula vs. TRUE/FALSE labels
- Options: (a) the literal equation `SKIP = NOTm xor ((N&Nm)|(Z&Zm)|(V&Vm)|(C&Cm)|(LT&LTm))`; (b) reinterpret the masks so that "TRUE" (0x04) executes always and "FALSE" (0x00) never.
- Choice: (a). Under it 0x04 always skips and 0x00 never skips; the other 14 masks behave as "execute when the condition holds" (e.g. EQ 0x0C skips when Z=0).
- Evidence: ESP notes §4.2.1 equation vs the §4.2.2 mask table labels (part 2, esp_29). The equation is explicit; the labels conflict with it.
- Confidence: Medium.
- Status: Implemented in ens::esp::skipCondition (Task 5). Test: alu_skip_masks. CCR/CMR layout N 23, C 22, V 21, LT 20, Z 19, NOT 18 (CMR); the datasheet's 8-bit masks are `cmr >> 16`.

## E6: MAC model
- Options: (a) `acc` is a 50-bit signed value (48 + 2 guard) that wraps at 50 bits; `product = sext24(X) * sext24(Y) << (SIGREG bit 22 ? 1 : 2)`; readout `sat48(acc)` clamps to [-2^47, 2^47-1], MACH = sat48 >> 24, MACL = sat48 & 0xFFFFFF; ALU write MACH sets `acc = (sext24(v) << 24) | (acc & 0xFFFFFF)`, write MACL sets `acc = (acc & ~0xFFFFFF) | v`; a plain multiply discards the preload; (b) saturate the accumulator on every accumulate (MAME-style, loses the guard bits).
- Choice: (a). The wrap is `((v << 14) >> 14)`, so an accumulator pushed past 2^49 wraps negative (MAME bug #2 not reproduced). The "MACL as destination duplicates the upper half" sentence is read as concerning the multiplier C-destination and is not implemented.
- Evidence: ESP notes part 1 §3.4 (2 guard bits, saturate on Z-bus/SPR readout, SIGREG shift, skipped step does not accumulate); part 2 §4.3.3 (preload at step N-1, plain multiply discards the preload, MACL is the only path to the low 24 bits).
- Confidence: Low-medium.
- Status: Implemented in ens::esp::Mac (Task 6). Tests: mac_fractional_half_times_half, mac_shift_two, mac_neg_full_scale_squared_saturates, mac_guard_bits_recover, mac_wraps_at_50_bits, mac_preload, mac_plain_multiply_discards_preload.

## E7: AGEN units
- Options: (a) the 20-bit AGEN registers (DLENGTH, ABASE, BBASE, DBASE) hold 24-bit-aligned values, writes keep bits 23..4 and reads return the low 4 bits as 0, MEMSIZ keeps its written 24-bit value; `unit = MEMSIZ + 1`, `shift = trailing 1 bits of MEMSIZ`; delay `s = (DBASE + off) & 0xFFFFFF`, if `s > DLENGTH` then `s -= DLENGTH + unit`, `phys = (s & ~MEMSIZ & 0xFFFFFF) >> shift`; table `phys = ((BASE + off) & 0xFFFFFF & ~MEMSIZ) >> shift`; I/O `phys = (off & 0xFFFFFF) >> 4`; (b) subtract DLENGTH only (the text), which breaks the circular period.
- Choice: (a), with two refinements to the literal formula. (1) The delay comparison and wrap are done on address bits only: `s = s & ~MEMSIZ`, `len = DLENGTH & ~MEMSIZ`, wrap when `s > len` by `len + unit`. Reason: the datasheet says MEMSIZ is "a mask for delay address range comparisons", and the literal compare wraps a read at word `len` plus a fraction (e.g. DLENGTH = 0xFFFFF0 with a read at 0xFFFFF8 would wrap to word 0xEFFF). With the masked compare, DLENGTH = 0xFFFFFF (the common firmware value, used as 0xFFFFF0) gives the full 64K ring and never leaves the 16-bit word range, and DLENGTH = 0x1F7DFF (used as 0x1F7DF0) keeps word 0x1F7D valid. The two forms agree whenever DLENGTH and the sum have the same fraction bits (all the datasheet-style examples). (2) The DBASE reload takes `DLENGTH & ~MEMSIZ` (not raw DLENGTH), keeping DBASE's fraction bits 0 as E20 implies; identical for aligned DLENGTH. DBASE decrements by `unit` and reloads when `DBASE < unit` (i.e. it would go below 0); the period is `DLENGTH/unit + 1` samples.
- Evidence: ESP notes part 1 §3.5.2-3.5.4 (MEMSIZ examples $0000FF for 64K, "mask for delay address range comparisons", DBASE "decrements from DLENGTH to 0", table base values not restricted by DLENGTH, I/O "always addressed absolutely"); inferred alignment note under §3.5.2. The firmware writes MEMSIZ = 0x0000FF and DLENGTH = 0xFFFFFF (one program 0x1F7DFF).
- Ruling R13 (Task 9 fix round): read-back returns the written 24-bit value; arithmetic ignores bits 3..0. DLENGTH, ABASE, BBASE and DBASE therefore store the full 24-bit value as written (the option (a) text above "writes keep bits 23..4 and reads return the low 4 bits as 0" is superseded), and every use in address arithmetic (Agen::physical, endOfSample, DBASE reload, RAM clear, host RAM access) goes through the bits-23..4 view (`dlength20()` etc.), so address behaviour is unchanged (DLENGTH = 0xFFFFFF still behaves as 0xFFFFF0 and gives the 64K ring). The decrement result and a DBASE reload have bits 3..0 clear. Reason: the KS-32 boot writes DLENGTH = 0xFFFFFF and verifies it by read-back with retries (esp_init_sequence.json); a read-back of 0xFFFFF0 would fail that compare.
- Confidence: Medium (refinement (1) and (2): Low-medium, pure interpretation of an unstated edge).
- Status: Implemented in ens::esp::Agen (Task 7; R13 in Task 9 fix round). The wrap became a full modulo by `len + unit` in E31 (2026-10-21); it gives the single-subtraction result above whenever the sum is below twice the ring length. Tests: agen_memsiz_64k, agen_delay_no_wrap, agen_delay_wraps_circular, agen_delay_top_of_buffer_with_fraction_does_not_wrap, agen_delay_dlength_all_ones_stays_in_range, agen_dbase_decrements_and_reloads, agen_period_matches_wrap, agen_period_odd_dlength, agen_table_not_masked_by_dlength, agen_io_absolute, agen_reg_alignment.

## E20: MEMSIZ write side effect
- Options: (a) writing MEMSIZ also sets `DBASE &= ~MEMSIZ`; (b) no side effect.
- Choice: (a).
- Evidence: ESP notes part 1 §3.2 register-map text "Writing the MEMSIZ register affects the DBASE register directly"; the datasheet gives no detail of the effect, masking is the natural reading since DBASE's bits below the address LSB are unused.
- Confidence: Low.
- Status: Implemented in Agen::writeReg(244) (Task 7). Test: agen_reg_alignment.

## E18 (FIFO part): DOL FIFO behaviour
- Options: (a) 2-entry queue; push when full drops the oldest; RAM write pops the oldest and on an empty FIFO re-outputs the last pushed word; dump pops and discards (nothing when empty); reset on halt-at-END empties the FIFO but keeps the last-pushed register; (b) reset also clears the last-pushed word.
- Choice: (a). The FIFO stores full 24-bit words; the 16-bit left-justified truncation belongs to the RAM write (Task 8). The RAM pipeline timing half of E18 is completed in Task 8.
- Evidence: ESP notes part 1 §3.6.3 ("Two DOL writes will fill both words. Two RAM writes will empty both words. Writing an empty FIFO to RAM will cause the last word written to the DOL to be written to RAM. Overstuffing the FIFO will cause the oldest data to be bumped out"; halt at END "will reset the FIFO pointers ... a subsequent RAM write may not output the last word written"); dump on empty does nothing (p27).
- Confidence: Medium.
- Status: DolFifo implemented (Task 7). Test: fifo_semantics, fifo_two_writes_two_reads_in_order. Pipeline timing: see "E18 (pipeline part)" (Task 8).

## E18 (pipeline part): RAM pipeline timing
- Options: (a) at step k the RAM cycle of word k-1 runs (its address was computed at step k-1 from `GPR[k-1]`); a read latches `DIL = data << 8` after step k's operand fetches, so it is visible from step k+1, two words after the issuing word; (b) a one-step pipeline (read visible to the next word); (c) the RAM cycle in the same step as the address computation.
- Choice: (a). A write pops the DOL FIFO (empty: re-uses the last pushed word) and drives bits 23..8 of the 24-bit FIFO word onto the bus. Read-delay/dump (RAM op 5) performs a delay read into DIL and dumps one FIFO word. An ALU or MUL FIFO destination in word k is pushed in step k+1 (phase 3 or 1) before word k's own write cycle (step k+1, phase 7), so a write assembled in the same word as its data stores that data ("write addresses are pipelined back zero cycles"). The pending address and RAM op carry across END into the next sample; the END word's RAM address uses DBASE from before the END decrement.
- Evidence: ESP notes part 2 §4.3.4 (pipeline length 2; read addresses pipelined back 2 words, writes 0; control bits pass a 1-cycle delay register), §4.3.1 timing figure ("RAM R/W Cycle Offset @ N-1"); part 1 §3.6.3 (DIL left-justified, persists until the next read). With DLENGTH = 0x00FF00 and MEMSIZ = 0xFF, a write at offset 0 and a read at offset 0x000400 give exactly 4 samples of lag.
- Confidence: Medium.
- Status: Implemented in Esp::step and Esp::ramCycle (Task 8). Tests: esp_delay_line_roundtrip (lag pinned at 4), esp_ram_read_visible_two_words_later, esp_mul_to_fifo_write, esp_end_step_executes_mul_and_ram_fields.

## E17: Execution order within a step
- Options: (a) nine phases: (1) commit MUL(k-1) (C register and/or DOL push), (2) MUL(k) operand fetch and product, (3) commit ALU(k-1) (register, DOL push, CCR, MACH/MACL preload), (4) skip evaluation for word k, (5) ALU(k) operand fetch and compute (pending), (6) MUL(k) accumulate, (7) RAM cycle of word k-1, (8) AGEN address for word k from `GPR[pc]`, (9) END handling; (b) execute each word atomically (MAME-like).
- Choice: (a). Consequences: an ALU result reaches the ALU on the next step but the multiplier only two steps later; a multiplier result reaches the ALU on the next step; an ALU write to MACH/MACL at step k-1 is the preload for the accumulate at step k (a preload in the same word as the accumulate is too late); because the preload lands in phase 3 and ALU(k) fetches in phase 5, before MUL(k) accumulates in phase 6, an ALU read of MACH/MACL in word k returns the preloaded value (word k's own multiply is seen only from word k+1); the previous multiply's result is a valid multiplier source on the next step (phase 1 before phase 2); the CCR written by ALU(k-1) is what word k's skip test sees (phase 3 before phase 4). The pipeline registers persist across `runSample()` calls. Within a commit the CCR is updated before the destination register is written, so an ALU instruction whose destination is CCR (250) leaves the written value, not its own flags. The MUL Z-bus result is MACH after the accumulate; MACH/MACL reads are saturated (E6).
- Evidence: ESP notes part 2 §4.3.1 timing figure (Write Mul N-2, Read Mul Op N-1, Write ALU N-2, Read ALU Op N-1), §4.3.2 ("ALU operations lag Multiplier operations by one T cycle"; leave one step between an ALU result and its use in the MUL; MUL results usable by the ALU on the very next step), §4.3.3 (preload on the step prior to the accumulation). The CCR-destination ordering is not documented.
- Confidence: High for the ordering; medium for the MAC preload slot; low for the CCR-destination detail.
- Status: Implemented in Esp::step, commitMul and commitAlu (Task 8). Tests: esp_mov_and_end, esp_alu_result_not_visible_to_next_mul, esp_alu_result_visible_two_steps_later_to_mul, esp_mul_result_visible_to_next_alu, esp_mul_result_visible_to_next_mul, esp_ccr_from_previous_alu_drives_skip, esp_alu_reads_mac_before_same_word_accumulates, esp_alu_reads_preloaded_mach, esp_mac_preload_slot, esp_mac_saturates_in_core.

## E4: CMP and skip
- Options: (a) CMP ignores the Skip bit (always evaluates and sets the CCR); every other instruction with Skip=1 never sets the CCR, whether or not it is skipped; (b) skippable instructions set the CCR when they execute; (c) a skippable CMP is skipped like any other instruction.
- Choice: (a). The skip condition is not evaluated for CMP. For other opcodes the CCR update depends on the word's Skip bit, not on the skip outcome. The Task 8 brief's "CCR is only written ... if `!skipped || op==CMP`" is read with "skipped" meaning "skippable", as this entry and the datasheet require.
- Evidence: ESP notes part 1 §3.3.1 (CMP is not affected by the skip bit), §4.2.1 ("Instructions which are skippable cannot set the condition codes, regardless of whether they were executed or not"). Agrees with MAME after PR #14979.
- Confidence: Medium.
- Status: Implemented in Esp::step (phase 4) and Esp::commitAlu (Task 8). Tests: esp_cmp_ignores_skip, esp_skip_suppresses_writes_not_ram (control half).

## E5: What a skip suppresses
- Options: (a) suppress the ALU and MUL register writes, DOL pushes and the accumulator update, but still perform the word's RAM-control action; (b) also suppress the RAM cycle.
- Choice: (a). A skipped word's operand fetches and its RAM cycle (read into DIL, FIFO pop and write, dump) still happen; the MAC keeps its previous contents.
- Evidence: ESP notes part 1 §4.2.1 ("executing each phase of the instruction except writing of its final results to the specified target register, including the Multiplier's accumulator. This method ... maintains consistency both in the ESP's internal pipelines and in the overall timing").
- Confidence: Medium.
- Status: Implemented in Esp::step, commitMul and commitAlu (Task 8). Test: esp_skip_suppresses_writes_not_ram.

## E8: SER registers
- Options: (a) 16-bit registers left-justified at bits 23..8 (read `v << 8`, write keeps bits 23..8); (b) right-justified; (c) 24-bit storage.
- Choice: (a). `setSerialIn(port, l, r)` latches SERnL (235 + 2n) and SERnR (234 + 2n); `serialOut` reads the same latches. Until Task 9's serial control the ports are plain latches shared by both directions.
- Evidence: ESP notes part 1 §3.2 (SER registers are 16 bits wide), §3.6.2 (16-bit data maps to GPR bits 23..8); same convention as DIL.
- Confidence: Medium.
- Status: Implemented in Esp::readReg, writeReg, setSerialIn and serialOut (Task 8). Test: esp_ser_regs_left_justified.

## E9: Addresses 192..233
- Options: (a) read 0, writes ignored; (b) extra GPRs; (c) aliases of other registers.
- Choice: (a), for program, multiplier and direct (host) access alike.
- Evidence: ESP notes part 1 §3.2 and §4.1.1 (0..191 are GPRs, 234..255 are listed; 192..233 are not described).
- Confidence: Low.
- Status: Implemented in Esp::readReg and writeReg (Task 8). Test: esp_constants_and_holes.

## E11: Program without END
- Options: (a) after executing word 159, behave as if it were END; (b) wrap the PC to 0 without the END actions; (c) run until an END is found (can hang).
- Choice: (a). `runSample()` therefore runs at most 160 steps and always returns with PC = 0.
- Evidence: Undocumented (the instruction memory has 160 words).
- Confidence: Low.
- Status: Implemented in Esp::step and runSample (Task 8). Test: esp_runsample_terminates_without_end (also runs 160 pseudo-random non-END words).

## E12: Constants 252..255
- Options: (a) read -1, 0x800000, 0x7FFFFF, 0, with writes discarded; (b) writable.
- Choice: (a).
- Evidence: ESP notes part 1 §3.2 (constants in ROM, "can be written to when dummy destinations are needed").
- Confidence: High.
- Status: Implemented in Esp::readReg and writeReg (Task 8). Test: esp_constants_and_holes.

## E19: END step
- Options: (a) END executes as an ALU NOP (no register write, no DOL push, no CCR change) while its MUL and RAM fields execute normally; after phase 8 of the END step DBASE -= unit (reloading DLENGTH below 0), then at most one pending host transfer, then HALT is sampled; the next step is PC 0; (b) END also suppresses the MUL and RAM fields.
- Choice: (a). The END word's MUL result and RAM cycle complete in the first step of the next sample (the pipeline carries across END). The host-transfer and HALT hook was filled in by Task 9 (see "E19 (host part)").
- Evidence: ESP notes part 2 §5.2.1 (END actions: PC reset, HALT sampling, serial clocks, DBASE decrement, host access). KS-32 firmware END words carry live MAC and RAM fields, e.g. a MAC with a delay write (docs/rom-investigation/findings.md).
- Confidence: Medium.
- Status: Implemented in Esp::step and commitAlu (Task 8). Tests: esp_mov_and_end, esp_end_step_executes_mul_and_ram_fields.

## E22: Narrow SPR read-back
- Options: (a) keep only the documented bits (SIGREG bit 22, CCR bits 23..19, CMR bits 23..18), so other bits read 0; (b) store all 24 bits.
- Choice: (a). The firmware writes SIGREG with all bits set; only bit 22 has an effect either way.
- Evidence: ESP notes part 1 §3.2 widths (SIGREG 1 bit = bit 22, CCR 5, CMR 6) and the DIL text ("unused 8 bits are read back as zeros"). Read-back of unused bits is not documented.
- Confidence: Low.
- Status: Implemented in Esp::writeReg (Task 8). Test: esp_narrow_spr_widths.

## E23: ESP reset state
- Options: (a) reset clears GPRs, instruction memory, SPRs, the MAC, the AGEN registers, DIL, the DOL FIFO (including its last-pushed word) and the pipeline registers, with PC = 0; (b) leave GPR and instruction memory as they were (power-up contents are undefined in hardware).
- Choice: (a), for deterministic emulation. The host loads the program and GPRs after reset anyway.
- Evidence: Undocumented.
- Confidence: Low.
- Status: Implemented in Esp::reset (Task 8); Task 9 extends it to the host-interface state (host data/address/control/select registers return to 0, the ESP is running, the external HALT pin level is kept). Test: esp_reset_clears_pipeline.

## E10: Host DOL low byte
- Options: (a) host register $0E reads 0x00 (the text: "the low bytes will read back as zero"); (b) reads 0xFF (the memory-map table).
- Choice: (a). Writes to $0E are ignored. DIL low byte $0B also reads 0.
- Evidence: ESP notes part 2 §5.1.1 table versus §5.1.3 text (DIL and DOL are 16 bits wide, left-justified).
- Confidence: Low.
- Status: Implemented in Esp::hostRead (Task 9). Test: host_register_map_details.

## E18 (halt part): END with HALT high
- Options: (a) an END taken while HALT is high resets the DOL FIFO pointers (empties it; the last-pushed word is kept, see the FIFO part); (b) leave the FIFO alone.
- Choice: (a). The reset happens after the END-step DBASE decrement and the host transfer. The remaining in-flight pipeline (the END word's MUL result and RAM cycle) is kept and completes in the first step after the ESP resumes. The halt-enable write (E24) also resets the FIFO.
- Evidence: ESP notes part 2 §5.1.3 ("During HALT, the FIFO pointers are reset"); part 1 §3.6.3.
- Confidence: Medium.
- Status: Implemented in Esp::step (Task 9). Test: halt_at_end.

## E19 (host part): host transfer and HALT sampling at END
- Options: see E19. The hook is now filled: after the END-step DBASE decrement, at most one pending host GPR/INSTR request is performed, then the HALT level is sampled; if high, the ESP halts with pc = 0 (so the next resume starts at word 0) and `runSample()` returns 0 until HALT goes low. A request issued by the host between samples therefore lands at the next END.
- Choice: as stated. DBASE still decrements on an END that halts (§5.4.1).
- Evidence: ESP notes part 2 §5.1.2, §5.2.1, §5.4.1.
- Confidence: Medium.
- Status: Implemented in Esp::step and Esp::hostTransfer (Task 9). Tests: host_write_gpr_running, host_read_instr, host_write_instr_and_gpr, host_one_transfer_per_sample, halt_at_end.

## E24: Halt-enable write mid-program
- Options: (a) with HALT high, a write to $1F stops the ESP at once, wherever it is (also inside `runSample()`, e.g. from a bus callback); the in-flight pipeline (pending ALU/MUL results and RAM cycle) is discarded, the DOL FIFO is reset and pc = 0, so the next resume starts at word 0; (b) keep the pipeline; (c) the write only arms a halt at the next END.
- Choice: (a). With HALT low the write does nothing. The datasheet calls the machine state unknown after this; discarding stale results avoids committing a half-finished word's results into the next program.
- Evidence: ESP notes part 2 §5.1.6 ("forces the ESP to halt immediately, without waiting for an END statement"; "the state of the machine could be unknown"); the firmware writes $1F = 0x02 then 0xFF with ESPHALT asserted (`docs/rom-investigation/data/esp_init_sequence.json`).
- Confidence: Medium for halting at once; low for discarding the pipeline.
- Status: Implemented in Esp::haltNow and the step() early return (Task 9). Test: halt_enable_immediate.

## E25: Host control register, RAM clear and refresh
- Options: (a) bit 0 Refresh Disable is stored; bit 1 RAM Clear takes effect only when written 1 while halted and with Refresh Disable = 0 (DBASE is loaded with DLENGTH, masked like DBASE reload, at that moment), advances one write per idle step and clears itself once address 0 (DBASE = 0) has been written; writing bit 1 = 0 aborts, writing it 1 with Refresh Disable = 1 or while running is ignored; releasing HALT abandons a clear in progress; bit 2 is read only; (b) a clear started with Refresh Disable = 1 runs anyway; (c) a clear continues across a resume.
- Choice: (a). The firmware writes 0x03 and then 0x02 before every clear and waits; under (a) the 0x03 is the "reset the RAM Clear bit" step and 0x02 starts the clear, so the behaviour matches the power-up sequence. Writes: zero to `(DBASE & ~MEMSIZ) >> shift`, then DBASE -= MEMSIZ + 1, so DLENGTH = 0x00FF00 with MEMSIZ = 0xFF clears addresses 0xFF..0 (256 writes, DBASE = 0 at the end). Refresh (Refresh Disable = 0, no clear) decrements DBASE once per idle step with the END-step rule (decrement, reload DLENGTH below 0) and performs no bus access; the external DRAM refresh itself is not modelled. Refresh Disable = 1 freezes DBASE while halted.
- Evidence: ESP notes part 2 §5.1.4, §5.4.1 (RAM Clearing, Enabling RAM Refresh), §5.4.2 steps 4-6; `docs/rom-investigation/data/esp_init_sequence.json` (host control 0x03 then 0x02).
- Confidence: Medium.
- Status: Implemented in Esp::hostWrite, setHaltPin and idleStep (Task 9). Tests: ram_clear, ram_clear_needs_halt_and_refresh_enabled, refresh_disable_freezes_dbase, host_firmware_boot_sequence.

## E26: Host GPR/INSTR registers and select protocol
- Options: (a) one request latch: a write to $80/$A0/$C0/$E0 replaces any pending request (data registers keep their contents), sets Host Access OK/ busy ($12 bit 2 = 1) and is performed at the next END (running) or the next idle step (halted); read requests capture GPR/SPR via the program-side register map (SPR semantics, holes read 0) and INSTR (0 above word 159) into $00-$08; each select register reads back its last value; INSTR byte $08 reads `& 0xF8` while writes store the byte as given; writes beyond word 159 or into holes are ignored; select addresses decode exactly ($80, $A0, $C0, $E0), other unlisted host addresses read 0 and ignore writes; (b) a queue of requests; (c) select addresses decode only bits 7:5.
- Choice: (a). Matches the single address latch in the hardware description and the "one transfer per sample" rule. The two-request behaviour is documented by host_one_transfer_per_sample (the second request wins, the first address is never written).
- Evidence: ESP notes part 2 §5.1.1, §5.1.2 ("only one transfer is allowed per sample period"), §5.4.1.
- Confidence: Medium.
- Status: Implemented in Esp::hostRead, hostWrite and hostTransfer (Task 9). Tests: host_write_gpr_running, host_read_instr, host_write_instr_and_gpr, host_one_transfer_per_sample, host_transfer_reaches_spr_and_ignores_holes, host_register_map_details.

## E27: Host RAM access
- Options: (a) a write to DADR HI ($0F) while halted performs one RAM or I/O cycle immediately; RAM address = `(DADR & ~MEMSIZ) >> shift` (the table rule), I/O address = DADR >> 4 (20-bit); the access mode comes from $14 (bit 7 read, bit 6 RAM, other bits stored); a write sends the host DOL word, a read latches DIL = data << 8; DADR LO keeps bits 7:4; the same write while running is ignored; (b) model the 6-clock latency; (c) allow the access while running.
- Choice: (a). The latency is not observable in an instruction-period model. DADR is left-justified: with MEMSIZ = 0xFF, DADR 0x001230 addresses word 0x12; the firmware's table writes (DADR $0F/$10 = word address) then land on that word.
- Evidence: ESP notes part 2 §5.1.3 and §5.4.1 ("Access to RAM is available ... only when the ESP is Halted"), the firmware table-load sequence (esp_init_sequence.json, "dram_tables").
- Confidence: Medium.
- Status: Implemented in Esp::hostRamAccess (Task 9). Tests: host_ram_write_read_halted, host_ram_access_io_space_and_running, host_firmware_program_load_with_dram_table.

## E28: What one idle step does
- Options: (a) a halted instruction period services at most one pending GPR/INSTR request and, independently, performs either one RAM-clear write or one DBASE refresh decrement; (b) exactly one of the three per step (a pending request would stall the clear/refresh for a period); (c) transfers are immediate.
- Choice: (a). The GPR/INSTR memories are internal DRAM refreshed by the internal counter; the clear and refresh use the external-RAM address generator. They are separate resources, so a host request does not delay them. `idleStep()` does nothing while running, and a request made while halted waits for the next idle step (the caller supplies one per ~300 ns of emulated time).
- Evidence: ESP notes part 2 §5.4.1 ("approximately one access per microinstruction period, or 300ns"; "host GPR/INSTR accesses are substituted for refresh cycles"; "A new refresh address will be generated every microinstruction period").
- Confidence: Low-medium.
- Status: Implemented in Esp::idleStep (Task 9). Tests: host_transfers_while_halted_one_per_idle_step, ram_clear, refresh_disable_freezes_dbase.

## E29: Serial control register and test-only registers
- Options: (a) $18 stores the written byte and reads back `(v & 0xFC) | 3` (bits 1:0 are fixed 1); the master/slave, Sony/I2S and SERn direction bits do not change `setSerialIn`/`serialOut`, which stay plain latches (the trace runner decides routing); $16 reads the PC (always 0 at a sample boundary, so the firmware's "PC < 0x28" poll passes); $17 and the $1F read (refresh and frame counters, test only) read 0; (b) model the direction bits in the port latches; (c) a live refresh/frame counter.
- Choice: (a). Nothing in the emulation depends on the bits, and the firmware only writes 0x63.
- Evidence: ESP notes part 2 §5.1.1, §5.1.5, §5.1.7; findings.md (host serial control 0x63, PC poll).
- Confidence: Low.
- Status: Implemented in Esp::hostRead and hostWrite (Task 9). Tests: serial_control_register, host_register_map_details, host_firmware_boot_sequence.

## E30: Sample-rate model and halted time
- Options: (a) one `Esp::runSample()` per OTIS frame runs the program to END with no instruction budget; halted time advances only through explicit `idleStep()` calls; (b) a slots-per-sample budget (one instruction per 375 ns: 3 MCLK per instruction at 8 MHz) with an advance API that interleaves running and halted time.
- Choice: (b), implemented as `Esp::advance(int slots)` (ruling R17), with (a) kept as `runSample()`/`idleStep()`, whose behaviour is unchanged. `advance` spends one serial frame of `slots` instruction periods. Running: it calls `runSample()`; if that END halted the chip (HALT pin high) it calls `idleStep()` for the rest of the frame (`max(0, slots - steps)` times); otherwise the frame is spent. Halted: it calls `idleStep()` `slots` times. It returns the instructions executed (0 while halted). Overrun: a program longer than `slots` still runs to END (P1); the over-budget instructions take no extra time from a following frame, so such a program is not hardware-faithful, but the KS-32 programs are at most 89 words (`docs/rom-investigation/findings.md` section B). The board calls `advance(89)` once per frame, so a halted ESP progresses with emulated time (host requests, RAM clear, refresh).
- Evidence: findings.md section B (89-word program, 89 slots per frame at 8 MHz); decisions E11, E25, E28; ESP notes part 2 section 5.
- Confidence: Medium.
- Status: Implemented in Esp::advance, `// DECISION E30` markers in `src/esp/esp.h` and `src/esp/esp.cpp`. Tests: esp_advance_running_matches_runSample, esp_advance_halted_runs_idle_slots, esp_advance_halt_at_end_idles_rest_of_frame, esp_advance_overlong_program_runs_to_end; `runSample`/`idleStep` behaviour is covered by the tests named under E11 and E28.

## E31: Delay wrap when DBASE + offset is more than one ring length past DLENGTH
- Options: (a) full modulo: when the sum is above DLENGTH, the address is `s mod (len + unit)` (address bits only, E7 refinement (1)), so every delay access stays in words 0..DLENGTH for any DBASE and offset; (b) one subtraction of `len + unit` (E7 as first implemented), which leaves sums above `2 * len + unit` outside the ring; (c) keep (b) and have the loader put DBASE inside the ring before it releases HALT (write DBASE, or hold HALT for one refresh cycle, 65536 idle steps, about 737 frames).
- Choice: (a). (a) and (b) give the same address whenever the sum is below twice the ring length. That covers any DBASE inside the ring with an offset of at most one ring length: all the datasheet examples, the Plan 1 traces, and programs 0 to 9, whose ring is the whole 64K. They differ only when DBASE (or an offset) is far above DLENGTH, which is the state program 10 starts in after every load (P16).
- Evidence for (a): the datasheet's step list for delay addressing (ESP notes part 1, section 3.5.4) gives the out-of-range case as "(ASUM - DLENGTH) modulo (DLENGTH)". The RAM-control table (section 4.1.6) describes both delay modes as "DBASE + offset, modulo DLENGTH". MAME's es5510.cpp computes `(dbase + offset) % (dlength + memincrement)`. The firmware's program load (`esp_init_sequence.json` program_load) never writes DBASE. After its RAM clear and halted refresh, DBASE is wherever the whole-memory refresh count happens to be. With (b), program 10 would then overwrite its own gain tables after most loads on the real machine, which is unlikely in a shipped effect.
- Evidence for (b): the prose of section 3.5.4 says that DLENGTH "is subtracted from the sum". The datasheet asks that DBASE and DLENGTH be carefully initialised at program load so that the delay lines do not overwrite table data (section 3.5). That warning matters most if a stray DBASE can reach the tables, though a wrong DLENGTH does the same under (a). The ESP2 document (`docs/reference/esp2-full-text.txt` sections 7.1.3 and 7.1.5) describes a later Ensoniq chip. It specifies one subtraction, `address - (SIZEM1 + 1)`, and warns that computed offsets can land outside the modulus. A 20-bit compare-and-subtract also fits one instruction period more easily than a divider.
- Evidence for (c): the firmware's halted phase after it writes program 10's DLENGTH may last longer than the 24.6 ms one refresh cycle takes. That phase covers verified record writes, the table write and read-back, the record verify and the ramped mix GPRs. If it does, DBASE is inside the ring by the time HALT is released on the hardware, even under (b). This timing is not measured, and the player's loader gives host accesses almost no emulated time, so (c) would be a stand-in.
- Confidence: Low-medium. The ES5510 text points to (a) in two places and to (b) in one, and the hardware may do (b) and rely on the timing in (c). The only observable difference is the start of a program whose DBASE is above DLENGTH. If a hardware recording ever shows program 10 cutting off held notes right after a load, go back to (b) and model the firmware's halted time instead.
- Status: implemented in Agen::physical (`// DECISION E31`, `src/esp/esp_agen.cpp`). Tests: agen_delay_wrap_is_modulo_when_dbase_above_dlength, agen_delay_never_leaves_ring_for_any_dbase, agen_delay_walk_continuous_from_dbase_above_dlength, and synth_rom_held_note_sustains_on_every_program (program 10 now sustains: AC RMS 2546 at 0.75 s, 3004 at 2.5 s, against 16 before). The E7 tests pass unchanged. The Plan 1 traces (smoke, esp_mix, reverb_tank) and the default-effect render of `player_smoke.mid` are byte-identical before and after the change, so the MAME comparisons in `docs/verification-plan1.md` and the identity hash in `docs/verification-plan3.md` still hold.

## Trace runner

## TR1: OTIS to ESP routing in the trace runner
- Options: (a) OTIS ch0 -> aux out, ch1/ch2/ch3 -> ESP SER0/SER1/SER2 in, ESP SER3 out -> main out; (b) ch0..ch3 -> SER0..SER3 (the ESP would also need SER3 as an input); (c) another permutation.
- Choice: (a). Fixed for this plan; the same routing is used by the MAME harness (Task 12).
- Evidence: KS-32 firmware (`docs/rom-investigation/findings.md` section C): host serial control = 0x63 (SER3 output, SER0-2 inputs); every ESP program reads SER0, SER1, SER2 and writes only SER3; voices start with CR[9:8] = CA 1, 2 or 3 and CH0 is used only by the boot clock-test voice; OTIS init writes SERMODE = 0. SER0 is the dry bus (plain ALU addend), SER1 and SER2 are the effect sends (MAC operands). This agrees with the MAME audit (VFX-derived routing; MAME's Aux output on CH0 is silent on the KS-32).
- Confidence: High that CA1 -> SER0 (dry), CA2 -> SER1, CA3 -> SER2 as a mapping of roles; medium for the physical pin wiring (the firmware only shows how the programs treat each SER port, `findings.md` "Unknown").
- Status: Implemented in TraceRunner (Task 11), `// DECISION TR1` in `src/trace/trace.cpp`. Tests: trace_esp_passthrough, trace_routing_ch2_ch3_to_ser1_ser2.

## TR2: Trace runner host conventions
- Options: (a) `esphalt 1` while the ESP runs also writes host `$1F`, so it halts at once; while halted every `esp` host write is followed by one `Esp::idleStep()`, and `espidle <n>` adds more idle steps; OTIS port 3 serial input is never driven; (b) `esphalt 1` only raises the pin (the ESP stops at its next END, which the trace cannot see); (c) no automatic idle steps (select-register transfers would never land while halted).
- Choice: (a). The firmware also stops the ESP with a `$1F` write while ESPHALT is asserted (`docs/rom-investigation/data/esp_init_sequence.json`), and the idle step per write is what makes the documented host protocol (write select register, wait for the transfer) work in a script. Consequence: idle steps also run the refresh model, so DBASE moves while a trace loads a program unless host control bit 0 (Refresh Disable) is set.
- Evidence: ESP notes part 2 section 5.1.2, 5.4.1; decisions E24, E25, E28; controller rulings R2/R15.
- Confidence: Medium.
- Status: Implemented in TraceRunner (Task 11), `// DECISION TR2` in `src/trace/trace.cpp`. Tests: trace_esp_passthrough, trace_espidle_finishes_ram_clear, trace_halted_esp_outputs_nothing_and_resumes. The MAME harness maps these conventions onto MAME in `tools/mame_ref/main.cpp` (`// DECISION TR2 (MAME mapping)`, Task 12): the `$1F` write is passed to MAME's host_w, and the idle steps have no MAME counterpart; see `docs/mame-ref.md`.

## Player

## P1: ESP slots per frame
- Options: (a) 89 instruction periods per frame (8 MHz ESP, 3 clocks per instruction, one 29.76 kHz frame; every KS-32 ESP program is exactly 89 words); (b) 160 slots (the full program memory); (c) run-to-END only (no budget, choice (a) of E30).
- Choice: (a): the board calls `esp.advance(89)` once per frame. A program longer than the budget still runs to END (no mid-program cut-off, no carry-over into the next frame); the only effect of the budget is the number of idle periods after a halting END.
- Evidence: `docs/rom-investigation/findings.md` section B (89-word ESP program, 89 slots per frame at 8 MHz); decision E30.
- Confidence: Medium.
- Status: Used by `Esp::advance` callers (the KS-32 board, later Plan 3 tasks). Test: esp_advance_overlong_program_runs_to_end.

## P2: Pitch (FC table, P formula, clamps)
- Options: (a) FC table = `round(1024 * 2^(i/768))` for i in 0..767, with P = key*256 + zone.pitchWord (+ bend, P9), P clamped to [-0x4800, 0x5400], octave = floor(P / 3072), index = (P mod 3072) >> 2, FC = table << octave (or >> -octave), clamped to 0xFFFE; (b) copy the firmware table (0xC09F64) as data; (c) floating-point pitch.
- Choice: (a), integer apart from building the table. 768 entries x 4 units cover one octave (3072 units per octave, 256 per semitone); P = 0 gives FC 0x400, one sample per frame. 0x5400 is seven octaves up (FC would reach 0x20000, clamped); -0x4800 is six octaves down (FC 0x10). 0xFFFE because FC bit 0 is not implemented, so the register reads back `fc & 0xFFFE`.
- Evidence: formula round(1024*2^(i/768)). Task 2 compared it with the firmware table: the 768 words at 0xC09F64 (0x400..0x7FE) equal round(1024*2^(i/768)) for every i; the largest distance from the unrounded value is 0.4995 and no entry is an exact .5, so (a) and (b) give identical tables and `std::lround` matches (`docs/rom-investigation/effect-params.md` section 1, `tools/extract_effect_params.py`). The firmware's P = key*256 + pitch word and the [-0x4800, 0x5400] clamp are from findings.md 2a. FC step = FC >> 1 in 1/512-sample units (OTIS notes).
- Confidence: High for the table (bit-exact with the firmware); Medium for the octave shift and the 0xFFFE clamp.
- Status: `makeFcTable`, `fcForPitch`, `// DECISION P2` in `src/player/voices.cpp`. Tests: voices_fc_table_and_clamps, voices_extreme_keys_and_bend_clamp, voices_note_on_programs_firmware_registers.

## P3: Voice bus
- Options: (a) all notes go to one bus chosen by the caller, default 1, CA = bus + 1 (OTIS channel 2 by default); (b) a bus per MIDI channel or per wave; (c) a fixed bus.
- Choice: (a). `VoiceConfig::bus` (0..2) and `Voices::setBus`; the CR channel-address bits [9:8] get bus + 1, so bus 1 is `runFrame().ch[2]`. CH0 is not used (the firmware uses it only for the boot clock test).
- Evidence: decision TR1; findings.md section C (voices start with CA 1, 2 or 3).
- Confidence: Medium.
- Status: `// DECISION P3` in `src/player/voices.h` and `Voices::start`/`setBus`. Tests: voices_note_on_programs_firmware_registers (CR 0x0E0C, CA = 2), voices_attack_release_reaches_exact_silence (reads ch[2]). The bus is also switched at run time by CC103 in `Synth::midi` (0..2, others ignored and logged; test synth_controllers_and_messages).

## P4: Filter setup
- Options: (a) CR LP3 = LP4 = 1, K1 = K2 = 0xFFF0 (filter wide open) and the filter storage registers (page|0x20, regs 1-6) zeroed at every note start; (b) leave the filter state from the previous note; (c) a per-wave cutoff.
- Choice: (a), as in the firmware start order. K1 = K2 = 0xFFF0 is also written for every voice in `init()`. Clearing the storage keeps a stolen voice from leaking the previous note filter state.
- Evidence: findings.md "Voice start" (firmware order); trace test voice setup.
- Confidence: High for LP3/LP4/K, Medium for zeroing.
- Status: `// DECISION P4` in `Voices::init` and `Voices::start`. Test: voices_note_on_programs_firmware_registers (CR, K1, K2).

## P5: Envelope
- Options: (a) integer 8.8 fixed point in the volume byte, linear in the byte (a straight ramp in dB), one update per frame in `tick()`; attack to peak, decay to the sustain level, hold, release at a constant byte rate (full scale in releaseMs); defaults attack 5 ms, decay 0, sustain 100 %, release 250 ms; time base 29762 frames per 1000 ms (a non-zero time is at least one frame); (b) exponential segments; (c) floating-point envelope.
- Choice: (a). LVOL = RVOL = byte << 8, written only when the byte changes. The chip has no envelope; the firmware drives volume from software. 29762 is the OTIS frame rate 29761.8 Hz rounded (error under 0.001 %).
- Evidence: O5 volume byte; OTIS frame rate 30.476 MHz / 1024.
- Confidence: Medium (shape is a stand-in, not firmware-measured), High for the time base.
- Status: `Voices::tick`, `msToFrames`, `Adsr` defaults, `// DECISION P5`. Tests: voices_attack_release_reaches_exact_silence, voices_sustain_holds_until_pedal_up.

## P6: Velocity and CC 7 law
- Options: (a) peak volume byte = `max(0x10, 0xF0 - (127-vel)*40/127 - (127-cc7)*40/127)` in integer arithmetic, each term divided separately; (b) a log law from velocity*cc7 (the first version of this task); (c) a table.
- Choice: (a). Full velocity and CC 7 give 0xF0; each term costs at most 40 bytes, so the minimum is 0xA0 and the 0x10 floor is never reached with these terms (CC 7 = 0 is quiet, not silent: that is the exact law as specified). The sustain level uses the same law with sustainPct mapped onto 0..127: peak - (127 - pct*127/100)*40/127, floored at 0x10; 100 % is the peak, 0 % is the floor. Default channel volume is 100 (GM). A CC 7 change gives sounding voices a new peak at once; their sustain level steps to it. The law is a stand-in (the firmware velocity curve is not measured).
- Evidence: plan decision list; no firmware measurement.
- Confidence: Low for the loudness curve.
- Status: `peakByte`, `sustainByte`, `// DECISION P6`. Tests: voices_peak_law_and_floor, voices_attack_release_reaches_exact_silence.

## P7: Note end
- Options: (a) a release, or a one-shot that finished (STOP0/STOP1 read back from CR, O1), fades the volume byte until it is below 0x10 (the release time, or 2 ms for a finished one-shot), then LVOL = RVOL = 0 and CR = STOP0|STOP1 and the voice is freed; (b) cut at once; (c) rely on the sample ending in silence.
- Choice: (a): a stopped voice keeps fetching and outputting its held sample (O7), so the player must mute it itself; the short fade avoids a click. `tick()` reads CR of every sounding voice that is not already releasing. `allSoundOff` is the immediate version (no fade).
- Evidence: O1, O7, O5 (exponent 0 = silence, so bytes below 0x10 are silent).
- Confidence: Medium.
- Status: `Voices::tick`, `Voices::free`, `// DECISION P7`. Tests: voices_oneshot_end_mutes, voices_attack_release_reaches_exact_silence, voices_all_notes_off_with_pedal_and_all_sound_off.

## P8: Stealing and retrigger
- Options: (a) one voice per (channel, key): a repeated note restarts its own voice (including one in release or held by the sustain pedal); otherwise the lowest free voice, else the oldest voice in release (by start order, not the quietest), else the oldest voice; a steal is a hard re-start with the firmware start order; (b) always allocate a new voice; (c) steal the quietest.
- Choice: (a). A note-off releases that one voice, or marks it held while the sustain pedal is down; pedal up releases held voices; `allNotesOff` clears the pedals and releases everything.
- Evidence: standard polyphony practice; the firmware steal rule is not known.
- Confidence: Low.
- Status: `Voices::pickVoice`, `noteOn`, `noteOff`, `// DECISION P8`. Tests: voices_steal_33rd_note_and_release_all, voices_steal_takes_oldest_in_release_not_quietest, voices_same_key_retriggers_own_voice, voices_sustain_holds_until_pedal_up.

## P9: Pitch bend
- Options: (a) bend range +-2 semitones by default (`VoiceConfig::bendSemis`), bend = (value14 - 8192) * (bendSemis*256) / 8192 in integer arithmetic, truncated toward zero, added to P before the P2 clamps; (b) fractional (float) bend.
- Choice: (a): a bend of up to 7 units either side of centre is zero. Bend is per MIDI channel; it retunes the sounding voices of the channel at once and applies to later notes.
- Evidence: the plan pitch formula; GM default bend range.
- Confidence: Medium.
- Status: `Voices::pitchOf`, `pitchBend`, `// DECISION P9`. Test: voices_extreme_keys_and_bend_clamp.

## P10: MIDI map
- Options: (a) omni (every channel plays, the channel only keeps its own sustain, bend and volume): note on/off (velocity-0 note-on = note off), pitch bend (d1 | d2 << 7), program change = wave number in the bank set by CC0 (0 internal, 1 rom3; stored, applied at the next program change), CC7 volume, CC64 sustain (>= 64 down), CC102 effect 0..10 (queues an ESP effect load, at most one waiting behind the load in progress, P17; other values ignored and logged), CC103 bus 0..2 (others ignored and logged), CC120 all sound off, CC123 all notes off; all other messages ignored; (b) General MIDI program numbers mapped to waves; (c) one MIDI channel per bus.
- Choice: (a). The KS-32 sounds are not a GM set, so program change addresses the wave table directly. A program change to a wave with no zones is logged ("wave S:N has no zones; ignored") and the previous wave stays selected.
- Evidence: the plan (P10 row); `docs/rom-investigation/data/sample_map.json` (waves with zones, sets 0 and 1).
- Confidence: Medium (a test-player convention, not firmware behaviour).
- Status: `// DECISION P10` in `Synth::midi` (`src/player/synth.cpp`). Tests: synth_velocity0_is_note_off, synth_program_change_missing_wave_ignored, synth_controllers_and_messages.

## P11: Effect parameters (mix GPRs, output level, handler GPRs)
- Options: (a) re-run the firmware's own effect set-up on each effect's default parameters (record bytes +6..+15 at 0xC1FB8A) and use the GPR values it writes; (b) take the parameter block of a particular factory sound; (c) hand-set mix levels measured on the chip core (dry 0 dB, wet about -6 dB, the brief's fallback); (d) only the mix pairs, leaving the other handler-written GPRs at the program record presets.
- Choice: (a), from `docs/rom-investigation/data/esp/effect_defaults.json`, one entry per ESP program index. After loading the program records, the player writes every GPR in that entry. That covers the mix pairs 7C/7D (FX1), 7A/7B (FX2) and 76/77, the output level 7F, and the 6 to 18 coefficient GPRs per effect that the firmware's parameter handlers compute. The mix values are table 0xC0A72C entries, an equal-power pair `(round(32767*sin(n*pi/254)), the same at 127-n) << 8`. The FX1/FX2 index n comes from the effect's default parameters; the 76/77 index comes from $DE8A. Two ESP programs are shared by two effects each, and the JSON uses the lower effect: program 7 (effects 6, 7), program 9 (effects 9, 10).
- Evidence: `docs/rom-investigation/effect-params.md` sections 2, 3, 5 and 6. Routine 0xC1CB76 writes the mix GPRs. Handlers via 0xC0FA70 and the effect init routines (record +0) write the rest. `tools/extract_effect_params.py` runs these routines unchanged in a 68000 interpreter. Factory sounds 2, 22, 25, 26 and 36 carry exactly the record defaults. The GPRs only the host supplies (7A-7D and 7F everywhere, 76/77 in programs 5 and 6, 86 in programs 5-7, 8F in program 6) are all among the firmware writes.
- Hand-set inputs (**provisional**, logged in each `basis`):
  - The block is the effect default, not a specific sound's parameters.
  - Master volume $DD72 = 127, which gives 7F = 0x7FFF00.
  - $DE8A = 0 for effects that never set it (only programs that do not read 76/77).
  - Modulation sources are at rest.
- Confidence: High for the tables and index rules. Medium-high for the default index. Medium for the handler values, since the interpreter was checked by hand on five handlers but not against hardware.
- Status: data produced by Task 2. No mix level is hand-set, so the chip-core fallback was not used. Used by `EspSequencer::queueLoad` (`src/player/esp_loader.cpp`, Task 5): every `mix` GPR except 7F is written while halted, in ascending address order, after the program records and the DRAM tables; 7F is the fade-in target (P13). Tests: effects_real_defaults_file, loader_program10_loads_instr_gpr_tables, loader_every_program_runs_and_decays.

## P12: Record write order
- Options: (a) GPR 00-8F cleared, then the type-1 range [0, type1Count) via $E0 (GPR + INSTR), the type-2 range [type1Count, type1Count + type2Count) via $C0 (INSTR only), then every other GPR record ascending via $A0; (b) write the records in file order; (c) INSTR first for all 89 words, then all GPRs.
- Choice: (a). A $E0 write carries the program's GPR value for that address, or 0 if the program has no record for it. GPR records below type1Count were already written by $E0 and are not repeated; the rest (addresses from type1Count up, including the SPRs) follow in ascending order, so MEMSIZ/DLENGTH/ABASE/BBASE/SIGREG (F4-F9) come last. Everything is written while halted, so the order cannot be heard; it only matters for the read-back state at release.
- Evidence: `esp_init_sequence.json` program_load ("records": type 1/2/3/4); `esp/esp_programs.json` keeps the record types and counts but not the order of the type-4 GPR records. In every program type1Count + type2Count = 89.
- Confidence: Medium (the per-record order is lost; the end state is the same for any order).
- Status: `// DECISION P12` in `EspSequencer::queueLoad`. Test: loader_program10_loads_instr_gpr_tables (all 89 words and every non-mix GPR record read back at the moment HALT is released).

## P13: Output fade timing (GPR 7F)
- Options: (a) the firmware's ramp: GPR 7F from 0 to the target in steps of 0x012F00 (111 verified host writes for target 0x7FFF00), paced by the ESP host protocol, about 6 frames per write, 666 frames (22.4 ms) in all; (b) 20 ms, 595 frames, one write per frame (the plan's placeholder); (c) the lower bound, one transfer per frame, 333 frames.
- Choice: (a): `fade` in `effect_defaults.json` (`frames` 666, `writes` 111, `step` 012F00, `from` 000000, `to` 7FFF00). The player may step the level every frames/writes frames, or interpolate linearly over 666 frames. The firmware fades down before a program load: 110 writes of -0x012F00 from the current level to 0. Its mix ramps run while 7F = 0, so the player can set the mix GPRs at once.
- Evidence: `docs/rom-investigation/effect-params.md` section 4:
  - 0xC1CC1A..0xC1CC38 ramps up and 0xC1CC62 ramps down. Neither loop has a timer.
  - Each write (0xC1CC80) is three host transfers: select $A0, read-back $80, park $C0.
  - The ESP makes one transfer per sample, at END (E19 host part, E26).
  - About 360 68000 cycles between a completed transfer and the next select write miss the PC < 0x28 window (15 us at 8 MHz ESP), so each transfer takes 2 frames for any 68000 clock from about 8 to 16 MHz.
- Confidence: Low-medium. The step and count are exact; the frames per transfer are a cycle-count estimate (1 to 3 possible), and interrupts can only lengthen the ramp.
- Status: data produced by Task 2. The player (Task 5, `EspSequencer::ramp`, `// DECISION P13`) interpolates linearly: callers pass `fadeFrames` (the library's 666), and the ramp writes GPR 7F once per frame for that many frames, value from + (to - from) * k / frames for k = 1..frames, so it ends exactly at the target (0 on the way down, the `mix` 7F value on the way up). Each step is data $00-$02, select $A0 = 0x7F, one frame, then Host Access OK: while running the transfer lands at that frame's END. A ramp whose start equals its target (the first load after boot, level 0) is one write. The level is the sequencer's own copy of the last value it queued, never read back from the chip. The firmware's read-back and park transfers are not modelled per step, so the staircase becomes a smooth line of the same length. Tests: loader_switch_while_running_fades_out_first (32-frame fade-out, still running until 7F reaches 0), loader_program10_loads_instr_gpr_tables.

## P14: ESP boot
- Options: (a) at start-up, run `boot_init` from `esp_init_sequence.json` through the host interface, wait for the RAM clear, park the read select at $C0 and release HALT; (b) set the chip state directly (setGpr/setInstr); (c) leave the ESP halted until the first program load.
- Choice: (a). Order: HALT pin high, $1F = 0x02, $1F = 0xFF, $E0 = 0x9F, MEMSIZ F4 = 0x0000FF, DLENGTH F5 = 0xFFFFFF, DBASE F8 = 0x000F00, $12 = 0x03 then 0x02 (RAM clear), $18 = 0x63, GPR + INSTR 00-9F = 0xFFFFFF / END, GPR A0-BF = 0xFFFFFF, SIGREG F9 = 0xFFFFFF, serial registers EA-F1 = 0, then wait for $12 bit 1 to clear, $80 = 0xC0, HALT pin low. Every select write is followed by a wait for Host Access OK. The read-back verification and retries of the firmware are not modelled (the core never needs a retry, test host_firmware_boot_sequence). The sequencer's output level starts at 0: GPR 7F holds 0xFFFFFF after the fill, but the all-END program outputs nothing and the first load clears GPRs 00-8F.
- Evidence: `esp_init_sequence.json` boot_init (routine 0xC1C7E8); findings.md 2b (ESPHALT is touched only around loads, from 0xC1C7E8 among others); E25 (a clear still running when HALT is released is abandoned, hence the wait). The clear of 65536 words takes 65536 idle steps; with 89 idle steps per frame plus one per host write the boot ends after 718 frames (24 ms of silence).
- Confidence: Medium-high for the register order (transcribed from the firmware); medium for the release of HALT at the end of boot.
- Status: `// DECISION P14` in `EspSequencer::queueBoot` (`src/player/esp_loader.cpp`). Test: loader_boot_sets_firmware_state.

## P15: SMF parsing
- Options: (a) strict reader of the Standard MIDI File spec with these choices: chunks of an unknown type are skipped (bounds-checked by length) while `ntrks` MTrk chunks are required, and bytes after the last track are accepted; one global tempo map built from the tempo events of all tracks (default 500000 us per quarter note); a track longer than 2^32 ticks is rejected (keeps tick * tempo inside uint64); `ntrks == 0` is rejected; running status is cancelled by meta and sysex events, as the spec says; a status byte (bit 7 set) where a data byte belongs is an error; delta times and meta/sysex lengths of more than 4 bytes are errors; (b) a lenient reader that keeps running status across meta events and resynchronises on bad data; (c) per-track tempo maps.
- Choice: (a). Failures return false with a message and never read past the buffer; a file is either parsed whole or rejected. Some real files keep using running status after a meta event; the reader rejects such a file with "data byte without running status" rather than guess (re-save it with a sequencer, or relax this rule). A status byte in a data position is reported as "status byte in data position" instead of silently desyncing the rest of the track.
- Evidence: the SMF 1.0 specification (chunk skipping, running status, variable-length quantities of at most 4 bytes); the plan timing formula (us = floor(sum(ticks * tempo) / ppq), frame = floor(us * 30476180 / 1024000000)).
- Confidence: High for the spec rules, Medium for rejecting rather than tolerating files that continue running status after a meta event.
- Status: `// DECISION P15` markers in `parseSmf` (`src/player/smf.cpp`). Tests: smf_skips_unknown_chunk, smf_rejects_5_byte_vlq, smf_rejects_status_in_data_position, smf_rejects_truncations_without_crash, smf_rejects_bad_headers, smf_tempo_change_midway.

## P16: ESP output DC residue
- Options: (a) accept the small negative DC the effect programs 0 to 9 settle on after a sound ends (-3..-18 LSB on the Task 5 decay-test stimulus, 0..-23 LSB on the smoke file); (b) change the ESP core's MAC readout / DOL rounding to round toward zero; (c) add a DC blocker in the player.
- Choice: (a) for programs 0 to 9. Program 10 (below): its cut-off was a delay-wrap issue, fixed by E31. Its idle DC is a static offset in the program itself, which is also accepted (a).
- Evidence, programs 0 to 9: on the Task 5 decay-test stimulus, 9 of 11 programs settle on -3..-18 LSB DC (about -80 to -65 dBFS) with AC RMS of at most 0.87 LSB. On the Plan 3 smoke file (`docs/verification-plan3.md`, effect sweep) effects 0 to 9 settle on 0..-23 LSB (about -63 dBFS at the worst; effects 4 and 8 are about 0), with AC RMS of at most 0.7 LSB. This is consistent with the floor truncation of the DOL write and the MAC readout already pinned by Plan 1 (`docs/verification-plan1.md`: the 2 LSB zero-input limit cycle; MAME agrees bit for bit on the esp_mix and reverb paths), and the KS-32's analog outputs are AC-coupled.
- Program 10 (diagnosed in the final-review fix round, resolved 2026-10-21). Held notes were cut off about 1.5 s after the effect was loaded, later notes on the wet buses stayed silent until the next load, and the program sat on a DC that the truncation argument does not explain.
  - Symptom (before E31). A note held on bus 1 (`--wave 0:0`, any velocity) fell from 2500 to 3800 AC RMS 0.75 s after the load to 10 to 20 by 2.5 s, while programs 0 to 9 sustain. A note started 3 s after the load was silent from the start (AC RMS 0.4). On bus 0 (dry), notes sounded normally. This was the same on MinGW i686 and MSVC x64.
  - Cause. Program 10 is the only program with a short delay ring (DLENGTH 0x1F7DFF, ring words 0..0x1F7D) and lookup tables above it (BBASE word 0x1F7E, ABASE word 0x1FBF, 65 words each). The load never writes DBASE, and neither does the firmware (`esp_init_sequence.json` program_load). After the load's RAM clear, the halted refresh reloads DBASE from the whole-DRAM DLENGTH (0xFFFF). The player writes the records, tables and handler GPRs within about a frame, so HALT is released with DBASE = word 0xF717, far above the ring, and DBASE needs about 1.85 s of one-word decrements to enter it. Under the single-subtraction wrap that E7 first used, the sums stayed outside the ring until then, so the program's delay writes landed all over the DRAM. When DBASE passed about 0x3F7D to 0x245F (1.5 to 1.8 s after the load), the writes overwrote both tables. Table B is the compressor's gain curve: program words 0x18/0x19 read it with the envelope 0x40 as the index, and the result, scaled by 0x89, is the gain 0x0B. Before the overwrite the gain is 0x0556xx to 0x07Axx for envelopes 0x0B0000 to 0x108609. After it, the gain is near zero or negative for the same envelopes (for example 0xFF85B4), so the wet path went quiet.
  - Evidence. A per-0.1 s check of DRAM words 0x1F7E to 0x1FFF against the loaded tables found 0 of 130 different up to 1.5 s and all 130 different from 1.8 s on. Writing DBASE = 0 together with the handler GPRs, before HALT is released, kept the tables intact for 6 s, and the note sustained (left AC RMS 3839 at 0.75 s and 3733 at 2.5 s, against 3839 and 17 without it).
  - Ruled out. The boot fill of GPRs 0x90 to 0x9F: program 10 reads only 0x91, 0x92 and 0x99 there, the firmware handlers write all three (P11), and it reads nothing in 0xA0 to 0xBF. The P11 handler values (compressor threshold 0x89, gate thresholds 0x8E/0x20/0x22, gain scale): the gate stays open and the gain is plausible until the tables change. The table read addressing (ABASE/BBASE, E7/E20): the lookups return the loaded curve until the words are overwritten.
  - Fix: E31, a full-modulo delay wrap in the chip core (options and evidence there; the loader alternative is E31 (c)). With it, the held note sustains: AC RMS 2546 at 0.75 s and 3004 at 2.5 s. On the smoke file the AC -60 dB time is 0.7 s after the last note-off (it was -3.5 s, a cut-off before the note-off), like the other reverbs.
  - Idle DC, traced 2026-10-21. With the tables intact, program 10 with no input sits on -158 LSB (left) and -72 LSB (right) DC, with AC RMS under 1, from the moment it starts. The Task 5 "140 to 157 RMS with no input" was this DC measured as raw RMS, not sound. The other values seen before E31 were this idle state after the table overwrite, which depends on what was playing when the writes swept through: -13.8 on the decay stimulus, and on the smoke file -146 / -67 before the render warm-up and -58 / -26 after it (P18). The source is the program's distortion stage, a sine waveshaper. Words 0x2A to 0x32 index table A (ABASE word 0x1FBF, 65 entries from 0x8001 through 0x0000 at entry 32 to 0x7FFF) at `0x1F80 + 0x1F80 * x`, with GPRs 0x05 and 0x06 both 0x001F80 in the ROM, and interpolate between entries m and m+1. With x = 4 * GPR 0x58, which is about 0 at idle (0xFFFC90), the index is 0x1F7F, between entry 31 (0xFBE2) and entry 32 (0x0000). Word 0x32 forms 0.504 * 0xFBE200 = -0x2131E (24-bit), and word 0x33 scales it by GPR 0x99 (0x0F0000 from the handler, product shift 1) to -0x3E3E in GPR 0x38. The smoothing filters 0x39 to 0x3E have unit DC gain with the handler values (GPR 0x91 = 0, 0x92 = 0x500000), so the value reaches the reverb input and both outputs. A per-word trace of one idle sample shows exactly these values. Moving the index base to 0x2000 (GPR 0x06, experiment only) takes GPR 0x38 to 0 and the output DC to -6 / -1 LSB, the same truncation residue as programs 0 to 9. The offset therefore comes from the firmware's own constants (an index centred half an entry below the table's zero). It does not depend on rounding in the core, since the ALU index arithmetic is exact and the table words come from the ROM. It is -46 dBFS and the KS-32's outputs are AC-coupled, so it is accepted like the residue of programs 0 to 9. The low peak on the smoke file (9328 / 7965 after E31, 8839 / 7797 before) does not come from the cut-off; it is the program's level with the default parameters (P11).
- Confidence: Medium for programs 0 to 9. Program 10: High for the cut-off mechanism (reproduced, and removed by one register); Low-medium for the fix (E31); High for the origin of the idle DC (traced word by word, removed by moving the index base).
- Status: the decay tests (`tests/test_player_esp_loader.cpp` `loader_every_program_runs_and_decays`, `tests/test_player_synth.cpp` `synth_rom_note_sounds_and_decays`) check AC RMS and |DC| <= 32 separately. In `loader_every_program_runs_and_decays`, program 10 has its own check: its final DC must lie in -175..-140 (left) and -90..-55 (right), with a late peak of at most 200 (measured -158.1 / -72.2, peak 159). `synth_rom_held_note_sustains_on_every_program` (`tests/test_player_synth.cpp`) checks that a held note sustains on all 11 programs.

## P17: Effect changes while a load runs
- Options: (a) at most one effect load waits behind the one in progress: a CC 102 when the sequencer is idle queues the load at once; one that arrives while a load (or the boot) runs becomes the pending program, and a later one replaces it; `Synth::render` queues the pending program at the start of the first frame in which the sequencer is idle; (b) queue every CC 102 as a full load (the previous behaviour: a knob sweep queued up to 11 loads of about 2070 frames each, played one after another, and each queueLoad allocated about 10 000 ops inside `midi()`); (c) abort the load in progress and start the newest at once.
- Choice: (a). The fade-out, halt, load, release, fade-in order of every load is unchanged, and so is the sequencer's level bookkeeping (each load fades out from the level the previous one faded in to). A sweep therefore costs at most two loads: the first value, then the last. The constructor's first load is queued straight behind the boot (no pending step), so start-up timing is as before. (c) was not taken because it would cut a fade or a halted load half-way, which the firmware never does.
- Evidence: the final review's finding (CC 102 loads never coalesced, about 68 ms and 0.5 MB each); the firmware's own load routine 0xC1C8C0 always runs whole (effect-params.md section 2).
- Confidence: Medium (a player convention; the firmware's handling of fast effect changes is not known).
- Status: `// DECISION P17` in `Synth::loadEffect` and the `pending_` member (`src/player/synth.{h,cpp}`); `Synth::ready()` is false while a load runs or waits. Real-time contract in `synth.h`: `render()` allocates when it queues the pending load, once per effect change. Test: synth_effect_changes_coalesce (ten CC 102 back to back load only the first and the last, in about two loads' time: 4140 frames against 2070 for one; the ESP then holds the last program's instructions).

## P18: Offline render warm-up
- Options: (a) `render` and `waves --audition` run the synth into a throwaway buffer until `Synth::ready()` (ESP boot and first effect load finished, output faded in) before WAV frame 0, capped at 1 s of frames, an error if not ready by then; (b) start the WAV at power-on (the previous behaviour: a tick-0 note was silent for about 40 ms and then faded in with the effect; the first 2100 frames were boot and load); (c) delay the MIDI events by a fixed lead-in.
- Choice: (a): WAV time equals MIDI time and a note at tick 0 is at full level from its first frame. The warm-up takes 2122 frames with the default effect (boot 718, load about 1400) and its log lines (`esp booted`, `effect N loaded`) are still printed. Live `play` is not changed (its stream starts at power-on; `--demo` keeps its 0.1 s lead-in).
- Evidence: the final review's finding; test synth_ready_after_boot_and_load (ready after 2122 frames, a note started then is at its sustained level once its 5 ms attack is over).
- Confidence: High.
- Status: `// DECISION P18` at `warmUp` in `tools/ks32play/main.cpp`, called by `renderEvents`. The cross-toolchain identity hash in `docs/verification-plan3.md` was re-taken after this change.

## 68000 core

## M1: Pass criterion for the per-instruction suite
- Options: (a) exact match of registers (including the prefetch queue), cycle count and RAM, with no exceptions; (b) exact match, except two classes that are documented deviations and counted separately: `SelfModifyingPrefetch` and `AddressErrorFrame`; (c) compare only registers and RAM, not cycles or the prefetch queue.
- Choice: (b). A test passes when every register (d0-d7, a0-a6, usp, ssp, pc, sr, both prefetch words), the total cycle count (`length`) and the RAM bytes agree. Bus transactions are not compared. RAM is compared at every address the CPU wrote and every address in the test's final RAM; an address the final RAM does not list is expected to hold its initial value (else 0). d, a, usp, ssp, pc, sr and cycles must always be exact. Two classes may absorb the remaining differences. `SelfModifyingPrefetch`: the test's own final RAM shows a write inside the prefetch window [instrAddr, instrAddr + 6) (an expected final byte in the window differs from its initial value, else 0), and every difference between expected and got is a RAM byte inside that window or one or both prefetch words (the closing fetch reads the old or the new value depending on the order of write and fetch, so the symptom is usually a different prefetch word, not different RAM). `AddressErrorFrame`: the step took vector 3, the prefetch words are equal, and the only differing RAM byte is the low byte of the frame's status word, at the final SSP + 1, and only in bits 3..0 (I/N and FC2..FC0, the M12 choices); the R/W bit, the high status byte, the access address, IR, SR and PC of the 14-byte group-0 frame are exact. `SelfModifyingPrefetch` never occurs in the suite (smp 0 in every file), but the class is kept: it guards self-modifying prefetch cases the suite happens not to contain and keeps the runner's output format stable. A third class, `ManualDeviation`, is by name only: a fixed table in `src/m68k/harte_format.cpp` (`kManualDeviations`) lists exact suite test names, each with the id of the decision that follows the PRM/UM against the suite for that test; a test that would fail and whose name is in the table is a manual deviation only when every difference lies in the fields the entry allows (D registers, SR bits, one RAM byte); any other difference is a failure. An entry needs such a decision id; entries are never added just to silence a failure. A listed test that passes is reported as a warning (`WARNING stale manual deviation`), since the entry no longer describes a real difference. Table: `80ef [DIVU (d16, A7), D0] 5745` (M15; only RAM 0x7FF, the low byte of the stacked PC, may differ: expected 0x00, got 0x04); the two inconsistent ASL.b cases of M19 (`e502 [ASL.b Q, D2] 1583`: D2 and the condition codes X N Z V C; `... 1761`: D2 only). Anything else is a failure. `m68k_harte` counts the three classes as passes in `pass/total` (so they do not set exit code 1) and lists them as `smp`, `aef` and `dev`. (c) would hide the timing errors the suite exists to catch.
- Evidence: the suite README (fields `length`, `transactions`, `ram`) and its stated aim of exact per-instruction behaviour; (b) gives a hard pass criterion without hiding a real bug, and the two classes are the cases where the recorded RAM depends on the order of a write against a prefetch, or on the status-word bits the UM defines differently from the suite's generator (UM 6.3.9.1 I/N, M12 function code). Final review: all 31371 aef cases differ only in status-word bits 3..0 (the suite shows no other frame difference), and the three listed cases only in the fields above; the classes were narrowed to exactly that, and the whole suite is unchanged at 1000060/1000060.
- Confidence: Medium (the two classes are a prediction from the suite's design; Tasks 3-6 confirm or shrink them when real data hits them: any other deviation needs its own entry; a class that never occurs would be deleted, except `SelfModifyingPrefetch`, kept on purpose, see Choice).
- Status: `// DECISION M1` at `kManualDeviations`, `deviationCovers` and `classifyCore` (`src/m68k/harte_format.cpp`), `HarteResult` and `manualDeviation` (`src/m68k/harte_format.h`) and in `tools/m68k_harte/main.cpp` (Task 2; the manual-deviation class Task 4, ruling R9). Tests: harte_sample_parses_and_runs, harte_failure_classes (address-error frame limited to status bits 3..0), harte_manual_deviation_by_name (allowed fields per entry). On the real suite (this task) NOP passes 8065/8065 with smp 0, aef 0. Task 3: the 23 data-movement files pass with smp 0 and aef 766, every aef case being the M12 function-code difference (aef 0 with M12 switched to the suite's choice). Task 4: the 38 arithmetic files pass 306470/306470 with smp 0, aef 1998 (again all M12: aef 0 with M12 switched) and dev 1 (DIVU, M15). Task 5: the 42 logic, shift and bit files pass 338730/338730 with smp 0, aef 339 (all M12) and dev 2 (ASL.b, M19). Task 6: the 20 program-control and system files pass 161300/161300 with smp 0, dev 0 and aef 28268: 27970 are the I/N bit of a fault on the first fetch at an odd jump target (Bcc 2200, BSR 3980, DBcc 1964, JMP 3806, JSR 3882, RTS 4057, RTR 4027, RTE 4054; M12) and 298 the M12 function code of PC-relative sources (MOVEtoSR 139, MOVEtoCCR 159); with I/N and M12 both switched to the suite's choice these files show aef 0, so nothing else hides in the class. The whole suite after Task 6: 1000060/1000060 (smp 0, aef 31371, dev 3) on both builds, no stale manual deviation. Task 9: the same total on both builds with byte-identical `m68k_harte` output (`docs/verification-plan5.md`); smp is 0 in every file of the real suite; the class is kept (final review, see Choice).

## M2: Undefined flags and results
- Options: (a) follow the suite where the PRM says "undefined"; (b) leave such flags unchanged; (c) clear them.
- Choice: (a), per instruction:
  - ABCD, SBCD, NBCD (PRM: N and V undefined): the binary sum a + b + X (difference a - b - X) is decimal-corrected by 0x06 when the low digit exceeds 9 or there is a half carry (half borrow), and by 0x60 on a decimal carry (borrow, which also includes a borrow out of the 0x06 correction); X = C = that carry. N = bit 7 of the corrected result; V = bit 7 turned on by the ABCD correction (`~binary & result & 0x80`) or turned off by the SBCD/NBCD correction (`binary & ~result & 0x80`). Z is cleared by a non-zero result and unchanged otherwise (defined by the PRM).
  - DIVU, DIVS overflow (PRM: N and Z undefined): N and Z unchanged, V = 1, C = 0, the destination unchanged.
  - DIVU, DIVS by zero (PRM: N, Z, V undefined): N = Z = V = C = 0, X unchanged, set before the exception (the stacked SR has them).
  - CHK (PRM: N set for Dn < 0, cleared for Dn > bound, undefined otherwise; Z, V, C undefined): on a trap N = (Dn < 0), so a negative Dn above a negative bound still sets N; without a trap N is unchanged; V = C = 0; Z = (Dn == 0).
  - Task 5 adds its instructions here.
- Evidence: suite register-operand cases, all matching: ABCD 4004, SBCD 3948, NBCD 1315 (result, X, N, Z, V, C); DIVU 613 and DIVS 333 quotients, and every overflow case (DIVU 536, DIVS 887: N and Z equal their initial values in all four combinations); the one divide-by-zero case (DIVU "80ef [DIVU (d16, A7), D0] 5745": SR 0x271D -> 0x2710); CHK Dn,Dn 1216 (trap: N = (Dn < 0) in all 1000; no trap: N equals its initial value in all 216; Z = V = C = 0 throughout). No CHK case has Dn = 0, so Z = (Dn == 0) is an unchecked choice that agrees with every case (Z = 0 would too; MAME's 68000, comparison only, sets Z from Dn). The 13 files of these instructions pass, apart from M15.
- Confidence: High for BCD, DIV overflow and CHK N/V/C (thousands of cases); Medium for divide by zero (one case) and CHK Z for Dn = 0 (no case).
- Status: `// DECISION M2` at `abcd`/`sbcd` (`src/m68k/m68k_alu.cpp`, `src/m68k/m68k_alu.h`), `Cpu::opDiv` and `Cpu::opChk` (`src/m68k/m68k_ops_arith.cpp`, Task 4). Tests: m68k_alu_bcd, m68k_alu_divs_overflow_leaves_operand, m68k_divu_by_zero_traps, m68k_chk_traps_with_next_pc.

## M3: PC and prefetch convention
- Options: (a) `Registers::pc` is the address of the next instruction and `prefetch` = {word at pc, word at pc + 2}, the suite's convention; (b) `pc` is the internal address register of the last prefetch (pc + 2 under (a)), as the hardware's address bus shows it; (c) do not expose the prefetch queue and refill it from memory in `setRegs`.
- Choice: (a). The core models the two-word queue as the UM describes: IR holds the opcode, IRC the next word, and the internal `pc_` is the address of the word in IRC, so `regs().pc = pc_ - 2` and `regs().prefetch = {IR, IRC}`. `setRegs` loads IR/IRC from `r.prefetch` without touching memory and sets `pc_ = r.pc + 2`. An instruction's closing fetch moves IRC to IR and reads the word at `pc_ + 2`; an extension word is taken from IRC and refilled the same way; a jump (reset, exception) reads IR and IRC from the target. (c) would hide the self-modifying-prefetch effect that M1 has to classify.
- Evidence: the suite's README defines `pc` as "the formal program counter" (the location of the next instruction) and `prefetch` as the 2-word queue, first item fetched first. NOP.json "4e71 [NOP] 1": initial pc 3072, prefetch [0x4E71, 0x2A53]; final pc 3074, prefetch [0x2A53, 0x0679], with one read at 3076 = pc + 4. That is exactly (a) with the closing fetch at `pc_ + 2`.
- Confidence: High.
- Status: `// DECISION M3` on `Registers` (`src/m68k/m68k.h`) and at `Cpu::regs` and `Cpu::setRegs` (`src/m68k/m68k_cpu.cpp`); implemented in those and in `Cpu::prefetch`, `Cpu::nextWord`, `Cpu::fillPrefetch` (Task 1). Tests: m68k_reset_loads_ssp_and_pc, m68k_nop_takes_4_cycles, m68k_set_regs_round_trips.

## M4: Interrupt exception timing
- Options: (a) 44 clocks, UM table 8-14 (interrupt 44(5/3), "the interrupt acknowledge cycle is assumed to take four clock periods"), with no dependence on the E clock; (b) 44 plus the 0..9 clocks the VPA/VMA E-clock synchronisation adds to an autovectored acknowledge on real hardware; (c) 34, the other group-1 figure, with the acknowledge not counted.
- Choice: (a), for every interrupt whatever the vector source. Order inside the 44 (the UM gives totals, not the sequence, and the suite has no interrupt case, so this is the UM-total-compatible order closest to the suite's group-1 entry): 6 internal clocks, the acknowledge cycle (`Bus::interruptAcknowledge(level)`, 4 clocks), 4 internal clocks, the frame (PC low, SR, PC high), the vector long, the first prefetch, 2 internal clocks, the second prefetch. The acknowledge cycle is the table's fifth read; the other 12 clocks are internal. On entry the SR is copied, S set, T cleared and the mask loaded with the level (UM 6.3.2); the frame is the 6-byte group-1/2 frame with the next instruction's address, on the SSP. The group (I/N bit, trace block) is passed to the entry explicitly: an interrupt is group 1 (I/N = 1 in the frame of a fault during it) whatever vector a device supplies, including 32..47 (the TRAP range) and 5..7, which `exception()` would otherwise infer as group 2.
- Evidence: UM table 8-14 and its footnote; UM 6.3.2 (SR copy, S, T, mask, vector fetch, "The saved value of the program counter is the address of the instruction that would have been executed had the interrupt not been taken"). The KS-32 machine (Plan 6) slices the CPU by OTIS voice slot, so a cycle count that depended on a free-running E clock would break the slice invariance of M6.
- Confidence: High for 44 and the frame; Medium for the position of the acknowledge cycle among the internal clocks (only a device that watches the bus timing against the frame writes could tell).
- Status: `// DECISION M4` at `Cpu::interrupt` (the 44-clock sequence) and `Cpu::enterException` (the mask load) (`src/m68k/m68k_cpu.cpp`) and on the `interrupt` declaration (`src/m68k/m68k.h`, Task 7). Tests: m68k_autovector_interrupt, m68k_interrupt_from_user_mode_and_trace, m68k_vectored_and_spurious.

## M5: Bus errors and function codes
- Options: (a) no bus-error return and no function-code lines on `Bus`, so vector 2 is never taken; (b) a bus-error return on every access with the 14-byte group-0 frame; (c) pass FC2..FC0 with each access.
- Choice: (a). No KS-32 source of BERR is known and no KS-32 decoding of the function codes is known; Plan 6 may add either. The 24-bit address the core passes and the access width are all a device sees.
- Evidence: UM 3.8 and table 3-3 (function codes), UM 5.4.1 (bus error operation). The KS-32 board's BERR and FC wiring is not documented in this repository.
- Confidence: Medium (it holds until a KS-32 source of BERR is found).
- Status: `// DECISION M5` on `struct Bus` (`src/m68k/m68k.h`, Task 1).

## M6: Run slices
- Options: (a) `run(n)` executes whole instructions and exceptions until at least n cycles have passed and returns the cycles used (overshoot less than one instruction); a halted or stopped CPU consumes exactly the rest of the slice; (b) stop before an instruction that would cross n (undershoot), which makes the result depend on how the caller slices time; (c) split instructions at bus-cycle granularity.
- Choice: (a). `run(n)` with n <= 0 returns 0 and runs nothing. If the CPU halts or stops in the middle of a slice, the rest of the slice is consumed as idle time (added to `totalCycles()`). `step()` on a halted or stopped CPU idles 4 cycles (one bus-cycle time) and returns 4, so a loop of `step()` calls still advances time. A caller that loops `run(1)` until `totalCycles()` reaches T ends at the same instruction boundary as one `run(T - start)`, so results do not depend on slicing (Task 7 adds interrupts and checks this).
- Evidence: none in the manuals (an emulator interface choice). (c) costs speed and nothing in Plan 5 needs it; Plan 6 can revisit it if a device needs bus-cycle timing.
- Confidence: High.
- Status: `// DECISION M6` at `Cpu::step` and `Cpu::run` (`src/m68k/m68k_cpu.cpp`, Task 1). Tests: m68k_run_returns_whole_instructions; the halted and stopped cases are tested in Tasks 3, 6 and 7 (m68k_double_fault_halts, m68k_runaway_into_fill_halts_cleanly, m68k_stop_loads_sr_and_stops, m68k_stop_wakes_on_unmasked_interrupt_only, m68k_slicing_is_invisible). Task 6: a pending trace exception (UM 6.3.8) is a step of its own and ends a STOP, so `step()` and `run()` treat a stopped CPU with a trace pending as running. Task 7: `run()` likewise treats a stopped CPU with an interrupt to take as running (`step()` takes it), counts the slice in 64 bits and clamps the returned cycles to `INT_MAX`; repeated `run(1)` until a cycle count is reached and one `run(n)` end at the same instruction boundary with the same registers, memory and `totalCycles()`, also with interrupt levels that change between slices (m68k_slicing_is_invisible, m68k_slicing_is_invisible_with_interrupts, m68k_run_is_overflow_safe).

## M7: Interrupt level sampling
- Options: (a) `setIpl(level)` latches the level; at each instruction boundary (the start of every `step()`, also of a stopped CPU) the latched level is compared with the SR mask: a level above the mask is taken (levels 1..6 are level-sensitive), and level 7 is taken in addition once for each rise to 7 whatever the mask (an edge latch); (b) level 7 only on the edge, never by level comparison; (c) sample the level inside instructions (bus-cycle granularity).
- Choice: (a). `pendingInterrupt()` returns the latched level when it is above the mask, or equals 7 with an undelivered rise; delivering a 7 clears the latch. `setIpl(7)` from a level below 7 arms the latch; `setIpl` to any other level disarms it (a request that came and went between two boundaries is never sampled); `reset()` disarms it. Priority at a boundary: a pending trace exception, then an interrupt, then the next instruction (UM 6.3.9: trace, then interrupt; the trace handler's first instruction runs after the interrupt entry). An interrupt ends a STOP (stacked PC: the instruction after STOP, M22); a masked level leaves a STOP running; a halted CPU (M9) ignores interrupts. A group-1 exception inside an instruction still blocks that instruction's trace (Task 6 `traceBlocked_`). The brief's wording for level 7 ("edge-triggered once per rise to 7 regardless of mask") holds for mask 7, where only the edge can deliver it. For a mask below 7 the UM's explicit sentence also applies ("A level 7 interrupt may still be caused by the level comparison if the request level is a 7 and the processor priority is set to a lower level by an instruction"), so a level held at 7 is taken again when an instruction or an RTE drops the mask. That is the only place (a) differs from (b).
- Evidence: UM 6.3.2 (pending interrupts are detected between instruction executions; a priority above the mask is taken; the level 7 rules quoted above); UM 6.3.9 (trace before interrupt); PRM STOP (an interrupt above the mask ends it).
- Confidence: High for levels 1..6, the edge and the order against trace; Medium for the held-7 re-take (UM sentence, no suite case; the KS-32 is not known to use level 7).
- Status: `// DECISION M7` at `Cpu::pendingInterrupt` (the level comparison and the level 7 latch), `Cpu::setIpl` (the latch), `Cpu::step`, `Cpu::run` and `Cpu::reset` (`src/m68k/m68k_cpu.cpp`), `Cpu::opStop` (`src/m68k/m68k_ops_flow.cpp`), and on `setIpl` and the `irq7Pending_` member (`src/m68k/m68k.h`, Task 7). Tests: m68k_masked_level_waits, m68k_level7_is_edge_triggered, m68k_stop_wakes_on_unmasked_interrupt_only, m68k_halted_cpu_ignores_interrupts, m68k_interrupt_from_user_mode_and_trace.

## M8: RESET instruction
- Options: (a) 132 cycles as UM table 8-12 gives (132(1/0)); `Bus::resetPeripherals()` is called once; every register except the PC (and the prefetch queue, which the closing fetch moves on) is unchanged; privileged; (b) also reset CPU state (SR, vectors), as an external reset would; (c) a fixed cost without the bus call.
- Choice: (a). Sequence: 4 internal clocks, `resetPeripherals()`, 124 internal clocks (the RESET line asserted for 124 clocks, PRM RESET), the closing fetch. In user mode it takes the privilege violation (vector 8, 34 cycles) and the bus is not called.
- Evidence: PRM RESET ("Asserts the RSTO signal for ... 124 [clocks] for MC68000 ... The processor state, other than the program counter, is unaffected"); UM table 8-12 (132(1/0)); UM 6.3.7 (RESET is privileged). Suite RESET.json: all 8065 cases 132 cycles with the bus log n4, n124, prefetch, and no register change ("4e70 [RESET] 1"). Where in the 124 clocks a device sees the reset does not show in the suite; at their start is the PRM's reading.
- Confidence: High.
- Status: `// DECISION M8` at `Cpu::opReset` (`src/m68k/m68k_ops_flow.cpp`, Task 6). Test: m68k_reset_instruction_calls_bus (and m68k_privilege_violation for user mode); RESET passes 8065/8065.

## M9: Double fault
- Options: (a) an address error during group-0 processing (the address-error exception itself, or reset) halts the CPU: `halted()` becomes true and only `reset()` clears it; (b) take another address error (an odd SSP or vector would loop forever); (c) treat it as a reset.
- Choice: (a). `step()` runs the address-error exception inside a second `try`; a fault there sets `halted_`. `reset()` halts when its initial PC is odd. A fault during group-1 or group-2 exception processing (an odd SSP while stacking, or an odd handler address) is an ordinary address error with I/N = 1 for group 1 (UM 6.3.9.1); if its own frame push faults too (odd SSP), that is the double fault. A halted CPU idles under M6.
- Evidence: UM 6.3.9.1 ("If a bus error occurs during the exception processing for a bus error, an address error, or a reset, the processor halts") and 6.3.10 (the same for an address error). The suite's Task 3 files contain no double fault.
- Confidence: High.
- Status: `// DECISION M9` at `Cpu::step` and `Cpu::reset` (`src/m68k/m68k_cpu.cpp`, Task 3). Tests: m68k_double_fault_halts, m68k_reset_with_odd_pc_halts, m68k_odd_exception_vector_takes_address_error.

## M10: DIVU/DIVS cycles
- Options: (a) the UM's upper bounds (DIVU 140, DIVS 158, table 8-4) for every case; (b) a data-dependent count that reproduces the suite; (c) a fixed average.
- Choice: (b). `divCycles(dividend, divisor, isSigned)` returns the total clocks without the source's address calculation (the closing prefetch included). The instruction spends `divCycles - 4` internal clocks and then runs the closing fetch, as in the suite's transaction logs. Counted in 2-clock units u, total = 2u:
  - Divisor 0: 38 (UM table 8-14, divide by zero, exception included).
  - DIVU: if dividend >> 16 >= divisor (overflow), 10. Otherwise u = 38, then 15 steps of the shift-and-subtract loop with D = divisor << 16: shift the dividend left by 1; if the bit shifted out was 1, subtract D (u unchanged); else u += 2 and, if the shifted dividend >= D, subtract D and u -= 1. Range 76..136.
  - DIVS: u = 6, plus 1 for a negative dividend. With q = |dividend| / |divisor| (magnitudes in unsigned 32-bit arithmetic, so 0x80000000 / -1 is defined): if q does not fit a signed 16-bit quotient (q > 0x7FFF for a positive quotient, q > 0x8000 for a negative one), overflow: u + 2 (16 or 18 clocks). Otherwise u += 55; for a non-negative divisor u -= 1 when the dividend is non-negative and u += 1 when it is negative; then u += 1 for each 0 among bits 15..1 of q. Range 120..156.
- Evidence: UM table 8-4 gives only the maxima and says the algorithm "provides less than 10% difference between the best- and worst-case timings", which the suite does not show (DIVU 76..136). Every register-operand case of the suite matches: DIVU 1149/1149 (613 quotients, 536 overflows), DIVS 1220/1220. The DIVS file passes 8065/8065 and DIVU 8064/8065 (the one failure is the stacked PC of M15; its cycles match). The loop is the 68000's restoring division as published by Jorge Cwik ("68000 DIVU/DIVS timing"), used as comparison only. With that analysis a DIVS quotient that is too large only for the signed range would run the whole loop, but the suite has the early overflow time in all 285 such register cases (16 for a non-negative dividend, 18 for a negative one), so that is what is implemented. A quotient of exactly -32768 (q = 0x8000, negative) is in range; the suite has no such case.
- Confidence: High for DIVU and for DIVS without overflow (every case matches); Medium for the DIVS signed-range overflow time (the suite's model, no second source); Low for the -32768 edge.
- Status: `// DECISION M10` at `divCycles` (`src/m68k/m68k_alu.cpp`, `src/m68k/m68k_alu.h`) and `Cpu::opDiv` (`src/m68k/m68k_ops_arith.cpp`, Task 4). Tests: m68k_alu_div_cycles, m68k_alu_divs_overflow_leaves_operand.

## M11: Address-error frame contents and the state at the fault
- Options: (a) follow the suite where the UM leaves the frame open (the UM says the stacked PC "is unpredictable and may be incremented from the address of the instruction", and figure 6-7 leaves status-word bits 15..5 undefined); (b) stack the instruction's address and zero bits 15..5; (c) stack the address of the next instruction.
- Choice: (a), with the timing and register effects the suite shows:
  - The faulting access is not run and takes no clocks. The step costs the clocks run before it plus 50 (UM table 8-14: 50(4/7)): n4, PC low, SR, PC high, IR, address low, status word, address high, the vector long, prefetch, n2, prefetch.
  - Stacked PC: `pc_ - 2` at the fault, where `pc_` is the address of the word in IRC. So it advances by 2 with every extension word or closing fetch run before the fault (MOVE.w `(xxx).L, Dn` stacks instruction + 4; MOVE.w `Dn, -(An)` stacks instruction + 2 because its closing fetch runs before the write). A fault on the first fetch at an odd jump target stacks target - 4 (`fetchJumpIr`, used by `fillPrefetch`): every odd-target case of Bcc, BSR, DBcc, JMP, JSR, RTS, RTR and RTE in the suite (27970, Task 6) matches, with the register effects that precede the fetch (BSR's push, DBcc's decrement, the SP of RTS/RTR/RTE after the pop, and RTE's new SR, which is the SR stacked). JSR faults before its push.
  - Access address: the full 32-bit address, not masked to 24 bits. IR: the instruction's opcode, even if its closing fetch already ran. SR: the SR at the fault (MOVE has set its flags before the write). Status word: bits 15..5 are IR bits 15..5, bit 4 R/W, bit 3 I/N (0 while executing an instruction or a group-2 exception, 1 in group-0/1 processing, UM 6.3.9.1), bits 2..0 the function code (M12).
  - Register effects up to the fault: a source (An)+ steps before its read and -(An) before its access; a MOVE (An)+ destination steps after the write (a faulting write leaves An); MOVE.l to -(An) steps An by 2 per word, low word first, so an odd An faults at An - 2 and keeps that value; MOVEM -(An) writes An only at the end; MOVEM (An)+ to registers has An + 2 at the first read.
- Evidence: the suite's address-error cases in the Task 3 files (MOVE.w 4697, MOVE.l 4795, MOVEA.w 2743, MOVEA.l 2686, MOVEM.w 3784, MOVEM.l 3843, CLR.w 3169, CLR.l 3162), all exact in registers, SR, PC, SSP, cycles and the stacked SR and PC. With M12 switched to the suite's function code every frame byte matches too (aef 0, checked). Examples: MOVE.w "3010 [MOVE.w (A0), D0] 4754" (50 cycles, PC = instruction), MOVE.l "2506 [MOVE.l D6, -(A2)] 92" (54, A2 - 2), MOVE.l "22cb [MOVE.l A3, (A1)+] 32" (A1 unchanged), MOVEM.w "4c98 [MOVEM.w (A0)+, #] 1" (A0 + 2).
- Confidence: Medium (the suite is one model of behaviour the UM calls unpredictable; firmware does not depend on it).
- Status: `// DECISION M11` at `Cpu::addressFault`, `Cpu::fetchJumpIr` and `Cpu::addressError` (`src/m68k/m68k_cpu.cpp`); the per-instruction orders are commented in `src/m68k/m68k_ops_data.cpp` (Task 3). Tests: m68k_address_error_frame, m68k_odd_exception_vector_takes_address_error; the suite files of Task 3.

## M12: Function code of PC-relative operand reads
- Options: (a) program space, as the PRM says (2.0: "Data items in the instruction stream can be accessed with the program counter relative addressing modes; these accesses classify as program references"); (b) data space, as the suite records them.
- Choice: (a). Without function codes on `Bus` (M5) the only place it shows is the FC field of an address-error frame's status word. The I/N bit likewise follows UM 6.3.9.1 (M11), while the suite sets I/N = 1 when the first fetch at an odd jump target faults (all 27970 such cases of Bcc, BSR, DBcc, JMP, JSR, RTS, RTR and RTE, Task 6). UM 6.3.9.1 is explicit ("The processor is processing an instruction if it is in the normal state or processing a group 2 exception"), so under ruling R8 the core keeps I/N = 0 there. Both are differences in bits 3..0 of the frame's status word, which M1's `aef` class allows (and nothing else).
- Evidence: PRM 2.0; the suite's transaction logs show FC 5 for `(d16,PC)` reads (MOVE.w "3e3a [MOVE.w (d16, PC), D7] 759"). In the Task 3 files this gives aef 766 (MOVE.w 129, MOVE.l 124, MOVEA.w 150, MOVEA.l 137, MOVEM.w 111, MOVEM.l 115), and switching to (b) gives aef 0, so no other frame difference is hidden in that count.
- Confidence: Medium (the PRM is explicit; the suite's generator differs; nothing on the KS-32 depends on it).
- Status: `// DECISION M12` at `Cpu::load` (`src/m68k/m68k_ea.cpp`), `Cpu::opMovemToReg` (`src/m68k/m68k_ops_data.cpp`), `Cpu::opMoveToSr` (`src/m68k/m68k_ops_flow.cpp`; Task 6: a PC-relative source) and, for the I/N bit of the status word (0 on a fault at an odd jump target, against the suite's 27970 cases), at `Cpu::addressFault` (`src/m68k/m68k_cpu.cpp`, Task 3).

## M13: Bus order of long accesses and of stores against the closing fetch
- Options: (a) the suite's transaction order; (b) always high word first, stores before the closing fetch.
- Choice: (a). Long reads go high word first. Long writes go high word first (MOVE to every mode but -(An), PEA and LINK pushes, MOVEM to control modes), except predecrement and read-modify-write stores, which go low word first (MOVE.l and MOVEM.l to -(An), CLR.l in every memory mode): `Cpu::write32` and `Cpu::write32Desc`. MOVE to -(An) runs its closing fetch before the write; MOVE from memory to (xxx).L writes before the fetch that replaces the low address word in IRC; CLR to memory reads, fetches, then writes. Task 4: every read-modify-write to memory (ADD, SUB, ADDI, SUBI, ADDQ, SUBQ, NEG, NEGX, NBCD) reads, fetches, then writes (a long low word first, `Cpu::storeRmw`); ADDX.l/SUBX.l -(Ay),-(Ax) read both operands low word first (An stepping by 2 per word), then write the low word, fetch, and write the high word; their byte and word forms and ABCD/SBCD read, fetch, then write. Task 5: AND, OR, EOR, ANDI, ORI, EORI, NOT, memory shifts, BCHG, BCLR, BSET, Scc and TAS follow the same read, fetch, write order (Scc reads the byte it replaces; TAS writes before the closing fetch, M20). Task 6: MOVE from SR to memory reads, fetches, then writes; BSR and JSR push the return address high word first; JSR runs the first fetch at the target before the push and the second after it; RTS pops high word first; RTE and RTR read PC high (SP + 2), SR (SP), PC low (SP + 4); MOVE to SR/CCR and ANDI/ORI/EORI to SR/CCR refetch both queue words from the next instruction after their 4 or 8 internal clocks; TRAPV's trap runs the closing fetch first, then the frame (M23 has the read counts).
- Evidence: the UM gives only access counts. Suite transactions: ADD.l "d392 [ADD.l D1, (A2)] 1513", ADDX.l "d98e [ADDX.l -(A6), -(A4)] 1" and "db8a [ADDX.l -(A2), -(A5)] 7" (an odd A2 faults at A2 - 2), MOVE.l "2100 [MOVE.l D0, -(A0)] 2394", "2080 [MOVE.l D0, (A0)] 5367", "23d0 [MOVE.l (A0), (xxx).l] 675", CLR.l "42a0 [CLR.l -(A0)] 1", MOVEM.l "48e0 [MOVEM.l #, -(A0)] 34", PEA "4879 [PEA (xxx).l] 30", LINK "4e56 [LINK A6, #] 1". The order decides which word faults (the stacked access address and PC, M11) and what a self-modifying store does to the prefetch queue (M1 `smp`); totals do not depend on it.
- Confidence: High for the cases listed (they fix the fault frames exactly).
- Status: `// DECISION M13` on `struct Bus` (`src/m68k/m68k.h`) and at `Cpu::write32Desc` (`src/m68k/m68k_cpu.cpp`, Task 3), `Cpu::storeRmw` (`src/m68k/m68k_ea.cpp`) and `Cpu::readPredec` and `Cpu::opAddxSubx` (`src/m68k/m68k_ops_arith.cpp`, Task 4). Tests: m68k_predecrement_long_write_goes_low_word_first, m68k_addx_long_predecrement_order.

## M14: ADDQ/SUBQ to an address register: cycles
- Options: (a) the suite: word 8 clocks, long 6; (b) UM table 8-5: ADDQ word 4(1/0), long 8(1/0); SUBQ word 8(1/0), long 8(1/0).
- Choice: (a). ADDQ/SUBQ #q,An change the whole register without flags; the closing fetch runs, then 4 internal clocks for the word form and 2 for the long form. Reason (ruling R8: an explicit manual statement wins unless the manual contradicts itself on that point, or the suite gives overwhelming consistent evidence that the manual's single figure cannot cover a data-dependent case): UM table 8-5 contradicts itself on these rows, so the suite decides.
- Evidence: suite ADD.w "Q, An" 407 cases all 8, ADD.l 380 cases all 6, SUB.w 384 all 8, SUB.l 351 all 6. UM table 8-5's An column contradicts itself (ADDQ.w 4 against SUBQ.w 8 for the same operation) and the suite in both sizes. MAME's 68000 and the published "Yacht" tables (comparison only) give 8 for both sizes, so the suite's 6 for the long form has no second source.
- Confidence: Medium-Low (the suite against a self-contradicting UM table; 2 clocks per ADDQ.l/SUBQ.l to An). If hardware evidence shows 8, the change is one line in `Cpu::opAddSubQuick`, and ADD.l/SUB.l would then fail exactly those 731 cases.
- Status: `// DECISION M14` at `Cpu::opAddSubQuick` (`src/m68k/m68k_ops_arith.cpp`, Task 4). Suite ADD.w, ADD.l, SUB.w, SUB.l pass.

## M15: Stacked PC of the divide-by-zero exception
- Options: (a) the address of the next instruction, as UM 6.3.5 states for every trap ("The saved value of the program counter is the address of the instruction following the instruction that generated the trap"); (b) the instruction's own address, as the suite's only divide-by-zero case stacks it.
- Choice: (a). DIVU/DIVS by zero: N Z V C cleared (M2), 4 internal clocks, then the vector-5 exception with the PC after the instruction's extension words (`pc_`): 38 + <ea> clocks.
- Evidence: UM 6.3.5. Suite DIVU "80ef [DIVU (d16, A7), D0] 5745" (the only divide by zero in DIVU and DIVS) stacks 0x0C00 for the instruction at 0x0C00, whose next instruction is at 0x0C04; its cycles (46), SR, registers and the rest of the frame match (a). CHK, the other group-2 trap of this task, stacks the next instruction's address in the suite as well.
- Confidence: Medium (an explicit UM statement against one suite case; nothing on the KS-32 is known to divide by zero).
- Status: `// DECISION M15` at `Cpu::opDiv` (`src/m68k/m68k_ops_arith.cpp`, Task 4) and at its entry in `kManualDeviations` (`src/m68k/harte_format.cpp`). Test: m68k_divu_by_zero_traps. The suite case differs only in RAM 0x7FF (the low byte of the stacked PC: expected 0x00, got 0x04) and is listed in M1's manual-deviation table, with that byte as the only field it may differ in, so DIVU shows 8065/8065 with dev 1 (ruling R9).

## M16: CHK trap cycles
- Options: (a) the suite: 38 + <ea> when Dn > bound (whatever the sign of Dn), 40 + <ea> when Dn < 0 and Dn <= bound; (b) UM table 8-14: 40(4/3)+ for every CHK trap.
- Choice: (a). After the closing fetch, a trap for Dn > bound enters the exception at once; one for Dn < 0 runs 2 more internal clocks first. Without a trap: 6 internal clocks, 10(1/0)+ (UM table 8-12).
- Reason (ruling R8: an explicit manual statement wins unless the manual contradicts itself on that point, or the suite gives overwhelming consistent evidence that the manual's single figure cannot cover a data-dependent case): the second exception applies. The UM gives one figure for a trap with two distinct causes, and the suite separates them consistently: 40 is right for the Dn < 0 cause, so the UM's figure is kept there.
- Evidence: suite CHK register-operand traps: Dn > bound 549 cases all 38 (78 of them with a negative Dn above a negative bound), Dn < 0 within the bound 451 cases all 40 (the transaction logs show n4 against n6 before the frame, e.g. "4581 [CHK D1, D2] 443" and "4581 [CHK D1, D2] 100"). The CHK file passes 8065/8065, with 3989 traps across every addressing mode.
- Confidence: Medium (the suite's model against the UM's single figure; nothing else on the 68000 is known to split it).
- Status: `// DECISION M16` at `Cpu::opChk` (`src/m68k/m68k_ops_arith.cpp`, Task 4). Test: m68k_chk_traps_with_next_pc.

## M17: BCHG, BSET and BCLR cycles on a data register
- Options: (a) UM table 8-8 as printed: BCHG 8, BSET 8, BCLR 10 for the dynamic register forms, 12/12/14 for the static ones; (b) the table's figures as maxima, with 2 clocks less when the bit number (modulo 32) is below 16.
- Choice: (b). After the closing fetch the register forms run 2 internal clocks (BCHG, BSET) or 4 (BCLR) for a bit number below 16, and 2 more from 16 up. BTST is 6 (10 static) for every number. BTST Dn,#imm (the immediate byte as operand) runs as a register operand: 6 + the immediate word's fetch = 10 (the suite; the UM has no row for it). Memory operands are bus-cycle exact (read, closing fetch, write) and need no internal clocks.
- Reason: the UM marks these figures "Indicates maximum value" (footnote *) without giving the smaller one, so the manual is silent on the rule and the suite decides (M2).
- Evidence: suite BCHG/BCLR/BSET register cases (BCHG dynamic 574 cases with a number below 16 all 6, 580 from 16 up all 8; BCLR 550 all 8 and 585 all 10; BSET 581 all 6 and 588 all 8; the static forms 4 more in each class, e.g. BCHG #n,Dn 69 all 10 and 78 all 12; BTST 6/6 dynamic and 10/10 static in both classes, including BTST Dn,#imm "073c [BTST D3, #] 91").
- Confidence: High (all 8065 cases of each file).
- Status: `// DECISION M17` at `Cpu::opBit` (`src/m68k/m68k_ops_logic.cpp`, Task 5). Tests: m68k_bset_bclr_bchg_register, m68k_btst_modulo; BTST, BCHG, BCLR and BSET suite files.

## M18: ASR by more places than the operand has bits
- Options: (a) the PRM's "C: set according to the last bit shifted out", which for a negative operand is the sign bit (1); (b) C = X = 0 for any count above the size.
- Choice: (b), for both signs. A count equal to the size still gives C = X = sign, and the result is all sign bits either way. `shift()` clears C after the loop when kind is ASR and count > 8 * size. ASL, LSL, LSR, ROx and ROXx follow the PRM's wording everywhere (their suite files pass without exceptions).
- Reason (ruling R8): the PRM does not say what is shifted out once every original bit has left, so its sentence does not decide this case; the suite shows one answer consistently.
- Evidence: ASR file cases with a register count above the size and a negative operand: ASR.b 1642, ASR.w 1063 and ASR.l 1031 cases, all with C = X = 0 (X cleared even when it was set; positive operands give 0 too, as every reading has it); with a count equal to the size the negative cases (35 in ASR.b) all have C = X = 1. Implementing (a) would fail 3736 suite cases.
- Confidence: Medium-High (consistent suite evidence, no manual statement either way).
- Status: `// DECISION M18` at `shift` (`src/m68k/m68k_alu.cpp`, Task 5). Test: m68k_shift_helper; ASR.b/w/l suite files.

## M19: Two suite cases whose recorded states are inconsistent
- Options: (a) leave them as failures; (b) list them in M1's manual-deviation table.
- Choice: (b). The suite's ASL.b file has two cases, "e502 [ASL.b Q, D2] 1583" and "e502 [ASL.b Q, D2] 1761" (ASL.b #2,D2), whose recorded final D2 differs from the recorded initial D2 in bits 31..8 (initial 0xCDFB7FBE, final 0x2E5E4304; initial 0x417C7E7D, final 0x6461D390), which no byte shift can do. The final low byte and the flags in both match a shift of a different initial byte (0x41 or 0xC1, and 0x64), so the initial D2 recorded with the test is not the one the final state came from. The core's result follows the recorded initial state and the PRM: the mechanism of M1 exists for exactly such entries, although the reason here is an inconsistent suite entry rather than a manual-versus-suite choice.
- Evidence: the original `ASL.b.json.gz` entries (initial and final state, and a normal 10-cycle transaction log); the other 8063 ASL.b cases pass, including every ASL.b #2 case.
- Confidence: High that the cases are inconsistent; the 8063 others show the shift itself is right.
- Status: `// DECISION M19` at the entries in `kManualDeviations` (`src/m68k/harte_format.cpp`, Task 5); ASL.b shows 8065/8065 with dev 2. Final review: the entries allow only D2 (and, for 1583, the condition codes X N Z V C, which the same inconsistent final state also affects); the recorded differences are exactly those.

## M20: TAS cycles on a memory operand
- Options: (a) UM table 8-6 as printed: 14(2/1)+ plus the operand's effective address time; (b) 10(1/1)+ plus that time (14 for (An)).
- Choice: (b). The effective address time of a memory mode already holds the operand read, so the table's two reads count it twice (a read-modify-write is one read, one write and the closing fetch: 12 clocks of bus cycles, 2 internal). The sequence: address calculation, byte read, 2 internal clocks, byte write, closing fetch. The read-modify-write is two plain accesses (no bus lock signal on `Bus`); N and Z come from the byte read, V = C = 0, X unchanged, and bit 7 is set in the byte written back. TAS Dn is 4(1/0).
- Evidence: suite TAS "4ad2 [TAS (A2)] 1" 14 cycles, "4aea [TAS (d16, A2)] 2" 18, "4af5 [TAS (d8, A5, Xn)] 6" 20: each is the address calculation, one 10-clock indivisible read-modify-write cycle, and the closing fetch. TAS passes 8065/8065.
- Confidence: High.
- Status: `// DECISION M20` at `Cpu::opTas` (`src/m68k/m68k_ops_logic.cpp`, Task 5). Test: m68k_tas_sets_bit7.

## M21: DBcc when the count expires
- Options: (a) n2, the first fetch at the branch target (read and dropped; an odd target is an address error, as in the looping case), then the two fetches after the displacement word: 14(3/0); (b) n2, 4 more internal clocks and the two fetches: 14 cycles with 2 reads, against the table's 3; (c) the target fetch without the address error.
- Choice: (a). Dn.w is decremented first; when it becomes -1 the branch target's first word is fetched as in the taken case, dropped, and the queue is refilled from the instruction after DBcc.
- Reason: UM table 8-9 gives 14(3/0) but not which three reads; the suite has no expired case (its 8065 DBcc cases have random counts: 4096 cc true, 2005 looping, 1964 looping to an odd target), so it is silent too. (a) is the only reading with the table's 3 reads that reuses the looping path's own first fetch.
- Evidence: UM table 8-9; suite DBcc (no expired case); the looping path "59c8 [DBcc D0, #] 1" (n2, two fetches at the target) and "53c8 [DBcc D0, #] 28" (n2, fault at the odd target with D0 already decremented).
- Confidence: Medium-Low for the third read's address and the odd-target fault (no suite case; assembled code never has an odd DBcc displacement); High for the 14 cycles.
- Status: `// DECISION M21` at `Cpu::opDbcc` (`src/m68k/m68k_ops_flow.cpp`, Task 6). Test: m68k_branch_cycles.

## M22: Prefetch queue of a stopped CPU
- Options: (a) STOP runs no bus cycle (UM table 8-12: 4(0/0)): SR is loaded from the immediate word already in IRC, `regs().pc` is the next instruction's address, and IR/IRC keep the STOP opcode and its immediate until the exception that ends the STOP refills them; (b) refill the queue from the next instruction (8 more cycles, 2 reads the table does not have).
- Choice: (a). The exception that ends the STOP (trace, and since Task 7 interrupt; a reset reloads everything) stacks `regs().pc` and refills the queue from its vector, so the stale words are never executed. Privileged: in user mode STOP takes the privilege violation and does not stop. With T = 1 at its start the trace exception follows as the next step and resumes execution (PRM STOP).
- Reason: the manuals give the timing and the PC but say nothing about the queue; the suite has no STOP file.
- Evidence: PRM STOP; UM table 8-12; UM 6.3.8.
- Confidence: Medium (only `regs().prefetch` of a stopped CPU can show it).
- Status: `// DECISION M22` at `Cpu::opStop` (`src/m68k/m68k_ops_flow.cpp`, Task 6). Test: m68k_stop_loads_sr_and_stops.

## M23: Bus reads of RTR and MOVE to CCR
- Options: (a) the suite's bus cycles: RTR is three stack reads and two fetches (5 reads), MOVE to CCR the source operand, 4 internal clocks and two fetches (2 reads besides the operand); (b) UM table 8-12's read counts: RTR 20(2/0), MOVE to CCR 12(1/0).
- Choice: (a). The cycle totals are the UM's either way (20, and 12 + <ea>).
- Reason (ruling R8): table 8-12 contradicts itself on these rows: RTR pops three words (CCR and a long PC) and must refill the queue, which no 2-read sequence can do, and RTE, which pops the same frame, is 20(5/0); MOVE to CCR does what MOVE to SR does (12(2/0)) with a byte of the same word. So the suite decides.
- Evidence: suite RTR "4e77 [RTR] 1" (reads SP + 2, SP, SP + 4, then the target and target + 2), MOVEtoCCR "44c0 [MOVEtoCCR D0] 131" (n4, two fetches) and "44d0 [MOVEtoCCR (A0)] 30"; RTR and MOVEtoCCR pass 8065/8065. The read count shows only in which program words are fetched (the queue contents) and in an address error's frame.
- Confidence: High.
- Status: `// DECISION M23` at `Cpu::opReturnFrame` and `Cpu::opMoveToSr` (`src/m68k/m68k_ops_flow.cpp`, Task 6).

## M24: Interrupt acknowledge result outside 0..255
- Options: (a) any answer of `Bus::interruptAcknowledge` other than a vector 0..255 or `kAutovector` is the spurious interrupt (vector 24); (b) mask it to 8 bits; (c) treat it as a bus error.
- Choice: (a). `kSpurious` (-2) is the documented spurious answer; an out-of-range number (negative or above 255) is a device-side error, which the 68000 handles as a failed acknowledge, that is the spurious vector (UM 6.3.4). (b) would alias a vector the device did not name, and (c) is not modelled (M5).
- Evidence: UM 6.3.4 (a failed acknowledge goes to the spurious interrupt vector).
- Confidence: Medium (an interface convention; no KS-32 device answers out of range).
- Status: `// DECISION M24` at `Cpu::interrupt` (`src/m68k/m68k_cpu.cpp`) and on `Bus::interruptAcknowledge` (`src/m68k/m68k.h`, Task 7). Test: m68k_vectored_and_spurious.

## DUART

Evidence cites the Motorola MC68681 data sheet (MC68681/D, Sep 1985, `datasheets/MC68681_DUART.pdf`, git-ignored) by paragraph and PDF page.

## D1: Serial timing
- Options: (a) bit-level TxD/RxD lines with start-bit detection, break and error detection; (b) character level: a character is a count of 16x baud ticks.
- Choice: (b). A character takes 1 start + data bits (MR1[1:0], 5 to 8) + a parity bit if MR1[4:3] is not 10 + the stop length of MR2[3:0] bit times. It is counted in 16x ticks so the sixteenth-bit stop lengths of table 4-5 are exact (6 to 8 bits: codes 0 to 7 give 9 to 16 sixteenths, 8 to F give 25 to 32; 5 bits: codes 0 to F give 17 to 32). The receiver uses the same length. A transmitted character finishes at the end of its last stop bit; a received one reaches the FIFO at the same point. Data is masked to the programmed length. There is no break detection and no receive parity or framing error: SR[7:5], ISR[2] and ISR[6] never set.
- Evidence: 3.1, 3.2 (character format; PDF 22, 23); 4.2.1.4, 4.2.1.6, 4.2.2.4 and table 4-5 (length, parity, stop codes; PDF 37, 39, 32); 4.2.9.5, 4.2.9.6 (TxEMT sets after the last stop bit, TxRDY sets when THR moves to the shift register; PDF 43). 9600 baud, 8N1 is 10 bits x 16 x 24 = 3840 X1 clocks.
- Confidence: High for the lengths; Medium for the receiver's finishing point (the real receiver samples the centre of the first stop bit, half a bit earlier).
- Status: `// DECISION D1` at `Duart::charTicks` (`src/duart/duart.cpp`, Task 1). Tests: duart_tx_character_timing, duart_char_length_parity_and_stop_bits, duart_rx_one_char_per_char_time.

## D2: Receive path
- Options: (a) receive() puts a byte straight into the FIFO; (b) bytes wait in a queue and enter the receiver one per character time at the channel's receive rate.
- Choice: (b). A byte given to receive() while the receiver is enabled joins a queue standing for the wire; the receiver takes them back to back, each finishing one character time (D1) after it starts, at the rate the channel's CSR (or the C/T) gives. A byte given while the receiver is disabled is lost, and so are the queued bytes and the character in progress when a disable or reset-receiver command arrives; characters already in the FIFO stay readable. The FIFO is 3 deep plus the shift register: a character finished with the FIFO full waits in the shift register; the start bit of the next character overruns it (the waiting character is lost, OE sets, `overruns()` counts), and a read of RHR moves a waiting character into the FIFO. OE stays until the reset-error-status command; reset-receiver leaves it alone (4.2.7.2: "All other registers are unaltered"). A receiver with no clock (an external IP clock pin with no clock wired, or a C/T that is not in a timer mode from X1 or X1/16) takes nothing.
- Evidence: 3.2 (FIFO, shift register, "held in the receive shift register until a FIFO position is available", overrun on the start bit of the next character; PDF 23, 24); 4.2.9.4 (OE; PDF 43); 4.2.7.2 and 4.2.7.4 (reset receiver, disable receiver; PDF 41, 42); 4.2.9.7, 4.2.9.8 and 4.2.15.3 (FFULL and RxRDY semantics; MR1[6] selects which drives ISR[1] and ISR[5]; PDF 43, 47).
- Confidence: High for the FIFO and overrun rules (the data sheet states them); Medium for the queue (a host convention the data sheet cannot give).
- Status: `// DECISION D2` at `Duart::startRx` (`src/duart/duart.cpp`, Task 1). Tests: duart_rx_fifo_and_overrun, duart_reset_receiver_keeps_overrun, duart_rx_disabled_drops_bytes, duart_rx_ready_interrupt_and_ffull_select.

## D3: Unmodelled features
- Options: (a) model them; (b) leave them out and count the writes that select them.
- Choice: (b). A register write that selects one of these increments `unsupportedWrites()` once and otherwise stores the value: automatic echo, local and remote loopback (MR2[7:6] not 00); RTS/CTS control (MR1[7], MR2[5], MR2[4]); multidrop (MR1[4:3] = 11, timed like a parity bit); the break commands start break and stop break (CR[6:4] = 110, 111; 101, "reset break-change interrupt", is supported: it clears ISR[2]/ISR[6], which never set); 1x external clocks (a CSR nibble 1111, which leaves that direction unclocked); a 16x external clock (CSR nibble 1110) on a pin with no clock wired; C/T clock sources other than X1 and X1/16 (ACR[6:4] = 001, 010, 100 or 101; 000, the reset value, is not counted, and the C/T does not count from it); any non-zero OPCR (OP7..OP0 stay the complement of OPR; the TxRDY, RxRDY, clock and C/T outputs are not driven). The C/T as a baud clock is supported in the timer modes from X1 and X1/16. External 16x clocks are modelled (amended in Plan 6 Task 3, because the KS-32 firmware writes CSRA = CSRB = 0xEE): `setExternalClock(pin, hz)` wires a rate to an IP pin, which reset() keeps; CSR code 1110 then ticks every X1 / hz X1 clocks (exact when hz divides X1, as 4 MHz / 500 kHz = 8 does). Pins per table 4-2: TxCA = IP3, RxCA = IP4, TxCB = IP5, RxCB = IP2. Also left out: the 25 to 50 microsecond filter on input-port changes.
- Evidence: 3.3, 3.4 (echo, loopback, multidrop; PDF 25, 26); 4.2.2.1 to 4.2.2.3 (PDF 37 to 39); 4.2.7.2 (break commands; PDF 41); 4.2.11, 4.2.14 and tables 4-2, 4-3, 4-4 (PDF 44, 46, 29, 30, 31).
- Confidence: Medium. They are features the KS-32's MIDI and panel-link use is not expected to need; the later tasks that run the ROM show whether `unsupportedWrites()` stays 0.
- Status: `// DECISION D3` at `Duart::writeMode`, `Duart::tick16` and the counting sites in `Duart::write` and `Duart::command` (`src/duart/duart.cpp`, Task 1; external 16x clocks and command 101 in Plan 6 Task 3). Tests: duart_unsupported_mode_counted, duart_external_16x_clock.

## D4: Reset state
- Options: (a) only what the data sheet lists; (b) also clear the registers it does not mention.
- Choice: (b), with the sheet's listed values taken verbatim: SRA, SRB, IMR, ISR, OPR and OPCR clear, IVR = 0x0F, OP7..OP0 pins high (the pin is the complement of OPR, so OPR = 0), channels inactive and TxD marking (transmitter and receiver disabled, so TxRDY = 0). MR1/MR2 (pointer at MR1), CSR, ACR, CTUR/CTLR and the counter also clear; the data sheet is silent on them. The sheet also says the C/T is in timer mode after reset (2.4, 3.5), but ACR = 0 encodes counter mode from IP2 (table 4-4): the model follows the ACR bits, so the C/T does nothing until ACR selects a mode (a counter also waits for its start command, D5; a timer runs, D6). The input pins are external: `reset()` keeps their levels, and they start high (unconnected TTL inputs).
- Evidence: 2.4 (PDF 17); 4.2.15 ("ISR initialized to 00"; PDF 46); 4.2.19 (IVR = 0F; PDF 48); 3.5 (timer mode after reset; PDF 26) against table 4-4 (ACR[6:4] = 000 is counter mode; PDF 31); 4.1 (mode register pointer set to MR1 by RESET; PDF 31).
- Confidence: High for the listed values; Medium for the unlisted ones.
- Status: `// DECISION D4` at `Duart::reset` (`src/duart/duart.cpp`, Task 1). Test: duart_reset_state.

## D5: Time base
- Options: (a) advance() in baud ticks; (b) in whole X1 clocks, with the C/T's X1/16 prescaler counted exactly.
- Choice: (b). `advance(n)` counts n X1 clocks. The C/T keeps the X1 clocks already spent on the current count (`ctPhase_`), so one call of n clocks and n calls of one clock count alike; a start command restarts the phase (in either mode). Counter mode, which counts only after a start command and stops when ACR leaves counter mode (D6): terminal count `preload` counts after the start, ISR[3] sets, the count rolls over from 0 to 0xFFFF and runs on; the stop command clears ISR[3] and stops it. Timer mode: terminal count every `preload` counts (square wave period 2 x preload); ISR[3] sets on every second terminal count (the sheet's "every other time"), counted from the start command or, without one, from D6's phase, so with preload 100 from X1 it sets 200 clocks after the start; the stop command clears ISR[3] but the timer runs on; a new preload is picked up at the terminal count; the minimum preload is 2. Baud rates use the fixed integer X1 divisors of table 4-6's 3.6864 MHz column: 9600 baud is 24 X1 clocks per 16x tick at any X1 (the rates scale with X1); the C/T as baud source gives 2 x preload x (1 or 16) X1 clocks per 16x tick.
- Evidence: 3.5.1 and 3.5.2 (PDF 26, 27); 4.2.15.5 (PDF 47); table 4-4 (PDF 31); tables 4-5 and 4-6 (PDF 33, 45: the 110, 134.5 and 1050 baud 16x clocks of 1.759, 2.153 and 16.756 kHz give the divisors 2096, 1712 and 220, and 2000 baud at 32.056 kHz gives 115).
- Confidence: High for the periods; Medium for the exact phase of a first terminal count on real hardware (the prescaler is free-running there; a start command is assumed to align it).
- Status: `// DECISION D5` at `Duart::advanceCounter` (`src/duart/duart.cpp`, Task 1). Tests: duart_counter_mode_ready, duart_counter_prescaler_exact_across_calls, duart_counter_reads_and_rollover, duart_timer_mode_period, duart_timer_new_preload_at_terminal_count, duart_rx_bit_clocks, duart_baud_table.

## D6: Free-running timer phase
- Options: (a) the timer runs only after a start command; (b) it runs from the moment ACR selects a timer mode, counting from the preload; (c) it runs from reset with an unknown count.
- Choice: (b). The sheet says the timer "runs continuously and cannot be started or stopped by the CPU" (3.5, 3.5.2) and gives no phase for a timer that has not been started; a start command (read of reg 14) reloads the preload, restarts the phase and the terminal-count parity (D5). Without one, a write that changes ACR[6:4] clears the count; the next advance() loads the preload then (so a preload written after ACR, before the first advance, counts), and ISR[3] sets at the second terminal count, 2 x preload source clocks later. The same write drops the counter run state, so a counter left and re-entered must be started again. Only counter mode needs the start command.
- Evidence: 3.5 and 3.5.2 (PDF 26, 27: "runs continuously", the start command "terminates the current countdown sequence ... reinitializes itself"); 4.2.13.2 (ACR[6:4]; PDF 46). The sheet's advice to issue the start command before routing the timer to OP3 (3.5.2, PDF 27; OPCR[3:2] note, PDF 44) is about configuration order and does not make the start a precondition for running.
- Confidence: Medium (the real phase of an unstarted timer depends on its prescaler and the count at power-up; any ROM that starts the timer explicitly, as the sheet recommends, is unaffected).
- Status: `// DECISION D6` at the ACR write in `Duart::write` (`src/duart/duart.cpp`, Task 1 fix round). Tests: duart_timer_runs_without_start_command, duart_leaving_counter_mode_stops_it.

## D7: Disabling the transmitter with a character in THR
- Options: (a) a disable lets only the shift-register character finish and drops one waiting in THR; (b) it lets both finish.
- Choice: (a). The disable-transmitter command resets TxRDY, TxEMT and THR (the loaded character is lost), and a character already in the shift register completes. The reset-transmitter command drops both.
- Evidence: the sheet is ambiguous. For (a): 3.1 says the RTS output negates "one bit time after the character in the transmit shift register is completely transmitted" (PDF 23), and 4.2.7.3 (CRA[3:2] = 10) says "if a character is being transmitted when the transmitter is disabled, the transmission of the character is completed before assuming the inactive state" (PDF 41). For (b): 4.2.2.2, the MR2A[5] text, says OPR[0] clears "one bit time after the characters in the channel A transmit shift register and in the transmit holding register, if any, are completely transmitted" (PDF 38), which implies a THR character is sent; but that text describes the automatic-RTS mode, not the command. 4.2.9.5 (PDF 43) says TxEMT clears when the transmitter is disabled.
- Confidence: Low to Medium (the two passages cannot both be taken as a statement about the command).
- Status: `// DECISION D7` at the disable-transmitter branch of `Duart::command` (`src/duart/duart.cpp`, Plan 6 final fix wave). No test pins it.

## KS-32 machine

Machine-level choices (`src/ks32/machine.{h,cpp}`, Plan 6). Task 2 made them provisionally; Task 3 settled K1-K3, K6, K9 and K14-K16 by running the OS ROM (v3.01), Tasks 4 and 5 settled K7, Task 6 settled K8 and K13, Task 7 confirmed K3 by MIDI in and examined K2 with notes playing, and Task 8 settled K10-K12. The evidence for those is in `docs/ks32-hardware-interface.md`, one section per fact. K4 and K5 remain Task 2's choices.

## K1: Memory map
- Options: (a) Task 2's map: 64 KB of RAM at 0xFF0000, mirrored at 0-0xFFFF only; (b) 64 KB mirrored over 0x000000-0x03FFFF and at 0xFF0000; (c) 128 KB or 256 KB at 0x000000-0x03FFFF. For the vectors: the ROM overlay of 0-7 ends at the first write into 0-7, or at the first access outside 0-7; or a RAM at 0x000000 separate from the one at 0xFF0000.
- Choice: (b). ROM at 0xC00000-0xC3FFFF: the upper EPROM supplies the even bytes, the lower EPROM the odd bytes, mirrored every 0x20000. 64 KB of RAM with A16/A17 not decoded: 0x000000-0x03FFFF (four mirrors) and 0xFF0000-0xFFFFFF. Reads of 0-7 come from ROM until the first write into 0-7. OTIS at 0x200000-0x20001F (words, reg = A4..A1), ESP at 0x260000-0x2601FF (odd bytes, reg = A8..A1), DUART at 0x280000-0x28001F (odd bytes, reg = A4..A1). Wrong-width accesses are handled as in Task 2 (a byte access to OTIS, an even byte of the ESP or the DUART); the boot makes none.
- Evidence: `docs/ks32-hardware-interface.md` "Memory map". The firmware clears 0x000000-0x03FFFF, then probes for 64 KB (word writes to 0x8000 and 0x18000, read back). Under (a) it reports event 130 (divide by zero) and touches 0x010000-0x04891C unmapped. Under (b) and (c) the boot completes with 0 unmapped accesses: 128 KB and 256 KB also boot, to the same outcome, because the firmware adapts to its probe. The service manual says "64K of RAM" (PDF p4), so (b). The overlay ending at the first access outside 0-7 makes no device access at all; a separate RAM at 0 gives the same trace hash as the shared one (one chip pair, U28/U33).
- Confidence: High for the windows and the overlay; Medium for the RAM size.
- Status: settled, Task 3. `// DECISION K1` at `isRam` and `Machine::romByte` (`src/ks32/machine.cpp`). Tests: ks32_machine_ram_write, ks32_machine_low_overlay_and_rom_mirror, ks32_machine_wrong_width_accesses_are_traced, ks32_firmware_boot_is_fully_mapped.

## K2: Interrupt map
- Options: the OTIS IRQ and the DUART INTR on levels 1-6 (7 for completeness); the DUART acknowledge answered by `duart.ivr()` or by autovector.
- Choice: the DUART is vectored (IVR) on level 3 (`kDuartIrqLevel`), and OTIS is on level 1 with autovector (`kOtisIrqLevel`). The firmware writes IVR = 0x40 (vector 64). Task 7 played notes to settle the levels, and the evidence cannot tell them apart, so they are kept (ruling T7-2). Their only basis is a recollection of MAME's esq5505 driver (DUART on IRQ 3, OTIS on IRQ 1), recalled and not checked. The IPL is the highest level of the asserting devices. Each acknowledge is traced as Device::Iack (addr = level, value = the vector answered).
- Evidence: `docs/ks32-hardware-interface.md` "Interrupts".
  - Boot (Task 3): autovector gives event 138 (spurious interrupt, at level 1) or 139 (unused vector, at level 4); the IVR answer gives neither. DUART levels 1-6 behave identically (exception counts, kinds of access, display text), and 7 breaks the boot. Outside its handlers the firmware runs at masks 0, 5 (6 instructions) and 7, so the DUART level is bounded to 1-5. IMR values written: 0x08, 0x00, 0x2B, 0x00.
  - Notes (Task 7): a 64-message MIDI burst, 24 held notes then 100 quick ones, and a note on each of the 16 channels. OS v3.01 never writes an OTIS CR with IRQE set (0 of 764 writes) and never reads IRQV, so OTIS never interrupts and its level is unobservable; the runs at OTIS levels 1 and 5 match, and the other levels are inferred. DUART levels 1-6 give the same PCM hash, exception counts and displays under each load. There are no autovectored or spurious interrupts and no unexpected event, and there are 0 MIDI overruns. Inside the level-L handler the masks are L and L - 1, relative to the handler's level, so they add no bound. Mask 5 does not appear with notes playing.
- Confidence: High for the IVR answer; Low for both levels.
- Status: IVR answer settled (Task 3). Levels examined in Task 7 and kept: the DUART is bounded to 1-5 and OTIS is unobservable with OS v3.01. `// DECISION K2` at `kOtisIrqLevel`/`kDuartIrqLevel` (`src/ks32/machine.h`) and `Machine::updateLines` (`src/ks32/machine.cpp`). Test: ks32_firmware_midi_burst_no_overrun (every acknowledge is the DUART's vector 64).

## K3: Clocks
- Options: (a) MAME's clocks; (b) values derived from the schematic crystals.
- Choice: (a), confirmed. The CPU and OTIS run at 15 238 090 Hz (30 476 180 / 2). A voice slot is 16 CPU clocks. The DUART X1 is 4 000 000 Hz. The ESP runs 89 instruction periods per frame (P1). The MIDI channel (DUART A) is clocked by an external 16x clock of 500 kHz on IP3 (TxCA) and IP4 (RxCA), 16 MHz / 32, which gives 31 250 baud (128 X1 clocks per bit). The panel channel is K16.
- Evidence: `docs/ks32-hardware-interface.md` "Clocks". The boot clock test (ACR = 0x30, counter X1/16, preload 2500, IMR = 0x08) leaves OTIS at 32 voices, so the ratio of the two clocks passes the firmware's own test (findings.md section B). The firmware writes CSRA = 0xEE (external 16x clocks), and channel A sends MIDI (control changes in running status, pitch bend), so it runs at the MIDI rate. The 16 MHz crystal is in findings.md section B.
- Confirmed by MIDI in (Task 7): after the boot `rxBitClocks(0)` is 128 (31 250 baud). A note-on on channel A sounds, and a program change selects ROM 05. A 64-message burst queued at once is received with no overrun. `kMidiChannel` = 0 (channel A). The firmware receives in omni mode: its System/MIDI defaults are "Base Channel= 01" and "MIDI Mode=OMNI", and every channel sounds. `docs/ks32-hardware-interface.md` "MIDI".
- Confidence: Medium for the CPU/OTIS and X1 values (the test constrains only their ratio); High for the MIDI clock, confirmed by MIDI in.
- Status: settled, Task 3; MIDI confirmed, Task 7. `// DECISION K3` at `kCpuHz`, `kDuartHz`, `kSerialClockHz` and `kMidiChannel` (`src/ks32/machine.h`) and the `setExternalClock` calls in the `Machine` constructor. Tests: ks32_firmware_boot_keeps_32_voices, ks32_firmware_midi_rate, ks32_firmware_midi_note_sounds, ks32_firmware_midi_burst_no_overrun, ks32_firmware_program_change_changes_display.

## K4: Scheduler
- Options: (a) per voice slot: the CPU runs 16 clocks, then OTIS, DUART and ESP steps; (b) run the CPU a whole frame, then the devices.
- Choice: (a). Each slot runs `cpu.run(16 - carry)` and carries the overshoot of the last instruction; a carry of 16 or more skips the CPU for that slot. Then `otis.stepVoice()`; the DUART advances by an exact rational X1 accumulator (16 x 4 000 000 / 15 238 090 X1 clocks per slot, the remainder kept); the ESP per K5; the IPL is recomputed. The IPL is also recomputed after every device access. The 40 reset cycles count in `cycles()` and are not carry. A frame ends when OTIS latches it (slot ACT).
- Evidence: K3. (b) would let a device see a stale CPU state for a whole frame (about 1 ms) and make the DUART timing coarse.
- Confidence: Medium.
- Status: provisional, Task 3. `// DECISION K4` at `Machine::slot` (`src/ks32/machine.cpp`, Task 2). Test: ks32_machine_frame_cadence.

## K5: ESP cadence
- Options: (a) always `esp.advance(89)` at frame end; (b) at frame end while running, and while halted spread the idle steps over the slots.
- Choice: (b). While running, `Board::route` calls `esp.advance(89)` at frame end (as `Board::frame`). While halted, slot i of an N-slot frame (N = ACT + 1) gets floor(89(i+1)/N) - floor(89 i/N) `idleStep()` calls, and `route` is given 0 slots so it does not advance the halted ESP a second time. The ESP's host accesses go straight to `hostRead`/`hostWrite`, so the firmware's loads land on the idle steps between its own writes.
- Evidence: `Esp::idleStep` cadence contract (about one per 300 ns); findings.md section B.
- Confidence: Medium.
- Status: provisional, Task 3. `// DECISION K5` at `Machine::slot` (`src/ks32/machine.cpp`, Task 2). Test: ks32_machine_halted_esp_idle_spread.

## K6: Unmapped accesses
- Options: (a) bus error; (b) reads 0 and writes ignored, with a trace event.
- Choice: (b) (the 68000 core takes no bus errors, M5). Every unmapped access is traced as Device::Unmapped with its size. With the K1 map the boot makes none.
- Evidence: M5 (`src/m68k/m68k.h`); `docs/ks32-hardware-interface.md` "Unmapped accesses".
- Confidence: High.
- Status: settled, Task 3. `// DECISION K6` at the unmapped fallthrough of `Machine::read8` (`src/ks32/machine.cpp`). Tests: ks32_machine_unmapped_is_traced, ks32_firmware_boot_is_fully_mapped.

## K7: Panel protocol
- Options: the channel (DUART A or B); a start-up handshake that the panel must answer, or none; for each command byte, its parameter count and effect; what the cursor does past the last position; how out-of-range parameters are handled. For the buttons: the byte form of a press, whether a release exists and its form (for example code | 0x80, a second code, or a different second byte), or no release with a hold expressed some other way; the code of each control.
- Choice (first part, firmware to panel): the panel is on DUART channel B (`kPanelChannel`). The machine gives `Panel::fromCpu` the bytes that finished leaving TxDB in each slot, and queues the bytes of `Panel::popToCpu` on RxDB, with no processing delay of its own; the bytes enter at the line rate (D2). There is no handshake: `popToCpu` gives only button events (second part). The display is 2 x 16 (positions 0x00-0x1F) with 15 LEDs (0x00-0x0E). Bytes 0x00-0x7F are characters. The commands are `87 pp` (cursor), `8C` (blank and home), `8D/8E/8F nn` (LED on/off/blink) and `90`/`91` (blink marking on/off). These take their parameters and change nothing modelled: `80`, `81`, `83`, `92`, `98`, `86 xx`, `94 xx`, `95 xx` and `93` with 9 bytes (Task 4 also had `88 xx` and `89 xx` here; see the second part). Past position 0x1F the cursor wraps to 0. Other command bytes, `87` with 0x20 or more and LED numbers of 0x0F or more are counted in `PanelState::unknown` and ignored.
- Choice (second part, panel to firmware, Task 5): `Panel::button(b, down)` queues `code | 0x80, 00` for a press and `code, 00` for a release; `Machine::button` passes it on, and the bytes reach RxDB from the next slot at the line rate. `Button` holds the 43 controls found (codes 0x04-0x39); Record, Stop and Play are open. Task 5 also settles `88 nn` / `89 nn` as saving the cursor in slot nn and moving it back (slots 0 and 1; others counted in `unknown`), and adds `85` with one parameter and no modelled effect.
- Evidence: `docs/ks32-hardware-interface.md` "Front panel: display and LEDs" (command table, with evidence per row). Channel B carries the display text. Before IMR enables RxRDYB the firmware never reads RHRB. A byte received during start-up is ignored and then discarded by the receiver reset at 598 ms. No single byte at 700 ms or 2000 ms changes the stream. Row 1 starts at 0x10: `87 10` is followed by 16 characters that form the second row. The wrap is never exercised (the firmware does not write past 0x1F). Buttons: "Front panel: buttons". Held Down/No repeats until `0C 00` and no other byte form stops it. Bank 9 + Screen 1/9/0 open the version, reinitialize and Analog Tests screens only while Bank 9 is held, and a press and release in one slot acts like a 100 ms press. Each code's screen matches Figure 2's labels and the inventory. Without a release, Select Sound stays held and blocks the combinations. The service manual's "Press Edit Seq/Preset" means press and release: holding it through the combination gives the Locate page.
- Confidence: High for the channel, the geometry, `87`, text, the LED numbering, "no handshake", the press and release forms and the codes marked High in the button table. Medium for `8C` blanking, on/off polarity, `90`/`91`, `88`/`89` and the codes marked Medium. Low for `8F` = blink and for the parameter counts of `85`, `86`, `93`, `94` and `95`. The effects of `80`, `81`, `83`, `92` and `98` are unknown.
- Status: settled, Tasks 4 and 5; Record, Stop and Play remain open. `// DECISION K7` at `kPanelChannel` (`src/ks32/machine.h`), the panel calls in `Machine::slot` (`src/ks32/machine.cpp`), and `paramCount`, the cursor wrap, `Panel::button` and the out-of-range rule in `Panel::run` (`src/ks32/panel.cpp`). Tests: panel_* (`tests/test_ks32_panel.cpp`), ks32_firmware_boot_shows_play_screen, ks32_firmware_version_screen, ks32_firmware_release_ends_the_hold, ks32_firmware_reinit_prompt, ks32_firmware_analog_test_page, ks32_firmware_quick_tap_is_seen.

## K8: Analog inputs
- Options: the path of the sliders, wheels, CV pedal, pressure and battery: (a) the OTIS POT converter (PAR) behind a multiplexer selected by DUART output bits; (b) the DUART input port; (c) another mapped device. For (a), the conversion completes at once when the selection changes, or after the datasheet's 1.6 ms.
- Choice: (a), at once. DUART OP3..OP0 select one of 16 inputs, and `Machine::updateLines` sets `Otis::setPot` to the selected input's raw value after every device access and every slot. Selects: 0 pitch wheel, 1 pressure, 2 mod wheel, 3 data entry, 4 CV pedal, 5 volume, 6 battery, 15 reference; 7-14 are never selected. `Machine::setAnalog(Analog, raw)` sets an input's raw 10-bit conversion, 0..0x3FF (masked).
- Evidence: `docs/ks32-hardware-interface.md` "Analog inputs". Every 4 ms the firmware reads PAR (page 0x40, reg 9) and then writes OPR set = 0x0F and OPR reset = n; no other OPR write touches OP3..OP0. Setting one select to 0x3FF or 0 moves exactly one Analog Test reading. The DUART input port reads 0xFF throughout and no reading follows it; the page makes no other device or unmapped access. The firmware reads 4 ms after each select, so a 1.6 ms conversion cannot be told from an instant one.
- Confidence: High for the path and the select table; Medium for "at once" (not observable).
- Status: settled, Task 6. `// DECISION K8` at `kAnalogSelect` and `Machine::updateLines` (`src/ks32/machine.cpp`). Tests: otis_par_reads_pot_value, ks32_firmware_analog_resting_values, ks32_firmware_slider_extremes.

## K9: Wait states
- Options: (a) none; (b) wait states on ROM, RAM or device accesses.
- Choice: (a). Every bus cycle takes the 68000's 4 clocks.
- Evidence: `docs/ks32-hardware-interface.md` "Wait states". No failure in the boot points to timing; the boot clock test, which times OTIS against the DUART, passes with none (design open item F).
- Confidence: Medium (no evidence of wait states; a timing failure in a later task reopens it).
- Status: settled, Task 3. Nothing to mark in the code: `Machine` adds no clocks to an access.

## K10: Memory card
- Options: the card-detect input (a DUART input pin, a probe of a mapped or unmapped region, or none) and its "empty" level; whether to emulate the card.
- Choice: DUART IP1 is the card detect, active low. The slot is always empty, so IP1 is high: the `Machine` constructor sets the input pins to 0x3F (the DUART's reset level). IP0 has no observable effect and rests high. The card window at 0x2E0000 (odd bytes) is not modelled; with IP1 high the firmware never touches it, and with IP1 low it reads 0 there (K6). There is no card interface (design open item E).
- Evidence: `docs/ks32-hardware-interface.md` "Memory card". The input port is read about every 28 ms and reads 0xFF. IPCR is never read, and no input-change interrupt is enabled. With IP1 low, the boot and a card operation read 0x2E0001 and 0x2E0003, write 0xFF to 0x2E0001 and read it back; with IP1 high there is no such access. On the empty slot, a copy to the card shows "SORRY! Install a RAM card first." IP0 low changes neither the boot, BankSet nor the release of a note. After the K1 probe, no RAM access falls in the mirrors at 0x010000-0x03FFFF.
- Confidence: High that the slot reads empty; Medium that IP1 is the card-detect line itself.
- Status: settled, Task 8. `// DECISION K10` in the `Machine` constructor (`src/ks32/machine.cpp`). Test: ks32_firmware_card_slot_empty.

## K11: Battery RAM image
- Options: what the image holds (the 64 KB RAM, or more state); what loading it resets (nothing, the CPU only, or the CPU and every device); whether the analog inputs are kept.
- Choice: the image is the whole 64 KB RAM (K1), raw. `loadRamImage` checks the size ("RAM image" in the error), then resets the CPU, OTIS, the ESP, the DUART and the panel, and brings back the 0-7 overlay: a power cycle with the battery connected. The RAM and the analog inputs are kept. A cold start is zero-filled RAM (`Machine::create`). `ks32emu --ram <file>` loads the file if it exists and writes it back on a clean exit (boot and render at the end, play on q).
- Evidence: `docs/ks32-hardware-interface.md` "Battery RAM". A boot from the saved image is a warm start: the RAM is not cleared, and Master Tune +02 survives. OS v3.01 powers on to ROM 00 from a cold or a warm start, whatever was selected, and MIDI out ends `C0 00` (ruling T8-1). A forced level-7 interrupt gives event 139, so there is no power-fail save. About 6 power-ons in a row of under about 2 s each reinitialize the RAM: the manual's system reset sequence (PDF p5, ruling T8-2), the real unit's behaviour, with no workaround.
- Confidence: High.
- Status: settled, Task 8. `// DECISION K11` at `Machine::loadRamImage` (`src/ks32/machine.cpp`). Tests: ks32_machine_ram_image_size_checked, ks32_firmware_ram_image_keeps_settings.

## K12: Local keyboard
- Options: the keys' path to the 68000: the panel link, the analog inputs, a DUART input pin, or a device the machine does not model. If modelled: the byte form, the key-to-note mapping and the velocity value.
- Choice: the panel link (DUART channel B). `Machine::key(note, down, velocity)` calls `Panel::key`, which queues `code | 0x80` (down) or `code` (up), with code = note - 28, then the strike value clamped to 1-127. A 0 there would make it a button event. Notes outside the 76 keys, 28-103 (E1 to G7), are ignored. Mono pressure stays analog input 1 (K8).
- Evidence: `docs/ks32-hardware-interface.md` "Keyboard". Every byte 0x80-0xFF followed by a nonzero byte plays a note, and `code, nonzero` releases it; `code, 00` and `code | 0x80, 00` are the button events. The firmware sends the key on MIDI out as note code + 28 (code 0x20 gives `90 3C`), and the measured pitches agree (codes 18-78, within 0.5 semitone). Its touch curve maps the strike value to the MIDI velocity: 127 gives 127, 64 gives 20, and 1-4 give 1. Values of 0x80 and up are erratic. The 76 keys are the span of a 76-note E-to-G keyboard; the firmware also plays codes 76-127.
- Confidence: High for the path, the byte forms and the note mapping; Medium for the key range; Low for the physical meaning of the strike value.
- Status: settled, Task 8. `// DECISION K12` at `Panel::key` (`src/ks32/panel.cpp`). Tests: panel_key_bytes, ks32_firmware_key_sounds.

## K13: Resting analog values
- Options: for each input, the raw value that gives the plan's resting state (pitch wheel at centre, mod wheel 0, CV pedal unplugged, volume at maximum so headless renders are audible, data entry at mid, pressure 0, battery nominal); for the reference (select 15), which the manual does not give, any value.
- Choice: `kAnalogRest` = pitch wheel 0x200, mod wheel 0x3FF, CV pedal 0, volume 0x3FF, data entry 0x16E, pressure 0, battery 0x298; reference 0x180 (`kReferenceRest`); inputs 7-14 rest at 0 (`mux_{}`). They read PW=064, Mod=000, CV=127, Vol=127, Knob=127, Pressure=000, Bat=164 and Reference=095.
- Evidence: `docs/ks32-hardware-interface.md` "Analog inputs". The battery reading is raw x 255 / 1023, and the boot warns below about 125 (raw 0x1E0 warns, 0x200 does not), the manual's lower bound (PDF p13: 125-205); 0x298 is the middle of the band. The pitch wheel's centre is its value at power-on, so any value reads 64; 0x200 leaves room both ways. Mod= is 0 from 0x300, Vol= 127 from about 0x2E8, Pressure= 0 up to 0x200. CV= reads 127 for raws below about 0xA0, apart from the pedal's travel (0 at 0xC0): the manual's unplugged reading (PDF p20). The reference changes nothing at or below 0x180; above it the firmware subtracts (reference - 0x180) from the other readings. Inputs 7-14 are never selected in the boot, on the play screen or on the Analog Test page, so their value is unobservable.
- Confidence: High for the battery band and the readings; Low for the reference value (any of 0-0x180 behaves the same).
- Status: settled, Task 6. `// DECISION K13` at `kAnalogRest` (`src/ks32/machine.h`), `mux_` (`src/ks32/machine.h`), `kReferenceRest` and the `Machine` constructor (`src/ks32/machine.cpp`). Tests: ks32_firmware_cold_boot_is_clean, ks32_firmware_analog_resting_values, ks32_firmware_boot_shows_play_screen.

## K14: RESET instruction
- Options: (a) reset every device; (b) the DUART only; (c) none.
- Choice: (b). `Machine::resetPeripherals()` resets the DUART and updates the IPL and the ESP's HALT pin; OTIS and the ESP are untouched.
- Evidence: `docs/ks32-hardware-interface.md` "RESET line". The OS never executes RESET (0 calls in 10 s of boot), so the three options give identical traces; (b) is kept as the harmless default.
- Confidence: High that the OS does not use it; what the line reaches is unobservable.
- Status: settled, Task 3. `// DECISION K14` at `Machine::resetPeripherals` (`src/ks32/machine.cpp`).

## K15: ESPHALT
- Options: DUART OP6 drives `Esp::setHaltPin` with (a) the pin level (high halts) or (b) its inverse.
- Choice: (a). The pin is high while OPR bit 6 is 0, so after reset the ESP halts at its first END, until the firmware sets OPR bit 6.
- Evidence: `docs/ks32-hardware-interface.md` "ESP wiring". The first DUART write is register 15 = 0x40 (OP6 high), right before the ESP halt-enable writes. Under (a) the ESP runs after the boot and no unexpected event is sent; under (b) the firmware reports event 33 ("bad ESP chip") and the ESP stays halted. findings.md section B (ESPHALT = OP6).
- Confidence: High.
- Status: settled, Task 3. `// DECISION K15` at `Machine::updateLines` (`src/ks32/machine.cpp`). Test: ks32_firmware_boot_runs_the_esp.

## K16: Panel-link serial clock
- Options: the external 16x clock on IP5 (TxCB) and IP2 (RxCB) at 500 kHz, 1 MHz or 2 MHz.
- Choice: 500 kHz, the same 16 MHz / 32 source as the MIDI channel (K3): 31 250 baud.
- Evidence: the firmware writes CSRB = 0xEE; 0.5, 1 and 2 MHz boot identically (same display text and exception counts; only the timing moves). Task 4 adds a lower bound only. There is no handshake, so there is no reply timing. While it sends the low-battery warning, the firmware writes THRB on every 1 ms tick even when TxRDYB is clear, and a panel character is 11 bits (MR1B = 0x13, MR2B = 0x0F). So the link needs 11 000 baud or more: a 16x clock of 176 kHz or more. At 153.6 kHz the warning loses characters; at 250 kHz and above it is unchanged. `docs/ks32-hardware-interface.md` "Clocks".
- Confidence: Low (500 kHz meets the bound; nothing picks it over faster clocks).
- Status: Task 3, revisited in Task 4 (lower bound). Task 5's buttons add no bound: a press and release sent back to back are all read at 500 kHz. `// DECISION K16` at `kSerialClockHz` (`src/ks32/machine.h`) and the `Machine` constructor.

# KS-32 app

## A1: MIDI-in bound
- Options: (a) an unbounded receive queue (Plan 6's deque), which allocates on the audio thread and grows without limit under a flood; (b) a fixed queue per channel that drops bytes beyond it, newest first, and counts them.
- Choice: (b), 1024 bytes per DUART channel (`ByteRing<1024> pending`, `src/common/ring.h`). A byte that finds the queue full is dropped and counted in `Duart::rxDropped(ch)`; the count clears on reset, like `overruns`.
- Evidence: Plan 7's real-time constraint and ruling F-1 of `docs/verification-plan6.md`. At 31 250 baud the line carries about 3 125 bytes a second, so 1024 bytes is about 330 ms of back-to-back MIDI, far more than one audio block brings. The queue stands for the MIDI cable, which has no buffer of its own, so the bound is an emulator choice, not a hardware fact.
- Confidence: High.
- Status: settled, Plan 7 Task 1. `// DECISION A1` at `Channel::pending` (`src/duart/duart.h`) and in the test. Test: duart_rx_queue_is_bounded; Plan 6's hashes are unchanged.

## A2: Transmit and panel rings
- Options: (a) take-by-swap vectors (Plan 6), which allocate every slot; (b) fixed rings that drop the newest byte when full and count it.
- Choice: (b). The DUART's sent-characters ring holds 256 per channel (`Duart::popTransmitted`, `Duart::txDropped`), and the panel's bytes for the firmware 256 (`Panel::popToCpu`). `Machine::slot` drains the panel channel both ways every slot; the caller pops MIDI out with `Machine::popMidiOut` (the engine every block, Task 2; `ks32emu` every frame).
- Evidence: a slot is 16 CPU clocks (about 1 us), and one character takes about 300 slots at 31 250 baud, so the panel rings never hold more than a few bytes; 256 bytes of MIDI out is about 80 ms of the line. The order is the old FIFO's, so the boot trace, render and smoke PCM hashes are unchanged (d2afd0fed8861cbd, 2bae46b3acc34cd0, a479ba867298cc55).
- Confidence: High.
- Status: settled, Plan 7 Task 1. `// DECISION A2` at `Channel::sent` (`src/duart/duart.h`), `Panel::toCpu_` (`src/ks32/panel.h`) and `Machine::slot` (`src/ks32/machine.cpp`). Tests: duart_pop_transmitted_in_order, panel_pop_to_cpu, rt_machine_runframe_does_not_allocate.

## A3: MIDI timing in a block
- Options: (a) every byte of a block enters at its start; (b) a byte at output offset `o` enters before machine frame `floor(o * 29762 / outRate)` of the block, with a fixed store per block.
- Choice: (b). `Engine::midi` stores up to 1024 bytes a block with their machine frame; `render` feeds them before that frame runs, and any left after the block's last frame enter at its end. Bytes beyond 1024 are dropped and counted in `Snapshot::midiDropped`, with the DUART's own drops (A1).
- Evidence: the design (§6.2) asks for sample-accurate MIDI within a block. A note at offset 479 sounds 478 output frames after one at offset 0 (engine_midi_offset_places_note). 1024 bytes is the DUART's whole receive queue (A1), so the store never loses what the wire could still hold.
- Confidence: High.
- Status: settled, Plan 7 Task 2. `// DECISION A3` at `Engine::midi` (`src/app/engine.cpp`) and `kMidiStore` (`src/app/engine.h`). Tests: engine_midi_offset_places_note, engine_midi_overflow_is_counted.

## A4: GUI event timing
- Options: (a) the GUI calls the machine under a lock; (b) a fixed single-producer queue the audio thread drains at the start of each block.
- Choice: (b). `Engine::post` pushes into a 256-event `SpscRing` and returns false when it is full; `render` applies the events at the start of the next block, in post order. A button or key goes on the panel link only while `Machine::panelRoom()` has room for both of its bytes (the panel's ring and the DUART channel B queue it drains into), so an event is never split; the rest wait for a later block, still in order. Analog events apply at once.
- Evidence: the real-time constraint (no locks on the audio thread). A block is 10 ms or less at usual settings, below what a mouse press can resolve. 256 events at once all arrive whole (engine_posts_never_split_a_panel_event: no panel or channel B drops). The Bank 9 + Screen 1 chord, posted with 100 ms of blocks between, opens the version screen (engine_chord_reaches_version_screen).
- Confidence: High.
- Status: settled, Plan 7 Task 2. `// DECISION A4` at `Engine::post` and `Engine::render` (`src/app/engine.cpp`) and `kEventQueue` (`src/app/engine.h`). Tests: spsc_ring_fifo_and_full, engine_chord_reaches_version_screen, engine_posts_never_split_a_panel_event.

## A5: Snapshot hand-off
- Options: (a) a sequence counter the GUI retries on (design §6.2); (b) a mutex the audio side takes with `try_lock`, skipping the block when the GUI holds it, while the GUI locks normally.
- Choice: (b). After each block the engine copies the panel state, `booted`, the frame count and the MIDI counters into `Snapshot` under `try_lock`; `Engine::snapshot()` returns a copy under the lock.
- Evidence: simpler than (a) and race-free; the audio side never waits. A skipped block only delays the display by one block. `PanelState` is plain data, so the copy does not allocate (rt_engine_render_does_not_allocate).
- Confidence: High.
- Status: settled, Plan 7 Task 2. `// DECISION A5` at `Engine::render` (`src/app/engine.cpp`). Tests: engine_boot_snapshot, rt_engine_render_does_not_allocate.

## A6: Booted
- Options: (a) watch the display for the play screen; (b) a fixed emulated time since power-on.
- Choice: (b). `Snapshot::booted` is true once the engine has run `kBootMs` (3200 ms) of machine frames since `create` or `loadRam`.
- Evidence: `kBootMs` is the measured boot (about 2121 ms) plus 50 % (`src/ks32/machine.h`). After 3000 ms of 48 kHz blocks it is false; after 3300 ms it is true and the display shows ROM 00, KS Grand Piano, with the Select Sound LED on (engine_boot_snapshot).
- Confidence: High.
- Status: settled, Plan 7 Task 2. `// DECISION A6` at `kBootFrames` (`src/app/engine.cpp`). Test: engine_boot_snapshot.

## A7: Display glyphs
- Options: for the display codes 0x00-0x1F and 0x7F, which have no known glyphs (Medium confidence that they are characters, interface doc line 309): (a) guess glyphs; (b) draw them blank and show their codes on demand.
- Choice: (b). Codes 0x20-0x7E are drawn as ASCII in a monospaced font (Consolas), one per cell of the 2 x 16 grid; 0x00-0x1F and 0x7F are drawn blank. The display's right-click menu has a "View" section with "Show display codes", which toggles a hex overlay: every cell shows its code as two hex digits, so the meaning of the unknown codes can be found by watching.
- Evidence: design section 6.3. The play screen ("ROM   00" / " KS Grand Piano") draws as the artboard shows (checked on a snapshot of the Standalone, `KS32_SNAPSHOT`).
- Confidence: High for 0x20-0x7E; Low for the rest, which is why they are blank.
- Status: settled, Plan 7 Task 6. `// DECISION A7` at `DisplayPart::paint` (`app/source/PanelEditor.cpp`). The menu is on the display, not a menu bar: the stock Standalone wrapper has no menu bar, and the VST3 has none of its own.

## A8: Blink
- Options: the blink rate of the display's blinking cells and of blinking LEDs: (a) measure it on a real KS-32; (b) a fixed rate, logged to revisit.
- Choice: (b). 2 Hz at 50 % duty (250 ms on, 250 ms off) from the message-thread clock (`Time::getMillisecondCounter`), for display cells and LEDs alike; a blinking cell is drawn blank while off.
- Evidence: not measured; no real unit to hand. The editor's 30 Hz timer repaints the display and the LEDs when the snapshot or the blink phase changes.
- Confidence: Low (a choice, not a measurement).
- Status: open to revisit with a real unit. `// DECISION A8` at `PanelEditor::timerCallback` and `DisplayPart::paint` (`app/source/PanelEditor.cpp`).

## A9: Chord latch
- Options: how to hold two buttons at once with one mouse (Bank 9 + Screen 1 and the like): (a) a modifier that latches; (b) toggle buttons; (c) no chords. And when the release is sent: (d) on mouse-up only inside the button, as `juce::Button` does; (e) on mouse-up wherever the mouse is.
- Choice: (a) and (e). A right-click or Shift-click latches a button down, drawn pressed, until it is clicked again (any click releases it). A plain press is sent on mouse-down and its release on mouse-up, wherever the mouse is. Space presses the focused button while held. When the app stops being the foreground process (Alt-Tab), every unlatched button that is down is released; latched buttons stay down.
- Evidence: design section 6.3 and review focus 3 (a stuck button breaks later chords). Each button sends a press or release only on a change of its own down state, so a late mouse-up after a focus-loss release sends nothing.
- Confidence: High for the rules; the mouse checks (version chord, drag-off, Alt-Tab) are left to the user's acceptance pass (ruling R1).
- Status: settled, Plan 7 Task 6. `// DECISION A9` at `ButtonPart` and `PanelEditor::timerCallback` (`app/source/PanelEditor.cpp`).

## A10: Strike
- Options: the strike value (1-127, `Machine::key`) of a key clicked with the mouse, which has no speed: (a) a fixed strike; (b) from where the key is clicked along its length; (c) from a modifier key.
- Choice: (b). Strike = 1 + round(126 x y / keyLength), where y is the distance from the key's back edge and keyLength the key's drawn length (white and black keys alike): the front is loud. On a drag, each key entered takes the strike where the mouse entered it. The release sends the press's strike as its release value.
- Evidence: design section 6.3. The firmware reads the strike through its touch curve (`docs/ks32-hardware-interface.md`, "Keyboard": strike 64 sounds at 20, 127 at 127), so the whole 1-127 range stays reachable on every key. The release value only reaches MIDI out as the note-off velocity.
- Confidence: Medium (a choice; the feel is the user's to judge).
- Status: settled, Plan 7 Task 7. `// DECISION A10` at `KeyPart::strikeAt` (`app/source/PanelEditor.cpp`). Louder near the front, and a glissando with no stuck notes, are left to the user's acceptance pass (ruling R1).

## A11: Memory saving
- Options: when to write the battery memory file: (a) on close only; (b) on close and every 60 s while running, never before the machine has booted; and for Erase, (c) delete the file, (d) restart the machine with zero RAM and let the next automatic save write it, or (e) write the 64 KB zero image to the file at once and restart the machine from it.
- Choice: (b) and (e) (ruling R7). The memory (`ks32-memory.bin` in `%APPDATA%\KS-32` unless changed) is loaded at start; a file of the wrong size is reported on the settings page and ignored, and the machine boots cold. It is saved on close from `machine().ramImage()` (audio stopped) and every 60 s through `requestRam`/`takeRam`, each time only when `Snapshot::booted` (A6). Each save writes `<file>.tmp` and then replaces the file (`File::replaceFileIn`). Erase asks first, then writes the 64 KB zero image to the memory file at once (`eraseMemoryFile`, the same `.tmp` and replace) and restarts the machine from that file, so the firmware rebuilds its memory as it boots. The boot gate covers the automatic saves (close and the 60 s save) only: Erase is an explicit act and a quick close must not undo it (R7). With no machine running, Erase still erases the file.
- Evidence: design sections 6.4-6.5. Six or so start-ups of under about 2 s make OS v3.01 reinitialize its memory (Plan 6), so a save before the play screen could store a half-booted image; with the A6 gate a quick open and close leaves the file untouched. The temporary file means a crash mid-write keeps the previous image whole; the 60 s save bounds what a crash loses. Checked on the Standalone with a temporary settings folder (`KS32_SETTINGS_DIR`): closed 0.5 s after the window appeared, the file kept its time stamp; after 6 s it was written on close; after 63 s running it had been rewritten by the timer; Erase followed by a close 0.8 s later left the file as 64 KB of zeros (it had 15 415 non-zero bytes before) with no `.tmp` left, and the next start booted cold from it: the image saved after 6 s differs from a true cold boot's (no memory file) in 16 bytes and from the warm image in 114. `app_tests` covers load, save over an existing file, no `.tmp` left, Erase leaving a 64 KB zero file, and the wrong size.
- Confidence: High.
- Status: settled, Plan 7 Task 5. `// DECISION A11` at `Ks32Processor::~Ks32Processor`, `eraseMemory` (also R7) and `timerCallback` (`app/source/Processor.cpp`), `saveMemoryFile` (`app/source/MemoryFile.cpp`) and the memory note (`app/source/SettingsPage.cpp`). Erase then Master Tune reading 00 on System/MIDI is left to the user's acceptance pass (ruling R1).

## A12: MIDI bytes passed
- Options: (a) only channel messages (note, controller, pitch bend and the like), filtering out sysex and real-time; (b) every byte of every `MidiBuffer` message, sysex and real-time included.
- Choice: (b). `Ks32Processor::processBlock` hands each message's bytes to `Engine::midi(samplePosition, data, numBytes)` unchanged, before `render`.
- Evidence: the MIDI IN jack receives the whole stream, and the firmware decides what to read (sysex dumps, MIDI clock and the like are its business, not the host's). The engine bounds a block's bytes (A3) and the DUART its queue (A1), so a sysex flood is dropped and counted, not allocated (engine_midi_overflow_is_counted).
- Confidence: High.
- Status: settled, Plan 7 Task 4. `// DECISION A12` at `Ks32Processor::processBlock` (`app/source/Processor.cpp`). Verified by the build and a 12 s launch of the Standalone; playing from a MIDI keyboard is left to the user's acceptance pass (ruling R1).

## A13: Default window
- Options: the editor's size: (a) the artboard's 2040 x 720 units at 1:1; (b) two thirds of it by default, resizable, aspect fixed.
- Choice: (b). 1360 x 480 by default, resizable from 1020 x 360 (half size) to 4080 x 1440, with the aspect ratio fixed at 2040:720. The whole board is drawn in design units and scaled uniformly by one transform, so the controls and their mouse areas scale together.
- Evidence: design section 6.3. The default fits a 1366-wide laptop screen; the layout table (`src/app/panel_layout.cpp`) is the artboard as a browser lays it out, rounded to whole units (tests: panel_layout_*).
- Confidence: High.
- Status: settled, Plan 7 Task 6. `// DECISION A13` at the `PanelEditor` constructor (`app/source/PanelEditor.cpp`). Readability at the minimum size is left to the user's acceptance pass (ruling R1).

## A14: Transport buttons
- Options: for Record, Stop and Play, (a) a code the running firmware answers, found by one bounded hunt that watches MIDI out as well as the display and LEDs; (b) no code, and the app shows the button disabled.
- Choice: (a) for Play (`Button::Play`, code 09, `PLAY`) and Stop/Continue (`Button::StopContinue`, code 29, `STOP_CONTINUE`); (b) for Record, which stays out of `Button`.
- Evidence: `docs/ks32-hardware-interface.md` "Front panel: transport buttons". From Select Seq/Preset ("Sequence-00") on a cold-booted machine, each of the 85 codes 0x00-0x7F outside `Button` was held 300 ms once: 09 sends MIDI Start (`FA`) and 29 sends MIDI Stop (`FC`), with no display or LED change; no other code changed the display, the LEDs or MIDI out, apart from 0x3C-0x7F, which act like Bank and Screen buttons. No `F8` or `FB` was seen; System/MIDI has no clock-out setting. These are the two codes Plan 6 singled out by instruction count.
- Confidence: High for the bytes sent; Medium for the names (from the bytes and the panel's labels); Record not found.
- Status: settled, Plan 7 Task 3. `// DECISION A14` at `Button` (`src/ks32/panel.h`). Tests: ks32_firmware_transport_codes, panel_button_table.

## A15: Current and requested sound
- Options: how the engine brings a saved sound back after a warm boot (OS v3.01 always powers on to ROM 00): (a) send a bare program change after every boot; (b) send the requested sound only if the machine is not already on it, and only on a request, so a panel or MIDI-in pick is never undone; and which bytes: (c) the program change alone; (d) the bytes the firmware used for the selection, bank select included.
- Choice: (b) and (d). The engine parses MIDI out (drained every block) and MIDI in with a running-status parser and, once the boot has ended (A6), tracks the program (`current_`), the channel it came on and the last bank select (CC 0 and CC 32). `Snapshot::select` is `Bn 00 msb 20 lsb` (only if a bank select was seen since the boot ended) then `Cn pp`; a data byte sent in running status is stored with its status. `setProgram(n)` records a request; at a block start, once booted, a request that differs from the current sound sends `Engine::setSelect`'s bytes if they select that program, else `Cn n` (n = the last channel seen, 0 at first), once, and counts it in `programsSent`. A panel or MIDI-in change only moves `current_`, so a later `setProgram` with the same value sends nothing. The boot sets `current_` to 0.
- Evidence: `docs/ks32-hardware-interface.md` "MIDI out" and "Battery RAM": the data byte alone does nothing after a boot, `C0 pp` brings a ROM sound back, INT 34 needs `B0 00 00 20 00` before the `C0`. Tests: engine_tracks_*, engine_restores_program_after_warm_boot, engine_panel_pick_is_not_undone, engine_restores_task1_kinds (INT 34, and ROM 55 picked after BankSet and back, whose tracked select starts `B0 00 00 20 01`; both come back with the display as before). A preset sends nothing on MIDI out and cannot come back.
- Confidence: High for the bytes; Medium for other channels and the Select Sound pages (not tried, as in Task 1).
- Status: settled, Plan 8 Task 2. `// DECISION A15` at `Engine::track`'s parser group (above `Engine::MidiParse::feed`) and at `Engine::setProgram` (`src/app/engine.cpp`). `setSelect` takes only bank selects and one final `Cn pp` on one channel (it comes from saved state).

## A16: Analogs after the boot
- Options: (a) pass `setAnalog` to the machine at once; (b) hold the wanted raw values and apply them at a block start once the boot has ended.
- Choice: (b), for the inputs the host has set. At each block start, once booted, an input whose wanted value differs from `Machine::analog` is applied. A GUI `Event::Kind::Analog` also records its value, so the block start does not undo it.
- Evidence: the firmware reads the inputs while it boots (A6, K13), and a pitch wheel or volume away from rest during the boot changes what the boot sees. Test: engine_analog_waits_for_boot (`PitchWheel` stays at the rest `0x200` for 1000 ms and is `0x100` after 3300 ms).
- Confidence: High.
- Status: settled, Plan 8 Task 2. `// DECISION A16` at `Engine::setAnalog` (`src/app/engine.cpp`).

## A19: MIDI note mask
- Options: (a) none; (b) a 128-bit mask of the notes held on MIDI in, published in the snapshot, for the editor to draw those keys pressed.
- Choice: (b). Note on (velocity above 0) sets bit n of `Snapshot::notes`, note off (or note on with velocity 0) clears it, on any channel; All Notes Off and All Sound Off (CC 123, 120) clear all. It follows MIDI in only, from the first byte, and `loadRam` clears it.
- Evidence: design section 6.6. Test: engine_note_mask.
- Confidence: High for the rule; a note held on two channels clears on the first off (a limit, not worked around).
- Status: settled, Plan 8 Task 2. `// DECISION A19` at `Engine::track` (`src/app/engine.cpp`).

## A17: Host parameters
- Options: (a) the analogs stay GUI events and nothing is automatable; (b) host parameters (an `AudioProcessorValueTreeState`) for the analogs and the sound, read by the audio thread as atomic raw values; and how a position maps to raw: (c) linear with `lround`, the wheels inverted.
- Choice: (b) and (c). Five parameters: `volume`, `dataEntry`, `pitchWheel`, `modWheel` (floats 0 to 1, defaulting to the position of the K13 rest value) and `sound` (an int 0 to 127, default 0). `analogRaw(a, p) = lround(p x 1023)` (the wheels from `1 - p`), `analogPosition` is its inverse; `processBlock` passes all five to the engine before `render` (`setAnalog`, `setProgram`). The editor's sliders and wheels read and write the parameters, one host gesture per drag; the pitch wheel springs back to 0.5 at mouse-up. The CV pedal, pressure and battery are not parameters.
- Evidence: design sections 6.3 and 6.6. Tests: analog_map_round_trips_rest_values, app_tests `parameters`. The machine accepts a raw value of 0 to 0x3FF, so position 0.5 gives 0x200 (the pitch wheel's rest).
- Confidence: High.
- Status: settled, Plan 8 Task 3. `// DECISION A17` at `analogRaw` (`src/app/analog_map.h`) and `Ks32Processor::layout` (`app/source/Processor.cpp`).

## A18: Processor tick
- Options: (a) keep the 1 Hz timer for the save and poll the sound elsewhere; (b) one 30 Hz timer, `tick()`, that follows the engine's sound into `sound`, with the 60 s save as 1800 ticks; and how the follow avoids fighting the host and the panel, given that the snapshot is at least one block old: (c) follow any change of `Snapshot::program`; (d) follow only a change tracked from the panel or MIDI (`Snapshot::programSeq`), and mark the followed value so the audio thread adopts it without sending.
- Choice: (b) and (d). `programSeq` counts program changes tracked from MIDI out or in, not the engine's own sends (`sending_`), and is not reset by `loadRam`. `tick()` follows when `program >= 0` and `programSeq` moved: it stores the value in `followed_` (seq_cst) first, then sets `sound` (inside a begin/end gesture, unless it already holds that value). `processBlock` reads `sound`, then tries `followed_.compare_exchange_strong(sound, -1)`: on a match it calls `Engine::adoptProgram` (the request is set, `dirty_` is not, so nothing is sent), else `setProgram`. At the start of each tick a marker that no longer equals `sound` (the host overwrote the follow before a block took it) is cleared. A new engine resets the tick's last-seen sequence to 0. `tick()` is public so the checks call it without a message loop.
- What holds: the host case is closed (a stale snapshot never writes `sound` back, since only `programSeq` moves it); a followed value that processBlock sees with its marker is adopted, never sent. What remains, rare: a panel pick and a host change of `sound` inside one tick period can leave the host showing the followed value while the machine plays the host's value, until the next change of either.
- Evidence: design section 6.3. Tests: engine_program_seq_and_adopt; app_tests `follow` on the real ROMs (two panel picks and two MIDI-in picks a block apart send nothing and `sound` ends on the last; the host setting `sound` to 3 then 4 sends exactly twice, ends on ROM 04, and a tick against the stale snapshot leaves `sound` at 4). The panel-case race needs the audio thread to run inside tick's write, so it is argued, not run.
- Confidence: High for the host case; Medium for the panel race (reasoned from the order of the stores and reads).
- Status: settled, Plan 8 Task 3. `// DECISION A18` at `Ks32Processor::tick` and `processBlock` (`app/source/Processor.cpp`) and `Engine::adoptProgram` (`src/app/engine.cpp`).

## A20: State format
- Options: (a) the parameters only, the memory file staying the only memory; (b) `copyXmlToBinary` of one XML element holding the parameters, the selection to replay and the RAM; and what an odd blob does: (c) reset the instance; (d) restore what it can and leave the machine as it is.
- Choice: (b) and (d). `<KS32State version="1" select="c037">`: the parameter tree (`sound` and the four analogs, A17), `select` (the replay bytes in hex, A15: once booted the running selection, before that the one the start image came with) and, in Project mode only (A22), a `<RAM>` child with the 65 536 bytes in base64 (about 87 KB). On restore a `select` that ends `Cn pp` writes `pp` into the restored tree's `sound` before `replaceState` (no extra host edit while a project loads), so a state taken before the 30 Hz follow (A18) caught up still brings back the right sound. In Project mode a version 1 RAM of the right size restarts the machine from it (a warm boot) and hands the engine `select` (`setSelect`); with no ROMs it waits as the start image (A21). A blob that is not a `KS32State` is ignored; no RAM, a wrong size or another version restores the parameters and `select` and leaves the machine as it is.
- Evidence: design section 6.4. Tests: app_tests `stateRoundTrip` (Master Tune +02, ROM 55 by MIDI and Volume 0.5 come back in a new instance, `sound` 0 in the state), `stateBadBlobs` (100 random bytes, a 1000-byte RAM and `version="2"` keep the same engine and render on), `vst3` (a second instance given the first's state reports `sound` 55 through the host's parameters).
- Confidence: High.
- Status: settled, Plan 8 Task 4. `// DECISION A20` at `Ks32Processor::getStateInformation`, `setStateInformation` (`app/source/Processor.cpp`) and the declarations (`Processor.h`).

## A21: Which RAM is saved
- Options: (a) always the running machine's RAM; (b) the running RAM once booted, the image the machine started from before that, and the image a state gave when there is no machine; and how to take it without a gap: (c) read the machine under `suspendProcessing`; (d) ask the audio thread for a copy (`requestRam`/`takeRam`) and wait.
- Choice: (b), and (d) with (c) as the fallback. Every restart keeps its image and selection (`startImage_`, `startSelect_`); a restart that keeps the RAM before the boot ends uses them (not the memory file), and a state that arrives with no ROMs sets them for `setRomFolder` to use. Once booted, the running RAM whatever thread asks. `processBlock` records its thread (one relaxed store into a lock-free `std::atomic<void*>`); on that thread no block runs during the call, so the machine is read directly (no wait, no gap; the 64 KB copy allocates there, the host's choice in calling `getStateInformation` on it). On any other thread (message or background) the caller drops any stale copy, asks for one and waits at most 300 ms in 5 ms steps; if no block ran (so the machine is not running either), it reads the machine under `suspendProcessing`. A host that has stopped calling `processBlock` therefore costs up to 300 ms per Project instance per save. One `std::mutex`, which `processBlock` never takes (`getStateInformation` takes it on every thread, the audio thread included, so there it waits, bounded, behind a concurrent save or restart), guards `restart` (the engine swap), this read and the start image and selection, so a background save cannot race a restart. The 60 s save also uses `requestRam`/`takeRam`, but only in File mode, whose state has no RAM and which refuses `copyToSharedMemory`, so the two never take each other's copy.
- Evidence: design sections 5 (anti-goals: losing a project's memory, a save that makes a gap, chaining short boots) and 6.4. Tests: app_tests `stateDuringBoot` (a state taken 1000 ms into a restored boot holds the restored RAM byte for byte and its `select`), `stateWithoutRoms` (with an empty ROM folder the state comes back unchanged; once the ROMs are good it boots to ROM 55), `stateThreads` (with `processBlock` on its own thread, saves from the message thread, a third thread and the audio thread each hold the running RAM, not the zero start image, and each restores Master Tune +02 set after the boot).
- Confidence: High.
- Status: settled, Plan 8 Task 4. `// DECISION A21` at `Ks32Processor::ram` and `restart` (`app/source/Processor.cpp`), `processBlock` (the thread store), `startImage_`, `stateMu_` and `audioThread_` (`Processor.h`). Known limit: a host that runs one instance's `processBlock` on several threads is seen as its last one; a save on another of them waits for the copy (it does not read directly). The reverse case: a save on a previously recorded audio worker thread while `processBlock` has migrated to another worker reads the RAM directly during a block, so the copy can be torn; it needs a host that migrates `processBlock` between threads and saves on a worker, and is rare.

## A22: Memory per format
- Options: (a) every format shares the memory file as in Plan 7; (b) the Standalone keeps the file, and every plugin instance keeps its memory in the project.
- Choice: (b), the user's rule ("If the instance is new start from the shared memory. If from the project file, load memory states from that project file."). `MemoryMode::File` for `wrapperType_Standalone` (from `PluginHostType::getPluginLoadedAs()`), `MemoryMode::Project` for any other (the checks construct directly: Project). File works as A11. Project: the constructor still starts from the shared file; there is no close save and no 60 s save; Erase restarts this instance cold from 64 KB of zeros and leaves the file alone; Change... picks the shared file and restarts from it, as in File; "Copy to shared memory..." asks first and then writes the A21 RAM to the shared file (temporary file then replace, A11), refused before the boot. The settings page says the instance's memory is in the project.
- Evidence: design section 6.5. Tests: app_tests `projectLeavesSharedFile` (past the 60 s save, Erase and a booted instance's destruction leave the file byte for byte; `copyToSharedMemory` is refused before the boot and then makes the file equal to the RAM), `vst3` (the file is unchanged across two VST3 instances).
- Confidence: High.
- Status: settled, Plan 8 Task 4. `// DECISION A22` at `MemoryMode` (`app/source/Processor.h`), the destructor, `eraseMemory`, `tick` and `copyToSharedMemory` (`Processor.cpp`) and the memory note (`app/source/SettingsPage.cpp`).

## A23: Standalone state
- Options: (a) the Standalone keeps no plugin state; (b) JUCE's wrapper keeps the processor state, which carries the parameters and `select` but not the RAM.
- Choice: (b). JUCE's `StandalonePluginHolder` (`juce_StandaloneFilterWindow.h`) calls `getStateInformation` when the window closes or the system asks to quit (`savePluginState`, audio still running) and stores it base64 as `filterState` in `%APPDATA%\KS-32\KS-32.settings` (its own `PropertiesFile`, not moved by `KS32_SETTINGS_DIR`); at start it calls `setStateInformation` (`reloadPluginState`) after opening the audio device and before `startPlaying`, so before the first block. The processor is made by `createPluginFilterOfType(wrapperType_Standalone)`, which sets the wrapper type before the constructor runs. A File-mode state has no `<RAM>`, so the memory file stays the Standalone's only memory, and its `select` reaches the not yet booted engine (`setSelect`).
- Evidence: design section 6.5; the JUCE source above. Tests: app_tests `standaloneNoRam` (a File processor's state has no `<RAM>`).
- Confidence: High.
- Status: settled, Plan 8 Task 4. `// DECISION A23` at `Ks32Processor::getStateInformation` and `setStateInformation` (`app/source/Processor.cpp`); the reopen check (ROM 55 and Volume come back, Master Tune from the file) is a user row in the verification doc.
