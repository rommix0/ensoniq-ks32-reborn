# ENSONIQ ESP (ES5510) Specification Rev. 2.4 — Transcription Part 2 (scan pages esp_29 .. esp_56)

Source: scanned "ENSONIQ ESP Specification Rev. 2.4". Page mapping: scan page `esp_NN.png` = document page `NN - 4`
(e.g. esp_29 = doc p.25, esp_54 = doc p.50). esp_55 and esp_56 are NOT p.51/52: they are superseded Rev 2.1 copies of pp.49-50 (with handwritten notes).

Conventions: `[illegible]` = could not read; `[inferred]` = my interpretation, not printed; `[note]` = transcriber remark.
Text is transcribed close to verbatim; tables are reproduced exactly as printed.

## Table of contents (from esp_02..esp_04, for orientation; doc page numbers)

```
1. Introduction ........................................ 1
2. General Description ................................. 1
3. Chip Architecture ................................... 3
   3.1 Instruction and GPR Memory Arrays ............... 3
   3.2 Special Purpose Registers ....................... 4
   3.3 The ALU ......................................... 6
       3.3.1 ALU Operations ............................ 6
       3.3.2 Condition Code Register ................... 9
       3.3.3 Setting Condition Codes (Pipeline Considerations) .. 10
   3.4 Multiplier ...................................... 11
   3.5 Address Generator ............................... 12
       3.5.1 Address Generator Architecture ............ 12
       3.5.2 Selecting the Addresssing range ........... 13
       3.5.3 RAM Addressing Modes ...................... 13
             Delay Line Addressing ..................... 13
             Table Addressing .......................... 14
             External I/O Space Addressing ............. 14
       3.5.4 Physical Address Computation .............. 14
             Delay Line Addressing Mode ................ 14
             Table addressing Mode ..................... 15
             I/O addressing Mode ....................... 15
   3.6 Busses .......................................... 15
       3.6.1 Internal Operand Busses ................... 15
       3.6.2 External RAM Busses ....................... 16
             RAM Data/Address Bus ...................... 16
             RAM Bus Control Lines ..................... 17
             Memory vs. I/O Space ...................... 17
       3.6.3 RAM Data Interface ........................ 17
             Data Input Latch (DIL) .................... 17
             Data Output Latch (DOL) and FIFO .......... 17
4. Operation ........................................... 18
   4.1 Microinstruction Word ........................... 18
             Microinstruction Word Format .............. 18
       4.1.1 Operand Fields ............................ 19
       4.1.2 ALU Opcode Field .......................... 19
             ALU Operation Mnemonics and Opcodes ....... 19
       4.1.3 Operand Select Modes ...................... 19
             ALU Operand/Bus Mapping ................... 19
             Multiplier Operand/Bus Mapping ............ 20
             Operand Select Field ...................... 21
       4.1.4 'No output' Multiplier Destinations ....... 21
       4.1.5 Skip and MAC Bits ......................... 22
       4.1.6 RAM Control Field ......................... 22
             RAM Control Field Encoding ................ 22
   4.2 Conditional Execution ........................... 23
       4.2.1 Basic Conditional Mechanism ............... 23
       4.2.2 Arithmetic Condition Masking .............. 23
             Arithmetic Condition Code Masks ........... 25
   4.3 Microinstruction Execution Cycle ................ 25
       4.3.1 Basic Execution Cycle ..................... 25
       4.3.2 ALU/Multiplier Timing Offset .............. 26
       4.3.3 Access to MACH and MACL ................... 27
             Preload and Postfetch ..................... 27
             MACH and MACL Volatility .................. 27
             MACL and MACH as Multiplier Operands ...... 28
       4.3.4 RAM Pipelining ............................ 28
             READ vs. WRITE Timing ..................... 28
             Write Data Latency ........................ 28
             RAM Address Assembly ...................... 29
5. Host Interfacing .................................... 31
   5.1 Host Interface Registers ........................ 31
       5.1.1 Host Interface Memory Map ................. 31
       5.1.2 GPR/Instruction Access .................... 32
             Host Access OK/ ........................... 32
       5.1.3 RAM Registers ............................. 32
             RAM Access Control ........................ 33
             Reading RAM Data .......................... 33
             Writing RAM Data .......................... 33
       5.1.4 Host Control Register ..................... 34
             Host Access OK/ ........................... 34
             RAM Clear ................................. 34
             Refresh Disable ........................... 34
       5.1.5 PC, Internal Refresh Counter, and Frame Counter .. 34
             The Program Counter ....................... 35
             The Refresh Counter ....................... 35
       5.1.6 Halt Enable Register ...................... 35
       5.1.7 Host Serial Control Register .............. 35
   5.2 Host Interaction and the END Statement .......... 35
       5.2.1 ESP End Statement ......................... 38
   5.3 Host Interface Pins ............................. 38
       5.3.1 HALT Pin .................................. 38
             Sample Rate Sync .......................... 38
             Single-Sample Execution ................... 39
       5.3.2 Host Bus .................................. 39
       5.3.3 Address Strobe ............................ 40
       5.3.4 Chip Select ............................... 40
   5.4 Host Initialization ............................. 40
       5.4.1 Halt State ................................ 40
             Enabling RAM Refresh ...................... 41
             RAM Clearing .............................. 41
             Host RAM Initialization ................... 42
       5.4.2 Power-up Sequence ......................... 42
       5.4.3 Program Load Sequence ..................... 42
6. ESP PIN DESCRIPTION ................................. 43
   6.1 Pinout .......................................... 43
   6.2 Pin Descriptions ................................ 44
       6.2.1 Host Interface ............................ 44
       6.2.2 External RAM .............................. 45
       6.2.3 Serial Interface .......................... 45
       6.2.4 Supplies (clock and power) ................ 46
       6.2.5 System Interface Hardware Block Diagram ... 46
7. Timing Diagrams ..................................... 47

List of Figures (page numbers as printed in TOC; note these appear to use a different/earlier numbering than the body)
ALU Functions ......................................... 11
ALU Operation Mnemonics and Opcodes ................... 23
ALU to Internal Bus Mapping ........................... 24
Arithmetic Condition Code Masks ....................... 29
Condition Code Register Bits .......................... 13
Conditon Code Computation ............................. 14
DRAM Timing ........................................... 34
ESP Block Diagram ..................................... 8
GPR bits to DRAM mapping .............................. 20
Host Control Register ................................. 38
Host Interface Memory Map ............................. 35
Host Serial Control Register .......................... 40
Internal Event Timing for the ESP ..................... 30
Microinstruction Word Format .......................... 22
Multiplier Bus Mapping ................................ 24
Negative Flag Test Conditions ......................... 28
Operand Select Field Encoding ......................... 25
Positive Flag Test Conditions ......................... 28
RAM Access Control Register ........................... 37
RAM Control Field Encoding ............................ 26
Special Purpose Registers ............................. 9
System Interface Hardware Block Diagram ............... 46
```
[note] The List-of-Figures page numbers equal (body page + 4), i.e. they match the scan file numbers `esp_NN`, not the printed body page numbers.

---

## esp_29 (doc p.25) — end of 4.2.2, start of 4.3

### Arithmetic Condition Code Masks (table, continued from 4.2.2 on previous page)

Columns N, C, V, LT, Z, NOT are the mask bits; Mask(hex) is the resulting byte value.

| Condition       | Mnemonic | N | C | V | LT | Z | NOT | Mask(hex) |
|-----------------|----------|---|---|---|----|---|-----|-----------|
| Equal           | EQ       | 0 | 0 | 0 | 0  | 1 | 1   | 0C        |
| Not Equal       | NEQ      | 0 | 0 | 0 | 0  | 1 | 0   | 08        |
| Negative        | NEG      | 1 | 0 | 0 | 0  | 0 | 1   | 84        |
| Positive        | POS      | 1 | 0 | 0 | 0  | 0 | 0   | 80        |
| Overflow set    | OV       | 0 | 0 | 1 | 0  | 0 | 1   | 24        |
| Overflow clear  | OVC      | 0 | 0 | 1 | 0  | 0 | 0   | 20        |
| Lower than      | LO       | 0 | 1 | 0 | 0  | 0 | 1   | 44        |
| Higher or same  | HS       | 0 | 1 | 0 | 0  | 0 | 0   | 40        |
| Lower or same   | LS       | 0 | 1 | 0 | 0  | 1 | 1   | 4C        |
| Higher than     | HI       | 0 | 1 | 0 | 0  | 1 | 0   | 48        |
| Less than       | LT       | 0 | 0 | 0 | 1  | 0 | 1   | 14        |
| Greater or equal| GTE      | 0 | 0 | 0 | 1  | 0 | 0   | 10        |
| Less or equal   | LTE      | 0 | 0 | 0 | 1  | 1 | 1   | 1C        |
| Greater than    | GT       | 0 | 0 | 0 | 1  | 1 | 0   | 18        |
| Always          | TRUE     | 0 | 0 | 0 | 0  | 0 | 1   | 04        |
| Never           | FALSE    | 0 | 0 | 0 | 0  | 0 | 0   | 00        |

