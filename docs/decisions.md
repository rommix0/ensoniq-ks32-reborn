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
- Status: Implemented in ens::esp::Agen (Task 7; R13 in Task 9 fix round). Tests: agen_memsiz_64k, agen_delay_no_wrap, agen_delay_wraps_circular, agen_delay_top_of_buffer_with_fraction_does_not_wrap, agen_delay_dlength_all_ones_stays_in_range, agen_dbase_decrements_and_reloads, agen_period_matches_wrap, agen_period_odd_dlength, agen_table_not_masked_by_dlength, agen_io_absolute, agen_reg_alignment.

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
- Options: (a) one `Esp::runSample()` per OTIS frame runs the program to END with no instruction budget; halted time advances only through explicit `idleStep()` calls; (b) a slots-per-sample budget (about one instruction per 125 ns at 8 MHz) with an advance API that interleaves running and halted time.
- Choice: (a). This equals hardware only when the program length is at most the ESP's slots per frame (KS-32: 89 words = 89 slots at 8 MHz, `docs/rom-investigation/findings.md` section B). While halted nothing advances (host requests, RAM clear, refresh) unless the caller calls `idleStep()`, about once per 300 ns (one instruction period), see `src/esp/esp.h`. A slots-per-sample budget and advance API is deferred to the start of Plan 3 (ruling R17).
- Evidence: findings.md section B (89-word program, 89 slots per frame at 8 MHz); decisions E11, E28; ESP notes part 2 section 5.
- Confidence: Medium.
- Status: Implemented in Esp::runSample and Esp::idleStep, `// DECISION E30` markers in `src/esp/esp.h` and `src/esp/esp.cpp`. Tests: those named under E11 and E28.

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