[inferred] From the hex values, the mask byte bit positions are: N = bit 7, C = bit 6, V = bit 5, LT = bit 4, Z = bit 3, NOT = bit 2, bits 1..0 = 0.
E.g. NEG = N|NOT = 0x80|0x04 = 0x84; LS = C|Z|NOT = 0x40|0x08|0x04 = 0x4C. (Semantics of how the mask is applied — i.e. OR of selected flags, inverted by NOT — is described in 4.2.1/4.2.2 on pages handled by the other agent. [inferred] "NOT"=1 gives the "set/true" sense: e.g. TRUE has only NOT set, FALSE none; EQ = Z & NOT, NEQ = Z without NOT. So condition = (OR of masked flags) XNOR? — specifically, with NOT=1 the condition is true when any selected flag is set (TRUE: no flags selected → treat as true); with NOT=0 condition is the inverse. Exact formula must be taken from 4.2.2.)

### 4.3 Microinstruction Execution Cycle

#### 4.3.1 Basic execution cycle

"The basic major execution cycle lasts for three input clock cycles, or T clocks. Each T cycle corresponds to a period of the external clock applied to the ESP. A T/2 clock is generated from the T clock for internal sequencing, and most internal events can be seen falling on these half-cycle divisions. A detailed diagram of the internal timing is shown below."

---

## esp_30 (doc p.26)

### Internal Event Timing for the ESP (figure — described)

Figure shows CLK with one microinstruction = T0, T1, T2 (three CLK periods). Rows (as labelled in the drawing; relative placement read from the drawing):

- **Multiplier N-1** … then **Multiplier N** (multiplier cycle spans ~ one microcycle, starting earlier than the ALU cycle).
- **ALU N-1** … then **ALU N** (ALU cycle starts later — lags multiplier by one T).
- Instruction fetch: "Read Inst N-1", "Decode Inst N-1" … "Read Inst N", "Decode Inst N" (read at start of T0 region, decode next half-cycle).
- Bus phases (half-T slots) in order: "Write Mul N-2", "Read Mul Op N-1", "Write ALU N-2", "Read ALU Op N-1" … then "Write Mul N-1", "Read Mul Op N", "Write ALU N-1", "Read ALU Op N".
- "DOL Valid" pulse, then "DIL Valid" pulse (each once per microcycle; DOL Valid precedes DIL Valid).
- "RAM Address Gen N-1" … "RAM Address Gen N".
- "Read Offset N-1" … "Read Offset N".
- "RAM R/W Cycle Offset @ N-2" … "RAM R/W Cycle Offset @ N-1".

[note] Exact edge alignments in the figure are approximate; the order of the bus slots within a microcycle is: Write Mul(prev-prev) → Read Mul Op(prev) → Write ALU(prev-prev) → Read ALU Op(prev), relative to the instruction being read/decoded at that time.

#### 4.3.2 ALU/Multiplier Timing Offset

"The timing diagram shows that the ALU, Multiplier and RAM Address Generator all have somewhat overlapping operating cycles. That their cycles are not completely coincident has important ramifications from a programming point of view. In particular, it can be seen that ALU operations lag Multiplier operations by one T cycle, 100ns. This lag is necessary because the Multiplier and ALU trade off their use of the X, Y and Z busses. The result is that the output of an ALU operation at any given microinstruction cycle is not available for the Multiplier to fetch as an operand during the next microinstruction cycle. Thus the programmer must leave one step between the generation of a value in the ALU and its use in the Multiplier.

Note however that because the multiplier writes its result before the ALU fetches its operands, Multiplier results can be used in the ALU on the very next step - no pipelining needs to be accounted for from a programming point of view."

**Emulator summary:** T = 100 ns (10 MHz clock implied) [inferred from "one T cycle, 100ns"]; microinstruction = 3T = 300 ns.
- ALU result of step N → NOT visible to multiplier operand fetch at step N+1 (multiplier sees the old value); visible at N+2.
- Multiplier result of step N → visible to ALU operand fetch at step N+1.

---

## esp_31 (doc p.27)

#### 4.3.3 Access to MACH and MACL

"The Multiplier's 48-bit wide accumulator is also Special Purpose Register (SPR) pair MACH and MACL. ALU operations can use either of these two registers as source or destination operands. There are some non-obvious aspects to their successful use, however, due to pipelining considerations and to the fact that they are really part of the Multiplier and not independent registers. When the ALU reads MACH or MACL, the contents will be appropriately and conditionally saturated depending on the accumulators contents at the time of the read."

**Preload and Postfetch**

"The instruction cycle timing diagram shows for the same microinstruction step N that Multiplier operations lead ALU operations by one T clock period. This means that ALU operations that preload either MAC half must be performed on the step *prior* to the one where the accumulation is to take place.

Note that ALU cycle N-1 completes sometime during the middle of Multiplier cycle N. It is at this point that the ALU result is loaded into the Multiplier to be accumulated at the end of cycle N.

Multiplied or accumulated results can be fetched from MACH or MACL by the ALU on the microinstruction step following the Multiplier operation. Note that fetching MACL with the ALU is the *only* way to gain access to the lower-order 24-bits of Multiplier results, since this half of the MAC is not normally enabled onto the Z bus at the end of a Multiplier operation."

**MACH and MACL Volatility**

"Unlike standard GPR's, MACH and MACL are volatile. This means that they are recycled with fresh data every Multiplier cycle, regardless of whether a multiply or an accumulate took place. To preserve the contents of the accumulator between instruction steps not using the multiplier, the operation MAC = MAC + 0 x 0 is used. This is equivalent to a multiply/accumulator no-operation (NOP). This is also necessary when operands must be preloaded more than one cycle ahead, or results need to be retained in the accumulator past the current instruction cycle. This is a necessity, too, if it is desired to preload all 48 bits of the MAC, an operation requiring 2 ALU cycles.

When data is preloaded into the MAC, it is normally meant to be used as part of an accumulation during the next Multiplier cycle. If the next cycle specifies a *multiply* rather than a multiply/accumulate, however, that data will be lost. Instead, the accumulator will retain the result of the multiplication, as if no data had been preloaded. Thus the accumulator always receives the result of the previous cycle's operation.

The ESP assembler interprets the lack of any specified multiplication as a NOP for the Multiplier, and assembles the microinstruction as a multiply/accumulate of 0 times 0. This any data already in the accumulator will be retained across microinstruction cycles when no Multiplier operation is specifically stated. the programmer can explicitly include "0 X+ 0 > 0" to achieve a NOP."

**MACL and MACH as Multiplier Operands**

"MACH and MACL can *not* be used as Multiplier source operands. The Multiplier performs its operand fetch 1/2 T cycle before the ALU writes the result of its operation at" (continued)

---

## esp_32 (doc p.28)

"the previous step. The destination of the previous multiply can be used, however, as a multiplier source operand.

Specifying MACL as a destination operand will cause the upper half of the accumulator to be duplicated in the lower half. The result of specifying MACH as a destination operand is undefined."

**Emulator summary of MAC rules:**
- MAC = 48 bits = MACH (upper 24) : MACL (lower 24).
- Every microcycle the MAC is overwritten with the multiplier result (MUL: `MAC = X*Y`; MAC: `MAC = MAC + X*Y`). Assembler default when no multiply is coded: `MAC = MAC + 0*0`.
- ALU write to MACH/MACL at step N-1 preloads the accumulator used by the multiply-accumulate at step N (only if step N is an accumulate; a plain multiply discards the preload).
- ALU read of MACH/MACL returns a (conditionally) saturated value based on the full accumulator.
- ALU writing MACL as destination: "upper half of the accumulator to be duplicated in the lower half" [as printed]. Writing MACH as destination: undefined. [note] This seems to concern *multiplier* destination selection; ambiguous wording.
- MACH/MACL cannot be multiplier source operands.

#### 4.3.4 RAM Pipelining

"One ESP access to RAM is guaranteed to be available each microinstruction cycle. In fact, the basic microinstruction period is tied to the relatively slow speed with which RAM accesses can occur. Because of slow DRAM access speed and the fact that the ESP takes a complete microinstruction cycle to calculate a complete physical RAM address, raw address offsets and RAM control fields must be supplied to the Address Generator considerably ahead of the time at which data associated with the address is needed or is ready. The RAM pipeline length, or the number of instruction cycles needed ahead of time, is 2. Looking at the RAM timing diagram in section 4.3.5, two complete T cycles can be seen to elapse between the fetch of the raw address offset for step N (OFFSET N) and the appearance of data corresponding to the resulting physical address on the multiplexed address/data bus (DATA N).

During the first cycle, a microinstruction and an offset are fetched and the physical RAM address is computed by the Address Generator (RAM ADDRESS GEN N) according to the instruction's RAM control field. This computation may involve an add, a comparison, and possibly a swap. (Section 3.4 gives more details on the operation of the Address Generator.)

During the second cycle, the row and column parts of the RAM address appear on the ESP's multiplexed data/address pins, suitably gated with the RAS, CAS, GATE and R/W signals. If RAM is accessed for a read operation, the data will be available to read during the latter half of this cycle and will be latched into the DIL by signals derived from the RAM control field. It will be available to be fetched as an ALU or Multiplier operand at the beginning of the next microinstruction cycle. If the RAM access is a write, the data output of a past ALU or Multiplier operation must be available from DOL FIFO to be transferred to the ESP data/address pins."

[note] Text says "two complete T cycles" but context (and "RAM pipeline length ... is 2") means two microinstruction cycles. Section reference "4.3.5" (RAM timing diagram) — the DRAM Timing figure is on esp_34; there is no numbered 4.3.5 heading in TOC. "Section 3.4" reference for the Address Generator — TOC places it at 3.5.

**READ vs. WRITE Timing**

"An inconsistency in the pipelining scheme is caused by the fact that data being read from RAM needs to be available before the beginning of the cycle on which it is used, while data being written to RAM are not available until the very end of that cycle. That the execution of the Multiplier and ALU are skewed with respect to each other spreads this time window out even further, to four T cycles. The result is that addresses associated with reads at a particular step must be available earlier than addresses corresponding to write operations from that step.

In particular, read addresses must be pipelined back 2 microinstructions prior to the instruction cycle that they are needed on. This is dictated by the basic pipeline length of the ESP. Write addresses, on the other hand, are pipelined back zero cycles. Write data will not be ready until almost 2 microinstruction cycles have elapsed since the write operation's instruction was fetched. The 2 cycle pipeline length combined with the nearly-" (continued)

---

## esp_33 (doc p.29)

"2-cycle-late appearance of write data means that addresses for write data can be assembled at the same step as the instructions that write the data."

**Write Data Latency**

"The different pipeline amounts for read and write addresses creates a situation of potential conflict between reads at one step and writes 2 steps later. Both operations want to see their RAM addresses at the same microinstruction address, even though they are occurring on different steps. This conflict is ameliorated to a great degree by the latency of data written to the DOL and the fact that the DOL contains a 2 stage FIFO.

The actual operation of the DOL FIFO is covered in greater detail in section 3.5.3, but its purpose is to store pending write data until such time as no read operation is in progress. Because of the FIFO, up to 2 data words can be buffered and waiting for a free RAM cycle. When an available cycle arrives, the write address for the (first) waiting data word will have been computed by the Address Generator, and the output of the FIFO can be output to the data/address pins."

**RAM Address Assembly**

"It is the responsibility of an ESP assembler to simulate the scheduling of the RAM interface and DOL FIFO to determine where to assemble address offsets. Write addresses must be positioned so that they match the write data that will be pending in the DOL FIFO at runtime. The assembler also must warn the programmer if read/write conflicts will occur, that is, if insufficient free RAM cycles to handle the number of specified write operations will cause the DOL FIFO to overflow. The scheduling is somewhat intricate and is complicated by the facts that A) the ESP sometimes "throws out" unwanted data in the DOL FIFO and B) data read to the DIL is latent through intervening write operations.

The RAM Control field of the microinstruction must be pipelined along with the address it corresponds to. The 3 bits in the field are needed to control the ESP's RAM interface at the time the address and data are written to or read from external memory, not at the time the RAM data is output or fetched by the ALU or Multiplier. In fact, the control bits must travel through a 1 microcycle delay pipeline register so that they remain synchronized with the address data as it passes through the 1 cycle delay caused by the Address Generator."

**Emulator summary of RAM pipeline [inferred from text]:**
- Instruction at step N carries an address offset + 3-bit RAM control field.
- Step N: Address Generator computes the physical address (control bits delayed 1 cycle alongside it).
- Step N+1: DRAM cycle performed (row/col, RAS/CAS). For a read: data latched into DIL late in N+1, usable as an operand by the instruction at step N+2.
- Write: data comes from the DOL FIFO (2 entries). The data written is whatever ALU/multiplier output was previously loaded into the DOL; writes happen on cycles where the FIFO has pending data and the RAM cycle is a write.
- DIL keeps its value through intervening write cycles ("data read to the DIL is latent through intervening write operations").
- The "throws out unwanted data in the DOL FIFO" behaviour is referenced but not detailed here (see 3.5.3/3.6.3 in part 1).

---

## esp_34 (doc p.30)

### DRAM Timing (figure — described)

Top scale: `T1 | T2 | T0 | T1 | T2` (one microcycle = T0,T1,T2).

Rows:
- "Read Offset N" (pulse at start of T1 of first shown cycle), "Read Offset N+1" (at T0 of next cycle).
- "RAM Address Gen N-1" … "RAM Address Gen N".
- "RAS N-1" … "RAS N" (RAS asserted region labeled).
- "CAS N-1" with a dashed extension labelled "Write" (CAS stretched on write) … second label also printed "CAS N-1" [note: second label is probably a typo for "CAS N"].
- "GATE" pulse in each cycle.
- Address/data bus: `ROW | COL | DATA N-1` … `ROW | COL | DATA N`.
- "R/W N-1" … "R/W N".

Caption text below figure:
"CAS does not fall when I/O SEL is active. I/O SEL timing is the same as CAS but is not stretched on a write cycle."

---

## esp_35 (doc p.31)

# 5. Host Interfacing

### 5.1 Host Interface Registers

"The ESP/Host interface allows the ESP to be addressed as a peripheral to a Host microprocessor. The interface is comprised of 8 pins which carry the multiplexed data and address of the desired interface register, plus R/W, chip select and address strobe pins.

The 8 address lines map the ESP into a 256 byte block of the Host's address space. This block contains registers which allow the Host to read and write to all of the memory internal to the ESP as well as into the I/O and external RAM memory spaces accessed via the ESP's external data bus."

#### 5.1.1 Host Interface Memory Map

| Add. (hex) | Register Name | Bits | Description / Function |
|---|---|---|---|
| 00 | GPR | 23-16 | GPR / SPR Data MS-byte |
| 01 | GPR | 15-8 | GPR / SPR Data |
| 02 | GPR | 7-0 | GPR / SPR Data LS-byte |
| 03 | INSTR | 47-40 | Mult D Operand Address |
| 04 | INSTR | 39-32 | Mult C Operand Address |
| 05 | INSTR | 31-24 | ALU B Operand Address |
| 06 | INSTR | 23-16 | ALU A Operand Address |
| 07 | INSTR | 15-8 | ALU Opcode / Operand Select |
| 08 | INSTR | 7-3 | Skip Flag + MAC + RAM Control |
| 09 | DIL | 23-16 | DRAM Input Latch MS-byte |
| 0A | DIL | 15-8 | DRAM Input Latch |
| 0B | DIL (always read as 00) | 7-0 | DRAM Input Latch LS-byte |
| 0C | DOL | 23-16 | DRAM Output Latch MS-byte |
| 0D | DOL | 15-8 | DRAM Output Latch |
| 0E | DOL (always read as FF) | 7-0 | DRAM Output Latch LS-byte |
| 0F | DADR (write last to transfer data) | 23-16 | DRAM Address MS-byte |
| 10 | DADR | 15-8 | DRAM Address |
| 11 | DADR | 7-4 | DRAM Address LS-byte |
| 12 | Host Control | 2-0 | ESP State / Control Flags |
| 14 | RAM Access Control | 7-6 | RAM,I/O, Read/Write Select |
| 16 | Program Counter | | For Test Purposes Only |
| 17 | Internal Refresh counter | | For Test Purposes Only |
| 18 | Host serial control | 7-0 | Serial I/O Format and Control |
| 1F | Halt enable (write only) / Frame Counter (read only) | | Stop ESP Execution / For Test Purposes Only |
| 80 | Read Select: GPR + INSTR | 7-0 | Read Address |
| A0 | Write Select: GPR | 7-0 | Write Address |
| C0 | Write Select: INSTR | 7-0 | Write Address |
| E0 | Write Select: GPR+INSTR | 7-0 | Write Address |

Side box next to the 80/A0/C0/E0 rows — "Address Ranges":
- GPR  $00 - $BF
- SPR  $EA - $FF
- INSTR $00 - $9F

**Instruction word bit layout implied by the INSTR byte registers (48-bit microinstruction):**
- bits 47-40: Mult D operand address
- bits 39-32: Mult C operand address
- bits 31-24: ALU B operand address
- bits 23-16: ALU A operand address
- bits 15-8 : ALU opcode / operand select
- bits 7-3  : Skip flag + MAC + RAM control (5 bits; [inferred] bit7 = Skip, bit6 = MAC, bits 5-3 = RAM control (3 bits, consistent with "The 3 bits in the field" in 4.3.4); bits 2-0 unused). Also 5.1.2 says "Force HI the upper bit of the INSTR byte interface register ($08)" to set the Skip bit → Skip = bit 7 of reg $08 = instruction bit 7.
- [note] Register $08 is listed as bits 7-3 of the instruction, i.e. the low byte of the 48-bit word; the instruction memory therefore holds 160 ($00-$9F) 48-bit words; GPR range $00-$BF = 192 GPRs; SPRs at $EA-$FF.

#### 5.1.2 GPR/Instruction Access

"The internal data busses of the ESP are constantly being accessed when microprograms are executing. Thus one microinstruction cycle per sample period is" (continued)

---

## esp_36 (doc p.32)

"dedicated to Host/ESP transfers. This time, during the execution of the END statement, is the only time during program execution a transfer may take place between the Host interface registers and the internal GPR and instruction memory (see 5.2). The transfer takes place in lieu of the fetch of A and B ALU operands. Since only one transfer is allowed per sample period, a protocol exists which should be invoked to read or write internal data."

**Host Access OK/**

"When accessing internal GPRs or microinstructions, the Host must always check that the Host Access OK/ bit in the Host Control register is low or set to 0. Reading or writing data or addresses into the GPR or Instruction registers can upset data transfers that are already in progress if the Host OK bit is set.

To read data, the Host must check Host Access OK/ and then request the data by writing a GPR number in the GPR READ register. The OK bit will be set and a copy of the register and instruction field data stored at that address will be placed in the Host access registers at the execution of the next END statement. When this data is valid, the Host Access OK/ bit will go low again, and the data can be read by the Host.

To write data, the Host must check Host Access OK/, and then place the data in the GPR and/or instruction field. Then the address of the GPR or instruction is written into one of three different write registers, depending on whether just GPR data, INSTR word data, or both get transferred. Writing the destination address at one of these locations causes Host Access OK/ to be set, and tells the ESP to transfer the new data to the internal memory on the next transfer slot (END statement). The OK bit will go low again when the ESP has completed the data transfer to the internal memory, a maximum of one sample period later.

This method of transferring data to the ESP is meant to be used as a way of modifying the parameters of a program in real time, without actually downloading a whole new algorithm. Examples of this are real time changes in reverb mix or chorus frequency. When downloading an entire new program, the CPU will hold the HALT pin high and stop ESP execution. This will allow higher transfer throughput to download the new algorithm quickly.

Note that the GPR and instruction fields and the address select registers will retain their last contents after a write has been completed. Thus the following technique could be used to, for instance, update only the Skip bit of a microinstruction:

1. The Host reads the microinstruction by writing the instruction address to Read Select at interface register $80, then checks the Host Control register for Host Access OK/.
2. Force HI the upper bit of the INSTR byte interface register ($08).
3. Copy the address back out of Read Select into INSTR Write Select ($C0) to initiate the write-back of the altered microinstruction."

**Emulator summary of host GPR/INSTR protocol:**
- Write $80 = address → read request; both GPR[addr] (24-bit, into $00-$02) and INSTR[addr] (48-bit, into $03-$08) are captured at the next END.
- Write $A0 = addr → write $00-$02 into GPR[addr]; $C0 = addr → write $03-$08 into INSTR[addr]; $E0 = addr → both.
- Host Access OK/ (Host Control register) = 1 while a transfer is pending; cleared to 0 when done (at END, or, when halted, see later pages).
- Transfer slot = END instruction, replaces ALU A/B operand fetch in that cycle.
- Registers $80/$A0/$C0/$E0 are readable (step 3 "Copy the address back out of Read Select") [inferred].

#### 5.1.3 RAM Registers

"This group of registers comprises the 3 byte wide DOL, 3 byte wide DIL, the 3 byte DADR RAM address registers, and a RAM Access Control register."

---

## esp_37 (doc p.33)

"With these registers, the Host may gain direct access to the RAM memory or I/O space for the purposes of preloading lookup tables, accessing sampled data, performing memory tests, and the like. To access RAM directly through these means, the ESP must be in its Halt state, caused by holding the HALT pin high. At other times, while the ESP is running, Host access to RAM memory must be provided for explicitly by dedicated microprogram instructions that read from memory into a designated GPR. (See section 5.4.1 for more details on the interaction between the Halt state, RAM Clear procedure and direct RAM access.)

The DIL and DOL Host registers are connected directly to RAM data through the multiplexed RAM address/data bus. During HALT, the FIFO pointers are reset, and the host interface can access the DOL directly. Space for DIL and DOL LO registers was reserved in the host memory map, but since the DIL and the DOL are both 16 bits wide, left justified, the low bytes will read back as zero.

RAM Addresses placed in the DADR registers bypass the ESP's Address Generator entirely, and become the physical RAM address which is delivered to the external memory devices. Addresses are always left-justified in the DADR registers. The number of address bits which are active is determined by the amount of RAM actually connected to the ESP. In an ESP configuration with 64K of RAM, for instance, the value placed in the DADR LO Host register is irrelevant for the purposes of addressing RAM. In any case, the most significant address register, DADR HI, will always be active because of the left justification of RAM addresses."

[note] Memory map says DIL LS-byte reads 00 and DOL LS-byte reads FF; text says "the low bytes will read back as zero" — a discrepancy in the original (DOL low byte: FF per table vs 0 per text).

**Key facts:** DIL and DOL are 16 bits wide, left-justified in 24-bit words (bits 23-8). DADR is left-justified; reg $11 holds bits 7-4 → DADR spans bits 23-4 = 20 bits [inferred] (max 1M words).

**RAM Access Control**

"The RAM Access Control register is used to specify whether read or write operations are to be performed, and whether access should be made to RAM memory or the ESP's I/O address space. Its active bits are as shown below."

**RAM Access Control Register** (host reg $14)

| Bit | 7 | 6 | 5 4 3 2 1 0 |
|---|---|---|---|
| Function | Read Select = 1 / Write Select = 0 | RAM Select = 1 / I/O Select = 0 | 1 1 1 1 1 1 |

"Once the access mode is set by writing into this register from the Host, write operations to DADR HI are used to actually initiate the transfers of data to or from memory. **Note:** The ESP should be in HALT mode when accessing this register."

**Reading RAM data**

"To read data, the RAM Access register is initialized for reads and the one or two least significant bytes of RAM address bytes are written to the DADR MID and (optionally) DADR LO Host registers. Finally, the most significant address byte is written to DADR HI. RAM data will become valid in the DIL Host register no later than 6 clock cycles afterwards."

---

## esp_38 (doc p.34)

**Writing RAM data**

"To write data, the RAM Access register should be initialized for writes, after which the data to be written can be placed in the DOL Host registers. Then the least significant parts of the address should be placed in the DADR MID and LO registers, as above. Finally, the most significant byte is written to the DADR HI register to initiate the transfer. The write operation can be assumed to have been completed after a maximum of 6 clock cycles."

**Host RAM access summary:** set $14 (bit7 R=1/W=0, bit6 RAM=1/IO=0, bits5-0 shown as 1s); [write] fill DOL $0C/$0D; write DADR MID $10, LO $11 (optional); write DADR HI $0F last -> triggers the cycle; read result in DIL $09/$0A within 6 clocks. Only valid while HALTed.

#### 5.1.4 Host Control Register

"The Host Control Register is a byte-wide register with control and status bits whose functions are shown in the table below."

**Host Control Register** (host reg $12)

| Bit | 7 6 5 4 3 | 2 | 1 | 0 |
|---|---|---|---|---|
| Function | N.A. | Host Access OK/ (read only, low active) | RAM Clear | Refresh Disable |

"The Host Signal Control register is concerned with Host access to the ESP's internal memory and has bits for controlling RAM clearing and RAM refresh disabling. Refresh Disable must be 0 when activating RAM Clear. Bits 1 and 2 are self-clearing. Bits 0 and 1 are useful only during Halt Mode."

**Host Access OK/**

"This read only bit is used by the ESP to indicate when the information in the GPR and Instruction Host Interface Registers is valid. It also indicates that the ESP is currently busy completing a read or write operation. The Host should not disturb the ESP by writing data or addresses into its GPR/INSTR interface registers when Host Access OK is true. Note that host interfaced registers not pertaining to GPR and INSTR access can be read or written at any time without regard to Host Access OK/.

More details on Host access to the GPR and Instruction memory can be found in section 5.1.2."

[note] Polarity: the bit is "low active" (OK/): 1 = busy / transfer pending, 0 = data valid / OK to access.

**RAM Clear**

"When the ESP is in its Halted state, setting this bit causes the RAM clearing process to be initiated. Prior to doing this, make sure that MEMSIZ and DLENGTH are initialized to correctly indicate RAM size. When zeros have been written to all of the memory specified, this bit will automatically clear. The Host may poll it to check the status of the clearing operation."

**Refresh Disable**

"When the ESP enters its Halted state, it will normally continue to refresh the contents of external RAM. This is desirable for preserving the contents of dynamic RAMs. However, RAM refreshing (and clearing) is accomplished by counting through the delay memory address space using the Address Generator's DBASE counter. Under some circumstances, however, it may be undesirable to alter the contents of the Base Address Pointer while the" (continued)

---

## esp_39 (doc p.35)

"ESP is halted. The best examples of this are when using HALT as a means for synchronizing or using HALT to set the sample rate. While halted, waiting for the next sample to begin, we do not want the Base Address Pointer to commence RAM refresh, as this will disrupt delay lines. This bit stops DBASE from decrementing while in Halt Mode. Options for DRAM refresh are discussed in Section 5.3.1"

#### 5.1.5 PC, Internal Refresh Counter

"These registers are intended for IC test purposes only. They should not be written to by ordinary ESP applications programs."

**The Program Counter**

"The Program Counter (PC) is the microprogram instruction counter, which increments through the ESP's program memory while the chip is running. It will start at 00 and increment until an END instruction is encountered. The PC will then reset to 0 and begin execution if the HALT conditions are negative."

**The Refresh Counter**

"This is a counter that is used to refresh the array of internal dynamic memory used to implement the ESP's GPR and microinstruction address space. Frame (bit 7) is a bit that is toggled when the counter completes refreshing the GPR or INSTR array. This bit therefore indicates which array is currently being refreshed."

[note] GPR and instruction memories are internal *dynamic* RAM, refreshed by an internal counter (host reg $17). "Frame (bit 7)" — presumably bit 7 of the Frame Counter read at $1F or of the refresh counter at $17 [ambiguous].

#### 5.1.6 Halt Enable Register

"When the HALT pin is high, a write to this register forces the ESP to halt immediately, without waiting for an END statement. Normally, if the HALT pin is high the ESP will continue to execute instructions until an END instruction is encountered. Enabling this condition is meant to be used on power-up when the contents of microprogram memory are unknown or if a fault condition occurs and the host CPU wishes to stop execution immediately. If the ESP is executing a known program it is not recommended to use this feature, since the state of the machine could be unknown. For example it might be wise to ramp the serial output registers to zero before loading a new program, so that output D/A does not make a click. If the host CPU asserts the HALT pin to a high, the ESP will stop correctly when the END instruction occurs. The host CPU can now download a new program."

#### 5.1.7 Host Serial Control Register

"The Host Serial Control Register is used to setup the various modes for the Serial Clock Lines (LRCLK,WCLK,BCLK) and the Serial I/O Lines (SER[3:0]). The following chart indicates how the bits in this register control the mode of the lines. Master mode means that all Serial Clock lines are enabled. In slave modes these lines need to be driven from an external source."

---

## esp_40 (doc p.36)

**Host Serial Control Register** (host reg $18)

| Bit # | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|---|---|
| Set (1) | MASTER | SONY | SER3 out | SER2 out | SER1 out | SER0 out | 1 | 1 |
| Reset(0) | SLAVE | I2S | SER3 in | SER2 in | SER1 in | SER0 in | - | - |

"The ESP has four stereo 16-bit serial data lines which operate in conjunction with the bit clock, word clock, and Left/Right clock. If the ESP sources these signals it is in Master Mode. Each serial line can be configured as an input or an output. The timing diagrams that follow show the relationship on the control clocks and data for the various modes available. For more complete information on the timing of the signals refer to the detailed timing information in the end of the specification."

### Figures: SONY MASTER MODE / SONY SLAVE MODE (described)

Signals: BCLK, LRCLK, WCLK, DATA OUT, DATA IN.
- Frame = 32 BCLK periods: 16 bits left then 16 bits right. MSB first: `L15 ... L0`, then `R15 ... R0`.
- LRCLK **high = Left** channel, low = Right.
- Sony mode: L15 is aligned with the LRCLK rising edge (no 1-bit delay).
- WCLK: low for the first 8 bits of each 16-bit word, rises at the middle of the word (after 8 bits), falls at the LRCLK transition (end of word). I.e. WCLK = 2x the LRCLK rate; falling edge marks end of each 16-bit word.
- Sony Slave: same, except DATA OUT's right word (R15) begins after the LRCLK change is detected (drawn with a small gap after L0; LRCLK falls slightly after the end of L0 in that figure).

---

## esp_41 (doc p.37)

**Sony Master Mode**

"In Sony Master mode DATA OUT is changed after the fall of BLCK. DATA IN is latched on the falling edge of BCLK. LRCLK and WCLK are output immediately after the fall of BCLK."

**Sony Slave Mode**

"The LRCLK is sampled when BCLK is low. If a transition of the LRCLK is detected then DATA OUT begins during the same BCLK low phase where the LRCLK change is detected. LRCLK **must only change** when BCLK is low. Note: The setup time to BCLK rising is very important.

DATA IN is justified with respect to WCLK. This means that the 16 bits shifted in prior to the fall of WCLK while the LRCLK is stable are used as the DATA IN.

Since DATA OUT responds to the LRCLK and DATA IN is justified with respect to WCLK the serial left and right data I/O transfers can occur at different times in the sample period."

### Figures: I2S MASTER MODE / I2S SLAVE MODE (described)

- Same 32-BCLK frame, 16 bits per channel, MSB first; LRCLK drawn **high during the Left word** (as in Sony mode).
- I2S: L15 begins **one BCLK after** the LRCLK edge (standard I2S 1-bit delay); L0 is followed by R15 one BCLK after the opposite LRCLK edge.
- I2S Master figure includes WCLK (low first half, high second half, falls at LRCLK edge); I2S Slave figure has no WCLK.

---

## esp_42 (doc p.38)

"In both I2S mode WCLK is not used for data transfers. The clock is made available in Master mode simple because it is there. In slave mode, DATA IN is the first 16 bits shifted into the Serial Port after a change in LRCLK. There is a one cycle delay after a change in LRCLK before the MSB is available. It is not necessary to have left serial data followed immediately by right serial data. LRCLK can remain high after the 16 left channel bits have been received. Right serial data will not be begin being received until LRCLK falls."

### 5.2 Host Interaction and the END statement

#### 5.2.1 ESP End Statement

"When the program encounters an END instruction the following actions are performed:
1. Resets program counter. This causes execution to begin at the start of the program
2. Enables halt line sampling. When the end instruction is encountered the HALT line is sampled. If the halt line is high, execution is suspended until the halt line is reset low. Once the halt line is low, execution resumes at the beginning of the program.
3. Triggers serial clocks. This starts the clocking sequence for serial data communication. In Master mode LRCLK will go high to begin the sequence. In Slave mode, when the LRCLK pin goes high data transfers will begin.
4. Decrements Delay counter DBASE. As discussed in Section 3.5
5. Allows host access. A single instruction or GPR location is available for access without halting program execution"

**Emulator summary of END:** PC <- 0; sample HALT; start serial frame (master mode: LRCLK rises, so sample rate = 1/(program length x 300 ns) [inferred]); DBASE decrement (with test/reload per 3.5, see 5.4.1); perform one pending host GPR/INSTR transfer.
[note] How END is encoded in the instruction word is not stated on these pages (see part 1, 4.1).

### 5.3 Host Interface Pins

#### 5.3.1 HALT pin

"This input pin is used to place one or more ESP processors in the Halt state and to resynchronize them. Halt state is useful for initializing an ESP, allowing high-bandwidth access from the Host. (See section 5.4.1 for a detailed description of the Halt state.) Driving the HALT line high causes an ESP to restart its internal microprogram counter at location 0. HALT can be used to perform external sample-rate synchronization, single-sample step execution in a debugging environment, or program execution synchronization in a multi-ESP system configuration.

HALT is a level sensitive input. Holding it high causes the ESP to enter its Halt state at the next execution of the END instruction. The level of the HALT is sampled only while halted or during execution of the END statement, so the ESP will continue executing until the end of its microprogram before Halting. If RAM refresh is disabled by the Refresh Disable bit in the Host control register, this leaves the entire state of the ESP in a known condition when Halted. When HALT goes low, the ESP will continue executing from location 0."

---

## esp_43 (doc p.39)

"If multiple ESPs are connected to the same HALT line, pulling the line low will cause all of them to begin executing their programs together. The microcode for all ESPs do not need to be exactly the same length, as HALT will be setting the actual sample rate. However, if delay lines are used by any ESP, RAM refresh must be disabled to prevent DBASE from commencing RAM refresh while halted."

**Sample-Rate Sync**

"HALT can be used to synchronize an ESP to an external sample rate clock such as from an analog sub-system or from some other signal source. The first requirement for this method is that the microprogram execution time be equal to or shorter than the shortest sample period. The second is that RAM refresh must be disabled unless the sample period is never more than 1 instruction cycle longer than the microprogram. (see single cycle stretching, below.) The HALT pin is driven with an external sample clock that has been conditioned to provide a low-going "begin-sample" pulse that is shorter than the microprogram execution time.

When the "begin-sample" pulse goes low, the ESP begins executing its microprogram. Sometime during program execution, the "begin-sample" pulse will rise, causing HALT to go high. When the ESP encounters an END instruction, it will enter Halt state and wait for another "begin-sample" pulse before executing the next iteration of its microprogram.

For sample-rate synchronization to work properly when delay lines are used, RAM refresh must be disabled with the Refresh Disable bit in the Host Control register. This prevents the Base Address Pointer from being used for RAM refresh. As a general rule, microprograms using delay lines typically access enough RAM locations in a short enough time to satisfy the refresh requirements of dynamic RAMs. Therefore, disabling refresh under these conditions is of no serious consequence. Programs without delay lines should not disable the refresh feature."

**Single-Sample Execution**

"Single-sample execution can be seen as a special case of sample-rate sync, where the "begin-sample" pulse occurs at a very low and/or irregular rate. The "begin-sample" pulse should be narrow relative to the execution time of the program. When the ESP has finished one iteration of the program, it will enter the Halt state and wait for another "begin-sample" pulse.

While Halted, the Host has full internal access to the ESP and may examine and/or change any ESP microcode, GPRs or special registers. Though RAM data may be lost if refreshing is disabled while Halted, refreshing RAM causes the Base Address (DBASE) pointer to be altered and lose track of its delay lines, an equally undesirable side effect. A solution is to perform single-sample execution in the following manner:

1. While the ESP is running, disable RAM refreshing using the Refresh Disable bit.
2. Use Resync to Halt the ESP when it is desired to suspend execution.
3. Immediately save the current Base Address pointer value.
4. Enable RAM refresh by clearing the Refresh Disable bit."

---

## esp_44 (doc p.40)

"5. Perform any internal examination of the ESP required for debugging
6. Disable RAM refresh.
7. Reload the Base Address counter with the saved value.
8. Bring HALT low to resume execution."

#### 5.3.2 Host Bus

"The Host Bus is a bidirectional multiplexed address input and data I/O bus. The 8 bit host address specifies which ESP register is to be accessed by the host CPU. This 8 bit bus is latched into the ESP internal address latch upon the fall of the AS line. After the fall of the AS strobe, the data to written is placed onto the bus, the data will be latched on the rising edge of CS. In a read cycle, the ESP will place data on the bus to be read by the CPU. Data will remain on the bus until CS goes high."

#### 5.3.3 Address Strobe

"This pin is used to latch the input address from the host CPU. Once a stable address is available on the Host Bus, holding this line low will hold the address in the ESP internal address latch. This line is level sensitive, not edge sensitive, therefore it must be low during the entire access cycle after the address is latched. Any glitches on this line sould result in incorrect address information being written to the ESP. In normal designs the AS line and CS line can be tied together."

#### 5.3.4 Chip Select

"This pin is used to initiate data transfer to the CPU after the address has been latched. A low condition on the CS line indicates the ESP should find an open internal time slot and transfer data from the selected register to the Host Bus. Since the address is latched on the falling edge of AS and a low on CS initiates a data transfer, these lines can be tied together in most designs."

### 5.4 Host Initialization

#### 5.4.1 Halt State

"The ESP enters its Halt state when it detects a high condition on the HALT pin during the execution of an END statement. The HALT line is only sampled during the END cycle, so the ESP will not halt until it finishes executing a complete iteration of the microprogram.

When HALT is high, the ESP will perform its normal DBASE update (decrement, test, and possibly reload) before it halts. The microprogram counter, however, will not be reset to 0 until after it comes out of the Halt state, since it is being used for internal refreshing while the ESP is Halted.

While the ESP is Halted, the Host will be allowed more frequent access to internal GPRs and microcode - approximately one access per microinstruction period, or 300ns. Thus the Host will not normally need to poll the Host Access OK bit to determine when reading and writing can take place, since this period is shorter than a typical Host's" (continued)

---

## esp_45 (doc p.41)

"microprocessor instruction cycle. GPRs and microcode memory will appear to be static to the Host since they are refreshed automatically and transparently during Halt by the ESP.

External dynamic RAM, on the other hand, is not necessarily refreshed by the ESP during Halt. Refreshing may be disabled during Halt by the Host in order to preserve the state of the DBASE counter. This is necessary in situations where the HALT pin and Halt state are being used to synchronize the ESP to an external sample clock or where single-sample program execution is being performed for debugging purposes.

**Note:** GPR and INSTR Select Registers will inhibit GPR Refresh when the last GPR or INSTR addressed by the Select Register ($80, $A0, $C0, $E0). If the Select Registers contains a valid GPR (not SPR) address, that GPR will not be refreshed. This was necessary because host GPR/INSTR accesses are substituted for refresh cycles. If this condition persists for an extended period of time, that GPR may lose its data. When you are finished GPR and INSTR accesses, park the select register at a non-GPR address (like $FF) to avoid this."

**Enabling RAM Refresh**

"Two bits in the Host Control register determine whether refreshing and/or clearing of RAM will occur during the Halt state (see section 5.1.5). When the Refresh Disable bit and RAM Clear bits are false, the ESP will begin refreshing RAM as after it enters the Halt state.

Refresh is accomplished by sequencing the DBASE counter through consecutive RAM addresses. A new refresh address will be generated every microinstruction period. Refreshing will begin at the address contained in the DBASE pointer, looping from 0 to DLENGTH, decrementing one RAM location each instruction cycle as determined by MEMSIZ. Thus the amount of RAM refreshed will correspond to the size of delay memory."

**RAM Clearing**

"When new programs are loaded into the ESP, it is usually desirable to clear all of delay memory so that no residual noise wil exist in delay lines defined. To implement this, the ESP can be used to write zeros to its delay memory space while in the Halt state.

Setting the RAM Clear bit in the Host Initialization register will cause the ESP to begin clearing RAM. The ESP must be halted to do this. The DBASE pointer is initialized to the contents of the DLENGTH register. It begins counting down one RAM location at a time, as determined by MEMSIZ. Thus only the memory configured for delay lines is cleared by RAM Clear. To clear all of RAM, set DLENGTH to the top memory address first.

Every microinstruction period, the Address Generator will decrement DBASE and output a new address. It will put all zeros on its RAM data bus, and initiate a write cycle. When DBASE has counted down to zero, the RAM Clear bit will clear automatically to indicate to the Host that memory has been cleared.

The action taken by the ESP when RAM clearing is finished depends on the state of the Refresh Disable bit. If the bit is false, then the ESP will begin refreshing RAM when" (continued)

[note] "Host Initialization register" here = Host Control register ($12). "see section 5.1.5" should be 5.1.4.

---

## esp_46 (doc p.42)

"the clearing operation is complete. This is the normal course of action, since the RAM which has been cleared must be refreshed to preserve its cleared state."

**Host RAM Initialization**

"When the ESP is Halted, the Host processor has free access to the ESP's RAM through its Host RAM Interface. The exact protocol for accessing RAM through these registers is detailed in section 5.1.3.

Access to RAM is available through the Host Interface RAM address and data registers only when the ESP is Halted. Host table preload write operations will always have priority over refreshing operations."

#### 5.4.2 Power-up Sequence

1. Bring HALT high. Write $FF to the Halt Enable Register to halt the ESP.
2. Set MEMSIZ to match system's physical RAM configuration. ($0000FF for 64K )
3. Set DLENGTH register to all of memory. ($FFFFFF)
4. Reset the RAM Clear bit to 0.
5. Set the RAM Clear bit to 1 and the Refresh Disable bit to 0 in the Host Signal Control Register. (Write a value of 2).
6. Wait for the RAM clear bit to reset.
7. Initialize Host Serial Control Register.
8. Set the Product Shift Mode to 1 by setting high all bits in the SIGREG SPR.
9. Clear all serial I/O registers.

[note] MEMSIZ = $0000FF for 64K (the encoding of MEMSIZ is defined in part 1, 3.5.2). SIGREG SPR "Product Shift Mode" (all bits high = mode 1) is defined in part 1.

#### 5.4.3 Program Load Sequence

1. Load and/or initialize GPRs.
2. Load microcode.
3. Load tables (if any).
4. Park host GPR access register at $FF.

---

## esp_47 (doc p.43)

# PART 6 ... ESP PIN DESCRIPTION

### 6.1 Pinout

RAM Interface (total 21)

| Pin Name | Function | # Pins |
|---|---|---|
| RAM[15:0] | RAM Address/Data | 16 |
| DWEN | RAM or I/O Write Enable (active low) | 1 |
| RAS | Row Address Strobe | 1 |
| CAS | Column Address Strobe | 1 |
| GATE | Address Gate Enable | 1 |
| I/OSEL | I/O Select (active low) | 1 |

Host Interface (total 12)

| Pin Name | Function | # Pins |
|---|---|---|
| HOST[7:0] | Host Address/Data | 8 |
| CS | Chip Select (active low) | 1 |
| R/W | Host Read/Write | 1 |
| AS | Host Address strobe | 1 |
| HALT | ESP halt control | 1 |

Serial Interface (total 7)

| Pin Name | Function | # Pins |
|---|---|---|
| SER[3:0] | Serial Data I/O | 4 |
| BCLK | Bit Clock | 1 |
| WCLK | Word Clock | 1 |
| LRCLK | Left/Right Clock | 1 |

Other (total 8)

| Pin Name | Function | # Pins |
|---|---|---|
| CLK_IN | System Clock | 1 |
| VDD | +5 volt power supply | 3 |
| VSS | Ground (0 volt) | 4 |

[note] Host interface table says "total 12" but lists 12 pins; RAM total 21 = 16+5. The external RAM data path is 16 bits (consistent with DIL/DOL being 16 bits, left-justified in 24).

---

## esp_48 (doc p.44)

### ESP Pinouts

**48 pin DIP — ES5510D**

| Pin | Name | Pin | Name |
|---|---|---|---|
| 1 | CLKIN | 48 | R/W |
| 2 | VSS | 47 | VDD |
| 3 | DWEN | 46 | CS |
| 4 | RAS | 45 | AS |
| 5 | GATE | 44 | HDB0 |
| 6 | CAS | 43 | HDB1 |
| 7 | I/OSEL | 42 | HDB2 |
| 8 | VDD | 41 | HDB3 |
| 9 | VDD | 40 | HDB4 |
| 10 | VSS | 39 | HDB5 |
| 11 | VSS | 38 | HDB6 |
| 12 | SERI/O3 | 37 | HDB7 |
| 13 | SERI/O2 | 36 | HALT |
| 14 | SERI/O1 | 35 | RAM15 |
| 15 | SERI/O0 | 34 | RAM14 |
| 16 | BCLK | 33 | RAM13 |
| 17 | LRCLK | 32 | RAM12 |
| 18 | WCLK | 31 | RAM11 |
| 19 | RAM0 | 30 | RAM10 |
| 20 | RAM1 | 29 | RAM9 |
| 21 | RAM2 | 28 | RAM8 |
| 22 | RAM3 | 27 | RAM7 |
| 23 | RAM4 | 26 | RAM6 |
| 24 | VSS | 25 | RAM5 |

**52 pin PLCC — ES5510P**

| Pin | Name | Pin | Name | Pin | Name | Pin | Name |
|---|---|---|---|---|---|---|---|
| 1 | R/W | 14 | SERI/O3 | 27 | RAM5 | 40 | HDB7 |
| 2 | CLKIN | 15 | SERI/O2 | 28 | RAM6 | 41 | HDB6 |
| 3 | VSS | 16 | SERI/O1 | 29 | RAM7 | 42 | HDB5 |
| 4 | DWEN | 17 | SERI/O0 | 30 | RAM8 | 43 | HDB4 |
| 5 | RAS | 18 | BCLK | 31 | RAM9 | 44 | HDB3 |
| 6 | GATE | 19 | LRCLK | 32 | RAM10 | 45 | NC |
| 7 | CAS | 20 | WCLK | 33 | NC | 46 | NC |
| 8 | NC | 21 | RAM0 | 34 | RAM11 | 47 | HDB2 |
| 9 | I/OSEL | 22 | RAM1 | 35 | RAM12 | 48 | HDB1 |
| 10 | VDD | 23 | RAM2 | 36 | RAM13 | 49 | HDB0 |
| 11 | VDD | 24 | RAM3 | 37 | RAM14 | 50 | AS |
| 12 | VSS | 25 | RAM4 | 38 | RAM15 | 51 | CS |
| 13 | VSS | 26 | VSS | 39 | HALT | 52 | VDD |

### 6.2 Pin Descriptions

#### 6.2.1 Host Interface

- **R/W** — Read/Write input. Driven by the host microprocessor to a high level when reading ESP registers, and to a low level when writing them.
- **/CS** — Chip Select input. Driven low by host to access ESP registers for read or write.
- **/AS** — Address Strobe input. The host data bus is used to transfer both address and data. A high to low transition on AS/ is required for the ESP to latch the address from the host.
- **HALT** — Halt input. Halt stops ESP microprogram execution at the next completion of the microcode, and allows ultimate host access to ESP registers. On the falling edge, Halt restarts the ESP microprogram execution. HALT can be used to synchronize microprogram execution to external events.

---

## esp_49 (doc p.45)

- **HOST[7:0]** — Host address inputs/bidirectional data I/O. Used for reading and writing ESP registers.

#### 6.2.2 External RAM

- **RAS** — RAM Row Address Strobe output. Row addresses from the ESP are identified by high-to-low transitions on this pin. RAS occurs every instruction cycle.
- **GATE** — Gate output. The falling edge of GATE is used to latch ESP column addresses. This is necessary because data and not column addresses are being transferred at the normal CAS time. GATE occurs every instruction cycle.
- **CAS** — RAM Column Address Strobe output. During an external RAM read, CAS low indicates when to enable read data onto the ESP RAM data bus. During an external RAM write operation, the negative edge of CAS is delayed and is signals when RAM write data is valid.
- **DWEN** — External RAM Write Enable output. When low, indicates an external RAM or I/O write operation will occur instead of the default read.
- **IO_SEL** — External I/O Select output. This active low signal selects external I/O space rather than RAM. IO_SEL replaces CAS during I/O accesses.
- **RAM[15:0]** — Multiplexed bidirection external RAM and I/O address/data. This bus contains row and column address information multiplexed to coincide with RAS, GATE, and CAS or IO_SEL timing. The direction of data is determined by the DWEN line. RAM or I/O accesses occur once every instruction cycle.

#### 6.2.3 Serial Interface

"The signals BCLK, WCLK, and LRCLK can be either outputs or inputs, depending on whether the ESP is programmed to operate as the serial master or as a slave."

- **BCLK** — Bit Clock. This is the bit rate clock for all serial data interface lines.
- **WCLK** — Word Clock. A negative transition on WCLK signals the end of a 16 bit data transfer on the serial data pins. See section 5.1.7 for additional information. WCLK is generally not used in the I2S serial mode.
- **LRCLK** — Left/Right Clock. This pin is used to initiate data transfer on the serial data lines and to steer data to the proper (left or right) register on serial data inputs. A positive transition of LRCLK initiates a left word transfer, while a negative transition initiates a right word transfer.
- **SER[0:3]** — Bidirectional Serial Data Lines. Serial data is transferred on these pins. The pins are independently programmable as inputs or outputs.

---

## esp_50 (doc p.46)

#### 6.2.4 Supplies (clock and power)

- **CLK_IN** — Primary Clock input.
- **VDD** — +5.0 volt power supply < 100mA @ 10MHz clock
- **VSS** — Ground. (0 volt)

#### 6.2.5 System Interface Hardware Block Diagram (described)

- HOST ADDRESS BUS <-> 74LS244 (EN) <-> HD[7:0] of ESP (ES5510).
- HOST DATA BUS <-> 74LS245 (EN, DIR driven by HR/W) <-> same HD[7:0] bus.
- /ESP CS feeds a small gate network (two gates + inverter, cross-coupled) generating the EN signals for the '244 and '245, and also drives ESP AS and CS (tied together); HR/W -> ESP R/W and '245 DIR.
- ESP RAS, CAS -> DRAM 64K X 16.
- ESP RAM[15:0] <-> DRAM DATA[15:0]; RAM[15:0] also -> 74LS373 latch (G = ESP GATE) -> DRAM MUX ADDRESS [7:0].
  [inferred] Row address is taken directly from RAM bus at RAS; column address is captured by the '373 on GATE falling edge and presented to the DRAM address pins, since the RAM bus carries data at CAS time. (Diagram shows only 8 address lines to a 64Kx16 DRAM.)

**Handwritten annotations at bottom of page** (not part of the typed spec; partially legible):
- "Sample Rate = MCLK / 512"
- "Instr Rate = MCLK / 3"
- "Instr cycle = 1 / Instr Rate"
- "Sample cycle = 1 / Sample Rate"
- "Instructions [? 'Instructional'] = [scribbled-out, illegible]" ... and to the right "1/2 x (Sample cycle / Instr cycle)" [partially illegible; meaning uncertain].
[inferred] At MCLK = 10 MHz: instruction rate 3.333 MHz (300 ns); if sample rate = MCLK/512 = 19.53 kHz then 512/3 = ~170 instruction slots per sample. The "1/2 x" factor is unexplained [ambiguous].

---

## esp_51 (doc p.47)

# PART 7 ... Timing Diagram

### ESPR7 TIMING SPECIFICATIONS  ("ESP" with subscript "R7")

| # | Characteristic | Symbol | 8MHz Min | 8MHz Typ | 8MHz Max | 10MHz Min | 10MHz Typ | 10MHz Max | 12MHz Min | 12MHz Typ | 12MHz Max |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Chip Select Active Low | CSL | 100 | | 1000 | 100 | | 1000 | 100 | | 1000 |
| 2 | Read/Write setup time | R/WSU | 20 | | | 20 | | | 20 | | |
| 3 | Read/Write hold after CS | R/WH | 20 | | | 20 | | | 20 | | |
| 4 | Address Strobe High Time | ASHT | 50 | | | 50 | | | 50 | | |
| 5 | Address Strobe to CS setup time | ASSU | 30 | | | 30 | | | 30 | | |
| 6 | Address Strobe hold time | ASH | 0 | | | 0 | | | 0 | | |
| 7 | Address In setup time | ADSU | 30 | | | 30 | | | 30 | | |
| 8 | Address In hold time | ADH | 30 | | | 30 | | | 30 | | |
| 9 | Data In Set up time to CS rise | DSU | 30 | | | 30 | | | 30 | | |
| 10 | Data In hold time | DH | 30 | | | 30 | | | 30 | | |
| 11 | Data Out Access time | DACC | | | 75 | | | 75 | | | 75 |
| 12 | Memory Cycle time | TCYC | | 375 | | | 300 | | | 250 | |
| 13 | RAS high time | RASH | 120 | 125 | 130 | 95 | 100 | 105 | 80 | 83 | 86 |
| 14 | RAS to GATE delay | RGD | 60 | 63 | 65 | 47 | 50 | 53 | 40 | 42 | 45 |
| 15 | GATE high | GATE | 120 | 125 | 130 | 95 | 100 | 105 | | 83 | |
| 16 | Memory R/W to RAS setup | MRWS | 60 | 63 | 65 | 47 | 50 | 53 | 40 | 42 | 45 |
| 17 | CAS high time WRITE | CASHW | 180 | 186 | 192 | 145 | 150 | 155 | 121 | 125 | 130 |
| 18 | CAS high time READ | CASHR | | 250 | | | 200 | | | 165 | |
| 19 | Row Address setup time | RASU | | 40 | | | 35 | | | 30 | |
| 20 | Row Address hold time | RAH | | 30 | | | 25 | | | 20 | |
| 21 | Column Address setup time | CASU | | 25 | | | 20 | | | 15 | |
| 22 | Column Address hold time | CAH | | 15 | | | 15 | | | 15 | |
| 23 | Memory Read Data setup time | MRDSU | 30 | | | 30 | | | 30 | | |
| 24 | Memory Read Data hold time | MRDH | 10 | | | 10 | | | 10 | | |
| 25 | Memory Write Data setup time | MWDSU | | 50 | | | 40 | | | 30 | |
| 26 | Memory Write Data hold time | MWDH | | 20 | | | 15 | | | 10 | |
| 27 | Bclk rising output delay | BCLKRD | | 20 | 30 | | 20 | 30 | | 20 | 30 |
| 28 | Bclk falling output delay | BCLKFD | | 20 | 30 | | 20 | 30 | | 20 | 30 |
| 29 | LRclk and Wclk delay | LRWD | | 20 | 30 | | 20 | 30 | | 20 | 30 |
| 30 | Bclk to Serial Data out delay | SDD | | 30 | 40 | | 30 | 40 | | 30 | 40 |
| 31 | Serial Data Out hold time | SDOH | 20 | | | 20 | | | 20 | | |
| 32 | Data In setup time | SDSU | 30 | | | 30 | | | 25 | | |
| 33 | Data In hold time | SDIH | 30 | | | 30 | | | 30 | | |
| 34 | LRclk & Wclk after Bclk ↓ | LRWC | 10 | | 30 | 10 | | 30 | 10 | | 30 |
| 35 | LRclk & Wclk before Bclk ↑ | LRWSU | 50 | | | 50 | | | 50 | | |
| 36 | Data In setup to Bclk ↓ (Sony) | SSDISU | 40 | | | 40 | | | 40 | | |
| 37 | Data In hold after Bclk ↓ (Sony) | SSDIH | 40 | | | 40 | | | 40 | | |

"Note: All timing parameters are in nanoseconds."

**Key emulator numbers:** memory cycle = microinstruction cycle = 3 CLK periods: 375 ns @ 8 MHz, 300 ns @ 10 MHz, 250 ns @ 12 MHz. (Consistent with 4.3.1: 3 T clocks per microinstruction.)

---

## esp_52 (doc p.48)

### CPU WRITE to ESP TIMING (figure)
Signals /CS, R/W, AS, H BUS. AS high (4: ASHT) then falls; H BUS carries ADD with setup (7) / hold (8) around AS fall; AS-to-CS setup (5); R/W setup before CS fall (2); /CS low for (1); DATA on H BUS with setup (9) before CS rise and hold (10) after; R/W hold (3) and AS hold (6) after CS rise.

### CPU READ from ESP TIMING (figure)
Same as write, but R/W high; after ADD phase, ESP drives DATA on H BUS within access time (11: DACC, max 75 ns) [figure shows (11) measured from end of ADD/CS region to DATA valid]; (12) shown as data hold after CS rise [note: (12) in the parameter table is TCYC "Memory Cycle time" — the label reuse in this figure appears to be an error in the original, or (12) here denotes data-out hold; ambiguous].

---

## esp_53 (doc p.49) — Rev 2.4

### DRAM READ INTERFACE TIMING (figure)
Signals /RAS, GATE, DWEN, /CAS, RAM 15:0. (12) TCYC spans one full cycle (RAS fall to next RAS fall). (13) RAS high time; (14) RAS-to-GATE delay; (15) GATE high; (16) memory R/W(DWEN)-to-RAS setup; (17) CAS high time [labeled 17 = CASHW in table, though this is the READ figure]; (19)/(20) row address setup/hold around RAS fall; (21)/(22) column address setup/hold around GATE fall; (23)/(24) read data setup/hold at end of cycle (CAS rising).

### DRAM WRITE INTERFACE TIMING (figure)
Same signals; DWEN low; CAS high time labeled (18) [= CASHR in table — the 17/18 labels appear swapped between the read and write figures relative to the table names]; (19)-(22) as above; (25) write data setup (data driven on RAM bus after column address) and (26) write data hold after CAS rise.

Bus sequence per cycle (both): ROW address -> COL address -> DATA.

---

## esp_54 (doc p.50) — Rev 2.4

### MASTER MODE CLOCKS (figure)
CLK-IN; BCLK edges follow CLK-IN edges with delays (27) BCLKRD and (28) BCLKFD; LRCLK/WCLK change after BCLK fall with delay (29) LRWD. [inferred from drawing] BCLK half-period spans about 1.5-2 CLK-IN periods in the drawing; exact BCLK divide ratio not stated in typed text [ambiguous] (handwritten note on esp_50 suggests sample rate = MCLK/512, which with a 32-bit frame gives BCLK = MCLK/16 [inferred]).

### I2S MODE (figure)
BCLK and DATA IN/OUT: (30) SDD BCLK-to-data-out delay; (31) SDOH data-out hold; (32) SDSU data-in setup and (33) SDIH data-in hold. Data changes after BCLK falling edge.

### SONY MODE (figure)
BCLK, DATA OUT, LRCLK, WCLK, DATA IN: (30)/(31) as above; (34) LRWC LRclk & Wclk after BCLK fall; (35) LRWSU LRclk & Wclk before BCLK rise; (36) SSDISU data-in setup to BCLK fall; (37) SSDIH data-in hold after BCLK fall.

---

## esp_55 (doc p.49) — **older revision page (footer: "ENSONIQ ESP Specification Rev. 2.1", page 49)**

Duplicate of DRAM READ / WRITE INTERFACE TIMING (esp_53) from Rev 2.1. Differences: the write-enable signal is labeled **MR/W** instead of DWEN. Otherwise the same numbered parameters (12)-(26).

---

## esp_56 (doc p.50) — **older revision page (footer: "ENSONIQ ESP Specification Rev. 2.1", page 50)**

Duplicate of MASTER MODE CLOCKS / I2S MODE / SONY MODE timing (esp_54) from Rev 2.1, with **handwritten annotations**:
- Next to "I2S MODE": "Rev 3 only" [handwritten] — i.e. I2S mode presumably only available on chip revision 3 [inferred].
- On the I2S DATA IN/OUT waveform, one bit slot (the one after the (32)/(33) data-in sampling point) is labeled "MSB" [handwritten].
- Near "SONY MODE": "Left justified I²S (LRCLK low = Right)" [handwritten; one word struck through before "(LRCLK", illegible] — i.e. Sony mode = left-justified format, LRCLK low = Right channel (consistent with LRCLK high = Left in the esp_40/41 figures).
- On Sony DATA IN: the bit slot following the WCLK-fall region is labeled "MSB" [handwritten].
- A handwritten initial/signature mark at left [illegible].

---

## Consolidated emulator-relevant facts from pages 29-56 (summary, [inferred] where synthesized)

1. **Timing:** microinstruction = 3 input clocks (T0,T1,T2); 300 ns @ 10 MHz. Multiplier cycle leads ALU cycle by 1 T.
2. **ALU->MUL hazard:** ALU result from step N not usable as a multiplier operand at N+1 (use at N+2). MUL result from step N usable by ALU at N+1.
3. **MAC:** 48-bit, = MACH:MACL; rewritten every cycle (MUL or MAC); default/"NOP" = MAC + 0*0; ALU preload at step N-1 feeds accumulate at step N; plain multiply discards the preload; ALU reads of MACH/MACL are conditionally saturated; MACL is the only path to the low 24 bits; MACH/MACL are not multiplier sources; MACL as destination duplicates upper half into lower; MACH as destination undefined.
4. **RAM pipeline:** read address/control must be issued 2 instructions before the data is used (DIL valid at start of step N+2); write address issued at the same step as the instruction producing the write data; DOL is a 2-deep FIFO; RAM control field = 3 bits, delayed 1 cycle with the address; one RAM/IO access per instruction cycle; DIL holds through intervening writes.
5. **Condition mask byte:** N=bit7, C=bit6, V=bit5, LT=bit4, Z=bit3, NOT=bit2 (see table on esp_29).
6. **Instruction word (48 bits)**, from host map: [47:40] Mult D operand addr, [39:32] Mult C operand addr, [31:24] ALU B operand addr, [23:16] ALU A operand addr, [15:8] ALU opcode / operand select, [7:3] Skip(bit7) + MAC + RAM control (3 bits) [bit assignment within 7:3 partly inferred; full format is on pages 18-22 (part 1)].
7. **Memory sizes:** INSTR $00-$9F (160 words), GPR $00-$BF (192 x 24-bit), SPR $EA-$FF.
8. **Host map:** $00-$02 GPR data (MSB first), $03-$08 INSTR bytes, $09-$0B DIL ($0B reads 00), $0C-$0E DOL ($0E reads FF), $0F-$11 DADR (HI written last triggers access; $11 holds bits 7-4), $12 Host Control (bit2 Host Access OK/ RO, bit1 RAM Clear, bit0 Refresh Disable), $14 RAM Access Control (bit7 Read=1/Write=0, bit6 RAM=1/IO=0), $16 PC (test), $17 refresh counter (test), $18 Host Serial Control, $1F Halt Enable (W) / Frame Counter (R), $80 read select, $A0 GPR write select, $C0 INSTR write select, $E0 GPR+INSTR write select.
9. **Host GPR/INSTR transfer:** one per END while running (replaces ALU A/B operand fetch); about one per 300 ns while halted. Select register pointing at a GPR inhibits that GPR's internal refresh -> park at $FF.
10. **END:** PC=0; sample HALT; start serial frame (LRCLK rises in master mode); DBASE decrement/test/reload; one host transfer.
11. **HALT:** level-sensitive, sampled only at END (or while halted); Halt Enable register write with HALT high halts immediately. During halt: PC used for internal refresh; DRAM refresh via DBASE decrementing per instruction cycle from DBASE, wrapping at DLENGTH (unless Refresh Disable); RAM Clear: DBASE := DLENGTH, write zeros counting down to 0, then bit self-clears.
12. **Serial:** 4 stereo 16-bit lines SER0-3, each in or out (reg $18 bits 2-5); MASTER/SLAVE bit7; SONY(1)/I2S(0) bit6; bits1-0 written as 1. 32 BCLK/frame, MSB first, LRCLK high = left; Sony = left-justified (no bit delay), I2S = 1-bit delay. WCLK falls at end of each 16-bit word.
13. **Power-up:** MEMSIZ=$0000FF for 64K; DLENGTH=$FFFFFF; write 2 to Host Control for RAM Clear; SIGREG SPR all ones => "Product Shift Mode" = 1.

### Not found on pages 29-56 (must come from part 1 / pages 1-28)
- Exact microinstruction field encodings (ALU opcode table, operand select encoding, RAM control encoding, END bit encoding, MAC bit meaning).
- ALU operation semantics, saturation and flag computation details, multiplier operand widths/rounding.
- SPR list (SER0-3 L/R registers, DIL/DOL/DADR, MACH/MACL, CCR/CMR, DBASE/DLENGTH/MEMSIZ/ABASE/BBASE, SIGREG etc.) and Address Generator formulas.
- No example microcode or appendices appear on pages 29-56; the document ends at esp_56 with two superseded Rev 2.1 timing pages.

