# Ensoniq ES5510 "ESP" (ENSONIQ Signal Processor) — Specification Rev. 2.4
## Transcription, Part 1 (scan pages esp_01.png – esp_28.png)

Conventions:
- "[pNN]" = scan page file esp_NN.png; "doc p. N" = the printed page number at the bottom of the page.
- `[illegible]` = could not be read; `[inferred]` = my interpretation, not printed text.
- Tables and formulas are quoted as printed (including the document's own typos, flagged with [sic] where relevant).
- Note: the "List of Figures" page numbers (p4) do not match the TOC/printed page numbers (they appear to be from an older revision). The TOC numbers match the printed page footers.

---

## [p01] Cover

- ENSONIQ (TM) logo
- **ES 5510 "ESP" — ENSONIQ Signal Processor**
- "SOUND BY ENSONIQ" logo
- Footer: "ENSONIQ ESP Specification Rev. 2.4"

---

## [p02–p04] Table of Contents (printed page numbers)

```
1.Introduction ............................................. 1
2.General Description ...................................... 1
3.Chip Architecture ........................................ 3
   3.1. Instruction and GPR Memory Arrays ................. 3
   3.2. Special Purpose Registers ......................... 4
   3.3. The ALU ........................................... 6
        3.3.1 ALU Operations .............................. 6
        3.3.2 Condition Code Register ..................... 9
        3.3.3 Setting Condition Codes (Pipeline Considerations) .. 10
   3.4 Multiplier ......................................... 11
   3.5 Address Generator .................................. 12
        3.5.1 Address Generator Architecture .............. 12
        3.5.2 Selecting the Addresssing range [sic] ....... 13
        3.5.3 RAM Addressing Modes ........................ 13
              Delay Line Addressing ....................... 13
              Table Addressing ............................ 14
              External I/O Space Addressing ............... 14
        3.5.4 Physical Address Computation ................ 14
              Delay Line Addressing Mode .................. 14
              Table addressing Mode ....................... 15
              I/O addressing Mode ......................... 15
   3.6 Busses ............................................. 15
        3.6.1 Internal Operand Busses ..................... 15
        3.6.2 External RAM Busses ......................... 16
              RAM Data/Address Bus ........................ 16
              RAM Bus Control Lines ....................... 17
              Memory vs. I/O Space ........................ 17
        3.6.3 RAM Data Interface .......................... 17
              Data Input Latch (DIL) ...................... 17
              Data Output Latch (DOL) and FIFO ............ 17
4. Operation .............................................. 18
   4.1 Microinstruction Word .............................. 18
              Microinstruction Word Format ................ 18
        4.1.1 Operand Fields .............................. 19
        4.1.2 ALU Opcode Field ............................ 19
              ALU Operation Mnemonics and Opcodes ......... 19
        4.1.3 Operand Select Modes ........................ 19
              ALU Operand/Bus Mapping ..................... 19
              Multiplier Operand/Bus Mapping .............. 20
              Operand Select Field ........................ 21
        4.1.4 'No output' Multiplier Destinations ......... 21
        4.1.5 Skip and MAC Bits ........................... 22
        4.1.6 RAM Control Field ........................... 22
              RAM Control Field Encoding .................. 22
   4.2 Conditional Execution .............................. 23
        4.2.1 Basic Conditional Mechanism ................. 23
        4.2.2 Arithmetic Condition Masking ................ 23
              Arithmetic Condition Code Masks ............. 25
   4.3 Microinstruction Execution Cycle ................... 25
        4.3.1 Basic Execution Cycle ....................... 25
        4.3.2 ALU/Multiplier Timing Offset ................ 26
        4.3.3 Access to MACH and MACL ..................... 27
              Preload and Postfetch ....................... 27
              MACH and MACL Volatility .................... 27
              MACL and MACH as Multiplier Operands ........ 28
        4.3.4 RAM Pipelining .............................. 28
              READ vs. WRITE Timing ....................... 28
              Write Data Latency .......................... 29
              RAM Address Assembly ........................ 29
5. Host Interfacing ....................................... 31
   5.1 Host Interface Registers ........................... 31
        5.1.1 Host Interface Memory Map ................... 31
        5.1.2 GPR/Instruction Access ...................... 32
              Host Access OK/ ............................. 32
        5.1.3 RAM Registers ............................... 32
              RAM Access Control .......................... 33
              Reading RAM Data ............................ 33
              Writing RAM Data ............................ 33
        5.1.4 Host Control Register ....................... 34
              Host Access OK/ ............................. 34
              RAM Clear ................................... 34
              Refresh Disable ............................. 34
        5.1.5 PC, Internal Refresh Counter, and Frame Counter .. 35
              The Program Counter ......................... 35
              The Refresh Counter ......................... 35
        5.1.6 Halt Enable Register ........................ 35
        5.1.7 Host Serial Control Register ................ 35
   5.2 Host Interaction and the END Statement ............. 38
        5.2.1 ESP End Statement ........................... 38
   5.3 Host Interface Pins ................................ 38
        5.3.1 HALT Pin .................................... 38
              Sample Rate Sync ............................ 39
              Single-Sample Execution ..................... 39
        5.3.2 Host Bus .................................... 40
        5.3.3 Address Strobe .............................. 40
        5.3.4 Chip Select ................................. 40
   5.4 Host Initialization ................................ 40
   5.4.1 Halt State ....................................... 40
              Enabling RAM Refresh ........................ 41
              RAM Clearing ................................ 41
              Host RAM Initialization ..................... 42
        5.4.2 Power-up Sequence ........................... 42
        5.4.3 Program Load Sequence ....................... 42
6. ESP PIN DESCRIPTION .................................... 43
   6.1 Pinout ............................................. 43
   6.2 Pin Descriptions ................................... 44
        6.2.1 Host Interface .............................. 44
        6.2.2 External RAM ................................ 45
        6.2.3 Serial Interface ............................ 45
        6.2.4 Supplies (clock and power) .................. 46
        6.2.5 System Interface Hardware Block Diagram ..... 46
7. Timing Diagrams ........................................ 47
```

List of Figures (as printed; page numbers do NOT match the printed page footers of this revision):
```
ALU Functions ............................ 11
ALU Operation Mnemonics and Opcodes ...... 23
ALU to Internal Bus Mapping .............. 24
Arithmetic Condition Code Masks .......... 29
Condition Code Register Bits ............. 13
Conditon [sic] Code Computation .......... 14
DRAM Timing .............................. 34
ESP Block Diagram ........................ 8
GPR bits to DRAM mapping ................. 20
Host Control Register .................... 38
Host Interface Memory Map ................ 35
Host Serial Control Register ............. 40
Internal Event Timing for the ESP ........ 30
Microinstruction Word Format ............. 22
Multiplier Bus Mapping ................... 24
Negative Flag Test Conditions ............ 28
Operand Select Field Encoding ............ 25
Positive Flag Test Conditions ............ 28
RAM Access Control Register .............. 37
RAM Control Field Encoding ............... 26
Special Purpose Registers ................ 9
System Interface Hardware Block Diagram .. 46
```

---

## [p05] doc p. 1 — ESP ES5510

### 1. Introduction

The ENSONIQ ESP 5510 is a VLSI device designed in a 1.0 micron double-metal CMOS process. It is optimized for signals in the audio frequency range, which have typical sample rates of between 10 kHz and 50 kHz. The ESP executes its microprogram once every sample period. Its nominal instruction cycle is **250 ns**, yielding program lengths from about **64 to 160 microinstructions** at typical sample rates.

The major features of the ESP chip are:
- 48 Pin DIP or 52 Pin PLCC
- Separate Address Generator ALU
- 4 Programmable Serial I/O Channels (I2S or Sony Format)
- On Chip Data and Microprogram Memory
- 8 Bit Address/Data Multiplexed Host CPU Interface
- External Sample Rate Synchronization
- Multiplexed Addressing for Simple DRAM Interface
- Host Access to ESP DRAM

### 2. General Description

The architecture of the ESP chip is implemented by the following major components:
- ALU
- Multiplier
- Address Generator
- On-chip data and microprogram memory
- Three 24 bit wide data paths
- Stereo serial digital audio interface
- Host interface

The ALU is a general purpose **24-bit** wide arithmetic/logic unit capable of **16 different instructions**. It has an associated Condition Code register which can be used to implement flow control constructs via the conditional execution of instructions.

The Multiplier is a **24 X 24 bit multiplier with its own dedicated 48 bit accumulator**. It can produce an accumulated product every microinstruction cycle.

The Address Generator is a separate ALU dedicated to calculating RAM memory addresses. A new read or write address is calculated according to one of several addressing modes every microinstruction cycle. The Address Generator allows memory to simultaneously support both recirculating delay lines and fixed data table structures. Through the Address Generator, the chip will interface directly to up to **16 Kbytes [sic? see note] of external 16-bit wide RAM**. Data transfers to memory are performed through a Data Input Latch (DIL) and Data Output FIFO (DOL FIFO). The DOL FIFO is **2 words deep**, to

> [inferred] note: "16 Kbytes" is as printed; elsewhere the doc speaks of 64K-word systems and 20-bit address registers, so this figure is likely a typo/old value (perhaps meant "16 Mbits" / "1M words"). Verify against later pages.

## [p06] doc p. 2

improve output throughput. An alternate I/O memory space can also be selected on a cycle by cycle basis which can be used as a 16-bit parallel interface to external hardware.

On chip memory consists of **192 24-bit wide GPRs**, or general purpose registers, and **160 45-bit microinstructions**, implemented using internal dynamic RAM. In addition to the GPRs there are a number of SPRs, or special purpose registers, which are used for I/O, condition codes, and addressing control. The GPRs are used for the storage of RAM address offsets, multiplier coefficients, and as general scratch pad memory.

Three internal 24 bit data busses, **X, Y, and Z** connect the GPRs to the ESP's processing units. The X and Y busses provide the inputs to the ALU and Multiplier while the Z bus is used to write the results from the ALU and Multiplier back into the registers or to external memory.

Pins are provided to synchronize program execution and communication, allowing systems to be easily configured with a variety of other signal processors or sources. The synchronization mechanism also allows execution in single sample steps to facilitate the debugging of microprograms. Registers are provided on the ESP to interface the chip to a Host microprocessor required to load microprograms and data into internal memories. Typically the Host will also alter the contents of GPRs or microinstructions while microprograms are executing.

The ALU, Multiplier and Address Generator all execute concurrently, using the data busses at different times. A separate microprogram bus is used to supply microinstructions to the ESP's processing elements so that instructions and operands can be fetched simultaneously. The microprogram memory does not appear on the X, Y or Z busses, so it may not be modified directly by ESP microprograms.

The GPRs are the operands most often used for ALU and Multiplier operations. The contents of **4 distinct 24-bit GPRs can be fetched directly during each microinstruction cycle** with no setup overhead. The results are written back to **1 or 2 of these 4 GPR source locations**. Alternatively, external RAM accesses can be substituted for 1 or 2 of the source operands and for 1 of the destination operands. The multiplicity and flexibility of the data paths in the ESP allows many DSP operations to be accomplished in a minimum number of microinstruction steps. Only a moderate amount of pipelining exists in the ESP's internal data paths, enhancing its ease of programmability.

| Examples | |
|---|---|
| Nth-order FIR filter | N steps |
| 2nd Order Filter section | 5 steps |
| All-pass filter | 2 steps |

### 3. Chip Architecture

The following are the major elements of the ESP chip:
- Microinstruction Memory Array (160 x 45 bits)
- General Purpose Register Array (192 x 24 bits)
- X,Y,Z Bus System
- Arithmetic/Logic Unit
- Multiplier

## [p07] doc p. 3

- External Memory Address Generator
- Host Interface
- Special Purpose Registers
- Serial Digital I/O (4 stereo channels, I2S or Sony)

### 3.1. Instruction and GPR Memory Arrays

The main memory inside the ESP is composed of 192 GPRs (general purpose registers) and 160 microinstruction words. These two memory arrays are arranged side by side, both physically and logically, so that **each of the first 160 GPRs have an adjoining microinstruction word**. The importance of this alignment will be discussed in the description of the Address Generator. Each GPR is 24 bits wide, while each Instruction word is 45 bits. Data in GPRs can be read and written by programs executing on the ESP, but the instruction memory can be accessed only by the ESP's program counter and by the Host CPU.

Instruction Memory is similar to any other microprocessor instruction storage area. It contains the program that will be executed by the ESP chip. The program will begin executing the first instruction in memory at the beginning of each sample period. The program will continue to execute each instruction in order until an END instruction is encountered at which point it will return to the first instruction and resume execution or wait for an external synchronization pulse to begin execution. GPRs have two purposes in the ESP chip, external RAM address offsets and data storage.

When used as an offset, **the data in the GPR corresponding to the microinstruction** [i.e. GPR index == PC, inferred] is used to calculate the indirect addresses to external DRAM. GRPs [sic] not used to store offsets are available to hold multiplier coefficients or temporary variables used in the ESP program.

## [p08] doc p. 4 — ESP BLOCK DIAGRAM (figure)

Figure description (as drawn):
- **Host Interface** block at top.
- **GPR'S 192 X 24** (left) side by side with **Microcode Memory 160 x 45** (right).
- Below the GPRs: **SPR's** block.
- An arrow labelled **"Offset GPR"** goes from the GPR array to the **RAM ADD GEN** block; a second arrow goes from the SPR's to RAM ADD GEN. RAM ADD GEN outputs **"Ram Add Bus"**.
- **X Bus 24** and **Y Bus 24** come out of the GPR/SPR area.
- **DIL** block takes input from the bidirectional **Ram Data Bus** and drives onto the **Y Bus**.
- **Multiplier 24 x 24** with inputs labelled **D** and **C** (both fed from the X/Y busses), with an **Accumulator 48** below it.
- **ALU 24** with inputs **B** (from Y bus) and **A** (from X bus).
- The ALU output and the Accumulator output feed the **Z Bus 24**, which loops back to the GPRs/SPRs.
- Z bus / ALU output also goes to **DOL x 2** which drives the **Ram Data Bus**.
- A bidirectional arrow connects the Accumulator and the ALU-output/Z path.

[inferred] Mapping: ALU A input = X bus, ALU B input = Y bus; multiplier C and D inputs are each taken from X/Y bus (exact mapping given in section 4.1.3).

### 3.2. Special Purpose Registers

In addition to the GPR's, there are other 'hardware' registers which are addressed in the same way but have special functions. Among these special registers are the two 24 bit halves of

## [p09] doc p. 5

the multiplier's 48 bit product accumulator. Others include address generator table registers, and various machine control registers.

**Special Purpose Registers**

| Address (dec) | Address (hex) | Width (bits) | Register Name | Function |
|---|---|---|---|---|
| *Serial I/O Registers* | | | | |
| 234 | EA | 16 | SER0R | Right Serial I/O Register 0 |
| 235 | EB | 16 | SER0L | Left Serial I/O Register 0 |
| 236 | EC | 16 | SER1R | Right Serial I/O Register 1 |
| 237 | ED | 16 | SER1L | Left Serial I/O Register 1 |
| 238 | EE | 16 | SER2R | Right Serial I/O Register 2 |
| 239 | EF | 16 | SER2L | Left Serial I/O Register 2 |
| 240 | F0 | 16 | SER3R | Right Serial I/O Register 3 |
| 241 | F1 | 16 | SER3L | Left Serial I/O Register 3 |
| *Multiplier Registers* | | | | |
| 242 | F2 | 24 | MACL | Least Significant Multiplier Accumulator |
| 243 | F3 | 24 | MACH | Most Significant Multiplier Accumulator |
| *Address Generator Registers* | | | | |
| 244 | F4 (R) | 16 | DIL | Ram Data Input Latch (left justified) |
| 244 | F4 (W) | 20 | MEMSIZ | System Memory Size Register |
| 245 | F5 | 20 | DLENGTH | Length of Delay Memory |
| 246 | F6 | 20 | ABASE | Table "A" Base Address Register |
| 247 | F7 | 20 | BBASE | Table "B" Base Address Register |
| 248 | F8 | 20 | DBASE | Delay Base Address Counter (decrements from DLENGTH to 0) |
| *Control Registers* | | | | |
| 249 | F9 | 1 | SIGREG | ESP Signal Register Bit 22=Product Shift 1⇒Shift ONE, 0⇒ Shift TWO |
| 250 | FA | 5 | CCR | Condition Code Register |
| 251 | FB | 6 | CMR | Condition Mask Register |
| *Constant Memory* | | | | |
| 252 | FC | 24 | $FFFFFF | -1 Constant (MINUS 1) |
| 253 | FD | 24 | $800000 | Negative Full Scale (MIN) |
| 254 | FE | 24 | $7FFFFF | Positive Full Scale (MAX) |
| 255 | FF | 24 | $000000 | Zero (ZERO) |

Notes:
- GPRs occupy addresses 0..191 (192 GPRs) [inferred from counts]. Addresses 192..233 ($C0..$E9) are not listed on this page [inferred: unused/reserved — not described here].
- DIL (read at $F4) and MEMSIZ (write at $F4) share the same address.
- SIGREG is 1 bit wide, and that bit is **bit 22** of the 24-bit word: 1 ⇒ product shift ONE, 0 ⇒ product shift TWO.

The MACH and MACL registers are the most significant and least significant halves of the Multiplier's 48-bit wide accumulator. They can be accessed exactly as GPRs with few restrictions. See section 4.3.3.

The DIL register is the external RAM Data Input Latch, and is a special purpose register to enable access during 1-operand instructions. **Only the leftmost 16 bits are used.**

## [p10] doc p. 6

**The unused 8 bits are read back as zeros.** The DOL FIFO, mentioned previously, is not available to the host CPU. It is only available as a destination in an ESP instruction.

The CCR and CMR registers are concerned with conditional execution and are explained in Sections 3.2.2 and 4.2. The DLENGTH, ABASE, BBASE, DBASE, and MEMSIZ registers are all part of the Address Generator and are covered in Section 3.5. **Writing the MEMSIZ register affects the DBASE register directly.**

The arithmetic constants are often used by ESP programs. They are implemented in ROM, but **can be written to when dummy destinations are needed** [i.e. writes are discarded, inferred].

### 3.3. The ALU

The Arithmetic/Logic Unit (ALU) can perform a variety of general and special purpose arithmetic, data movement, and logical operations. It incorporates optional zero-overhead saturation arithmetic for handling computational overflow (analogous to "clipping"), and can shift signals left or right for the purposes of normalization or amplification.

During every microinstruction cycle the ALU takes one or two 24-bit operands and produces a 24 bit result. ALU execution overlaps with the operation of the Multiplier, so that the two units may operate in parallel. Detailed description of the ALU and Multiplier execution cycles may be found in Section 4.3.

Associated with the ALU is a Condition Code Register (CCR) whose status bits are set as a result of ALU operations. The CCR along with the Condition Mask Register (CMR) are used to implement the ESP's conditional execution mechanism (see section 4.2.)

#### 3.3.1 ALU Operations

The ALU is capable of the 16 different arithmetic or logical operations described below. ALU operations are specified by the opcode field of the microinstruction, described in Section 4.1.2. The operations are listed here in order of their opcodes. A more complete description of each opcode follows the chart.

## [p11] doc p. 7 — ALU Functions

| Function | Description |
|---|---|
| ADD | Two's Compliment Addition with Overflow Protection (Clipping) |
| SUB | Two's Compliment Subtraction with Overflow Protection (Clipping) |
| ADDU | Two's Compliment Addition with no Overflow Protection |
| SUBU | Two's Compliment Subtraction with no Overflow Protection |
| CMP | A-B without result storage. Used to set Condition Codes |
| AND | Boolean AND operation |
| OR | Boolean OR operation |
| XOR | Boolean Exclusive OR operation |
| ABS | Absolute Value operation A=\|B\| |
| MOV | Move data from register A to register B. no CCR effect. |
| ASL2 | Arithmetic Shift Left 2 with Overflow Protection |
| ASL8 | Arithmetic Shift Left 8 with Overflow Protection |
| LS15 | Left Shift 15 bits, clear sign bit, no Overflow Protection |
| DIFF | "Difference" Operation, A=$7FFFFF-B, with Overflow Protection |
| ASR | Arithmetic Shift Right, sign bit is preserved |
| END | End of the Microprogram |

(Table is stated to be in opcode order; if so, opcodes would be ADD=0 … END=15 [inferred — confirm against the opcode table in 4.1.2].)

Note the MOV row says "register A to register B" but the detailed text below says "MOVE B to A" — the text is the more specific statement.

**ADD** is a classical saturating 2's complement addition operation used to create the sum of two signals. If the sum cannot be represented in 24 bits, full-scale positive ($7FFFFF) or full-scale negative ($800000) is substituted for the overflowed result. The operation performed is **A = A + B**.

**SUB** is a 2's complement classical saturating subtraction operation which yields the difference of two signals, analogous to ADD. It performs the operation **A = A - B**.

**ADDU** is an unsaturating addition operation. It behaves exactly as ADD, except that it lacks overflow correction. For this reason it is not normally used to add signals together. However, it is useful for generating ramp signals and for performing address arithmetic.

**SUBU** is an unsaturated subtraction operation, analogous to ADDU. Like SUB, it performs the operation **A = A - B**.

**CMP** is used to make signed comparisons between two values. It performs the operation A - B, but does not store the result back into A. The purpose of the comparison is to set bits in the Condition Code Register which can then be used to affect conditional instruction execution. **The skip bit does not affect this instruction.**

**AND** performs bit-wise logical AND-ing of each bit of two 24-bit operands. It can be used for many operations not usually thought of as signal processing, such as calculating power-of-2 moduli, performing lower-order bit truncation, or generating random numbers. It can also be useful for manipulating bits in the CCR and mask registers, and could conceivably be used to switch individual signal lines connected to devices in the ESP's external I/O address space.

## [p12] doc p. 8

**OR** performs bit-wise logical OR-ing of each bit of two 24-bit operands. Like AND, OR can be used for many operations not usually thought of as signal processing, as well as for manipulating the CCR and CCM [sic; CMR] bits.

**XOR** performs bit-wise logical EXCLUSIVE OR-ing of each bit in two 24-bit operands. Apart from the same types of logical applications mentioned for AND, OR, XOR can be used to produce the one's complement of a signal by XORing the signal with -1. The resulting 1's complement value is sufficiently close to the true arithmetic negative of the argument for most purposes. This is a more code-efficient method of negation than moving 0 into a register and subtracting a value from it.

**ABS** takes the absolute value of the argument, **A = |B|**. ABS performs the equivalent of a full-wave rectification on bipolar signals. This can be a very useful non-linear signal-processing function for deriving envelope signals and performing level detection. **Negative arguments are negated with One's complement results.** [inferred: i.e. ABS(negative B) = ~B, not -B; so ABS($800000) = $7FFFFF, ABS(-1)=0.]

**MOV** performs the basic data movement operation, **MOVE B to A**. The source and destination operands can be registers anywhere in the ESP's GPR memory space, or any SPR. **MOV does not affect the status bits in the CCR.** Thus moving a register back to itself is a true NOP.

**ASL2** performs an arithmetic shift left of two positions on the operand, corresponding to a multiplication by four, i.e., **A = B * 4**. Saturation will occur if the sign bit changes during the shift operation. Zeros are shifted into the least significant two bits of the word. There is no unsaturating counterpart to this instruction. Left shifting can be useful for normalizing a signal that is known to have been attenuated. ASL2 saves one cycle over the equivalent series of two ADD instructions. ASL2 can also be used as a logical left shift if bits 23 through 21 of the operand are the same.

**ASL8** performs an arithmetic shift left of eight positions on the operand, corresponding to a multiplication by 256, i.e., **A = B * 256**. Saturation will occur if the sign bit changes during the shift. Zeros are shifted into the least significant eight bits of the word.
ASL8 can be useful for normalizing a signal known to have been severely attenuated. ASL8 is a considerable savings over the equivalent series of ADD instructions. ASL8 can also be used as a logical left shift if bits 23 through 15 of the operand are the same.

[inferred: saturation for ASL2/ASL8 = result clipped to $7FFFFF if B positive, $800000 if B negative, whenever any of the shifted-out bits differ from the resulting sign — i.e. B outside the representable range after shift.]

**LS15** performs a left shift of 15 places on the operand, and clears the sign bit, equivalent to **A = B * 32768**. Consistent with the cleared sign bit, no saturation is performed on the result. The overall effect is to take the least significant eight bits and place them in the most significant positive part of the word. Zeros are placed in the low 15 bits of the word. The result is positive, having 8-bit precision.
[inferred: A = (B << 15) & $7FFFFF, i.e. bits 7..0 of B → bits 22..15 of A.]
LS15 is specifically intended to produce coefficients for interpolating between data values in adjacent RAM locations. The low 8 bits in a 24-bit address offset do not contribute to the physical addresses in a 64K word system (since addresses are left-justified) but can be thought of as representing the fractional part of the address. To perform a linear interpolation on RAM data at 2 adjacent memory locations, the difference between the 2 words can be multiplied by this fraction

## [p13] doc p. 9

and summed with the word at the lower address. For system memory configurations larger than 64K (see section 3.4.2 [sic; actually 3.5.2]), the fraction must be suitably masked and shifted farther to the left.

**DIFF** performs the "difference" operation **A = $7FFFFF - B**. DIFF is one instruction cycle more code-efficient than first MOVing the constant $7FFFFF into the A register and performing the equivalent subtraction.
DIFF is intended for generating crossfades between the outputs of delay lines. If B is the coefficient being applied to the output of one delay line, DIFF B,A creates the coefficient A to be applied to the other output to achieve an equal amplitude crossfade. With scaling, DIFF could also be used to generate difference addresses for coefficient look-up tables. **The subtraction is unsaturated.**

> CONFLICT: the ALU Functions table (p11) says DIFF is "with Overflow Protection", but this paragraph says "The subtraction is unsaturated." The condition-code table (p14) groups DIFF with CMP (V set as a plain overflow flag, not footnote (1) "saturation"), which leans toward "unsaturated". For B >= 0 no overflow is possible; for B < 0 the true result exceeds $7FFFFF (e.g. B=$800000 gives $FFFFFF = -1 if unsaturated). Verify against hardware/other sources.

**ASR** performs a one bit position arithmetic right shift, equivalent to **A = B/2**. The sign of the operand is preserved in the result. ASR is intended for situations where simple divide-by-two's might be required in parallel with other Multiplier operations.

**END** is the ALU opcode which signals the end of the ESP microprogram. END is equivalent to one ALU instruction, but **a multiply, or read or write of external RAM or I/O can be performed during the END instruction**. During execution of the END instruction the following actions occur:
- Host access is granted to GPR and microinstruction memory. (sec 5.2)
- The Address Generator's Delay Base Address Counter is updated. (sec 3.4.4 [sic; 3.5.4])

### 3.3.2 Condition Code Register

Associated with the ALU is a Condition Code Register (CCR) whose 6 bits reflect the state of previous ALU operations. The CCR is mapped into the ESP's operand address space as a special purpose register (SPR).

**Condition Code Register Bits**

| Bit | 23 | 22 | 21 | 20 | 19 | 18 |
|---|---|---|---|---|---|---|
| Code | N | C | V | LT | Z | NOT |

(Note: the SPR table on p09 lists CCR as 5 bits wide and CMR as 6 bits wide; here the CCR is said to have "6 bits" including a NOT bit at bit 18. [inferred] The NOT bit is functionally a CMR (mask) bit — "invert skip condition" (see 4.2.1) — so the same bit layout (23..18 = N,C,V,LT,Z,NOT) presumably applies to the CMR, while the CCR proper holds the 5 flags N,C,V,LT,Z in bits 23..19.)

The CCR contains 5 flags which are derived in most ALU operations from basic arithmetic results.

| Flag | Meaning |
|---|---|
| Z | zero result |
| C | carry (or no-borrow) |
| N | negative result |
| V | result overflow |
| LT | less than |

**LT is computed as LT = ( V AND NOT N ) OR ( NOT V AND N ).** [i.e. LT = N XOR V] LT is necessary because no true combination of the other flags can determine a "less-than" condition. More details are available in Section 4.2.2.

[Ambiguity: C is defined as "carry (or no-borrow)", yet the p28 table uses C alone for "Lower than (unsigned)" — which corresponds to C = borrow after subtraction. The two statements conflict for subtract/compare; emulator authors must decide (or test) the polarity of C on SUB/CMP.]

## [p14] doc p. 10

### 3.3.3 Setting Condition Codes (Pipeline Considerations)

All ALU operations except MOV and END will set bits in the CCR based on their results (unless the operation occurred on a conditionally unexecuted step). Section 4.1.2 contains a complete description of the computation performed by each ALU operation. **CCR bits become valid at the end of the complete ALU instruction cycle. Thus, they are available for reading by the next ALU operation, but not by the next Multiplier operation, just like a GPR** (see Section 4.3.1 for details of the ESP execution cycle). CCR bits will be active for the conditional execution of both the ALU and Multiplier phases of the subsequent microinstruction cycle.

**Condition Code Computation**

(The scanned table is rotated slightly; values read from an upscaled crop with row alignment checked against the skew.)

| ALU OPERATION | N | C | V | LT | Z |
|---|---|---|---|---|---|
| ADD,SUB | X | X | (1) | X | X |
| ADDU,SUBU | X | X | X | X | X |
| CMP,DIFF | X | X | X | X | X |
| AND,OR,XOR | X | - | - | - | X |
| ABS | 0 | (2) | - | - | - |
| ASL2,ASL8 | X | - | (1) | - | X |
| LS15 | 0 | (3) | - | - | - |
| ASR | X | (4) | - | - | - |
| MOV | - | - | - | - | - |
| END, NOP | - | - | - | - | - |

- (-) Indicates that this flag is not altered by the operation.
- (1) V indicates whether saturation as opposed to overflow occurred.
- (2) C receives the sign bit before the ABS operation.
- (3) C receives the shifted bit that was cleared at the sign position.
- (4) C receives the LS bit that was shifted right out of the operand.
- [inferred] X = flag set/cleared according to the result; 0 = flag forced to 0.

[Reading note: ABS, LS15 and ASR show Z as "-" (not altered); ABS not touching Z is surprising but is how it reads. Because of scan skew, the ABS/ASL2 cells in the Z column are the least certain: read as ABS Z = "-", ASL2/ASL8 Z = "X".]

Neither the MOV nor the END instructions affect condition code bits. (see Section 5.2 for a full description of END cycles).
The MOV instruction is a special case for two reasons:
1. ALU NOP instructions are actually assembled as a MOV instruction where the source and destination are the same GPR.
2. Since the CCR is a writeable register, you may move a value into it (after masking certain bits to force particular conditions, for instance). Preventing MOV from affecting CCR bits resolves any ambiguity as to the state of the CCR following the completion of the MOV operation.

## [p15] doc p. 11

### 3.4 Multiplier

The Multiplier multiplies a 24 bit multiplicand by a 24 bit multiplier and produces a 48 bit product during every instruction cycle. **If the MAC bit in the Operand Select Field is set** (see section 4.1.5) [sic — the MAC bit is bit 6 of the instruction, not part of the Operand Select field], **the product of the multiply will be added to the previous contents of the accumulator.** This previous value may have been preloaded, or was the result of a previous multiply or accumulation.

24-bit operands for the Multiplier are fetched on the X and Y busses from GPRs specified by the C and D operands or from the DIL register and the GPR specified by D. **The most significant 24 bits of the product appear on the Z bus and will be written back to the C operand address or possibly to the DOL FIFO.** This corresponds to a "fractional" mode of multiplication.

The least significant 24 bits of the product are also available to the ALU in a SPR register, MACL. Thus the Multiplier can function as an integer multiplier as well as a fractional multiplier. In fact, both MACH and MACL, the upper and lower halves of the multiplier/accumulator, are SPRs registers and can be preloaded and recovered by any ALU operations.

**The product will always be normalized, that is, shifted left, as either one or two bits prior to entering the accumulator, as determined by the Product Shift Mode bit in SIGREG.** The reason for this is the following.

A normal 24 x 24 bit hardware multiplication will result in a 48 bit product with 2 sign bits. By left shifting the result by 1 bit, we can strip the extra sign bit and maintain true 24 bit two's complement data. The left shift by 2 operation is very useful as a range extension. This means that the normal +/-1 range system can be extended to a +/-2 system and by using the left shift by 2 function for the multiplier output the 2 extra sign bits that are created are effectively removed.

**The Multiplier's accumulator contains two "guard" or extension bits** which allows the intermediate partial sums in a summation of products to far exceed the ESP's normal range of values. As long as subsequent summations cause the accumulator to underflow back into the least significant 48 bits, the final result output from the multiplier will be an accurate representation of the sums of products. **Otherwise, the Multiplier will substitute a saturated value for the overflowed result when it is output on the Z bus or read on the X or Y busses as SPRs.**

If an accumulation at the current instruction is called for by a set MAC bit but the instruction is skipped through the conditional execution mechanism, no accumulation will take place. No output will result from the MAC, and its accumulator will retain the sum from the previous un-skipped step.

[inferred emulator model, not printed: acc is 50-bit signed (48 + 2 guard bits). product = (C * D) << shift, where shift = 1 if SIGREG bit22 = 1, shift = 2 if bit22 = 0. acc = MAC ? acc + product : product. Z-bus output = acc bits 47..24 (MACH), MACL = bits 23..0, saturated to the 48-bit signed range if the guard bits indicate overflow. No rounding is mentioned anywhere on these pages — truncation is implied. Not stated: whether the saturated value is 24-bit $7FFFFF/$800000 on Z, nor what MACL reads when saturated.]

## [p16] doc p. 12

### 3.5 Address Generator

The address generator provides **up to 24 bits of address** for the RAM interface every instruction cycle. It has three main addressing modes which are selected by the 3 bit RAM Control field in the microinstruction (see section 4.1.6). The Address Generator can compute an address for one of the following operations on each instruction cycle:
- table "A" reads and writes
- table "B" reads
- delay line reads and writes
- external I/O reads and writes

The computed address comes out multiplexed in row and column form on the RAM data/address bus. The Address Generator uses the RAM Control field to drive the ESP's R/W pin and the RAS and CAS strobes. It also controls an I/O Space Select pin which is used to select between ordinary RAM accesses and accesses to other devices on the RAM bus.

### 3.5.1 Address Generator Architecture

The principle elements of the Address generator are:
- A Delay Base Address Counter (**DBASE**)
- A Delay Length register and comparator (**DLENGTH**)
- Two Table Base registers (**ABASE** and **BBASE**)
- A Memory size register to set delay increment (**MEMSIZ**)
- The GPR's used as address offsets to memory (**OFFSET GPR**)
- An offset adder

The DBASE Counter is the key to automatically maintaining signal delay lines in memory. All delay line accesses are performed relative to this recirculating decrementing counter, which configures RAM memory as a large circular buffer.

The DLENGTH register specifies how much of RAM should be devoted to delay line storage. Memory above the DLENGTH address will not be accessed by delay line read/writes as long as OFFSET GPR's are not greater than DLENGTH.

The Table Base registers ABASE and BBASE are used to provide base addresses for table-relative addressing of fixed data stored in RAM. The two registers allow two independent data areas to be established in memory.

The MEMSIZ register holds a power of 2 value used to define which address bits will used to address external RAM. The value written to this register is a function of the size of the external RAM in the system. It also sets the decrement value for the delay base counter (DBASE) and is a mask for delay address range comparisons.

The offset adder, the delay modulo comparator, and the delay base counter (DBASE) are the actual computational elements in the Address Generator. The method they use to compute physical addresses is detailed below.

The DBASE, DLENGTH, ABASE, BBASE, and MEMSIZ registers all appear in the GPR memory map as special purpose **20-bit** registers which can be written to and read from by ESP microinstructions or the Host. Normally these will only be written once

## [p17] doc p. 13

during initialization procedures, but may be modified later, as well. (Note that there may be pipelining ramifications to changing these registers while programs are running.)

### 3.5.2 Selecting the Addressing range

The architecture of the Address Generator is based on the fact that **all addresses in GPR registers are left-justified**, a convention which has several advantages. One is that address bits to the right of the binary point serving as interpolation coefficients retain the maximum possible resolution. Another is that addresses can be stored directly in RAM without realignment to the left-justified 16-bit wide RAM data bus.

All Address Generator registers and data paths are **20-bits wide to accommodate up to 1 mega-word of RAM**, this was done for future expansion. Because of the left-justified convention, the least significant physical address bit moves to the left within the ESP's standard 24-bit word when smaller amounts of memory are attached to the system. For example, if 16K words of RAM are used in a system, 14 address bits are needed. In this case, the least significant address bit would be (24-14=) bit 10. For 64K words, 16 address bits are needed, so the LSB is (24-16=) bit 8. The position of the least significant address bit is used by the Address Generator to determine how much to decrement the 20-bit wide DBASE pointer each sample period, and how many address bits to compare during delay address computation. The value in the MEMSIZ register is used for both computations.

**The MEMSIZ register must be loaded with a 1 bit for each corresponding unused address bit.** For instance in a 64K word system 16 address bits are used, so the leftmost 16 MEMSIZ bits should be 0 and the rest should be 1. In this case the MEMSIZ value would be **$0000FF**. In a 256K word system, 18 addresses are needed so **$00003F** would be used.

[inferred: although MEMSIZ is called a 20-bit register, the examples are written as 24-bit values aligned with GPR bits 23..0: for 64K words, GPR bits 23..8 are the address and MEMSIZ=$0000FF marks bits 7..0 as unused (fractional). The 20-bit address registers presumably hold GPR bits 23..4. The DBASE per-sample decrement is one unit at the LSB position, i.e. MEMSIZ+1 in GPR-bit units. Exact bit alignment of the 20-bit registers inside the 24-bit word is NOT stated on these pages.]

### 3.5.3 RAM Addressing modes

There are three basic RAM addressing modes which can be specified by the 3 bit RAM control field of each microinstruction. These are Delay Line, Table, and External I/O addressing. See Section 4.1.6 for the control fields for RAM addressing.

**Delay Line Addressing**

This is delay counter-relative addressing, and is used to access signal delay lines which have been established in memory. To create a delay line, data is written at one address but read from an address higher in the memory space. Physical delay line addresses are generated by adding the relative address offsets fetched from GPR memory to the DBASE pointer. As the pointer decrements once each sample period, the read address eventually equals the address at which the write occurred. The distance in memory locations between the read and the write addresses equals the length of the delay line in sample periods. In the ESP, multiple delay lines are implemented starting at physical address 0 and extending up to and including the address contained in the DLENGTH register. Memory above the DLENGTH address is normally not accessed by delay lines, and can be used for tables A and B, or for other data.

## [p18] doc p. 14

**Table Addressing**

This is table base-relative addressing, and is used to access data stored at fixed locations in RAM. Physical table addresses are computed by adding relative address offsets (OFFSET GPR) to the contents of one of the two Table Base address registers. Positive or negative offsets can be used with the table registers, allowing them to define the high boundaries, low boundaries, or midpoints of data tables.

No restriction is placed on the contents of the Table Base registers, allowing data tables to be placed anywhere in the 20-bit RAM address space. **The values in these registers are not masked by MEMSIZ.** Tables should be placed above the address stored in the DLENGTH register if delay lines are used. Note that although only 2 table base registers are provided in hardware, appropriate reassignment of the table base registers by the ESP microprogram can provide an arbitrary number of tables.

**External I/O Space addressing**

This is the ESP's third basic addressing mode, used to access devices in an external I/O memory space. This memory area is distinguished from RAM memory space by the assertion of the I/O select pin instead of CAS during an otherwise ordinary memory cycle. It is intended that external hardware devices which require parallel data be interfaced to the ESP by placing them in I/O space. These devices, which might be ADCs, DACs, memory devices, or even other ESP chips, are assumed to be able to decode their own addresses from the ESP's multiplexed data/address bus with the help of the I/O Select pin. (see section 3.5.2) **I/O space is always addressed absolutely**, that is, the address in the offset GPR becomes the direct physical address multiplexed onto the ESP's data/address bus.

### 3.5.4 Physical Address Computation

At the beginning of each instruction cycle, **a 24-bit word is fetched from GPR memory along with the microinstruction at that address.** In delay line and table addressing modes this word represents the relative address offset (OFFSET GPR) from the appropriate ABASE, BBASE, or DBASE counter. In I/O Space addressing mode it represents the absolute address of a device in the external I/O Space. The physical address computation thus depends on the addressing mode specified by the RAM Control field in each microinstruction.

[inferred: the OFFSET GPR used is GPR[n] for the instruction word at address n (n = 0..159); but see 4.1.6 / 4.3.4 — the RAM control field is "pipelined away" into a different instruction word, so the offset GPR index relative to the instruction issuing the read must be checked in 4.3.4 (part 2).]

**Delay Line Addressing Mode**

This requires the most complex calculation of the three addressing modes. First, the offset is added to the DBASE counter. The sum is then compared to the DLENGTH register. If the sum of DBASE + offset is greater than DLENGTH, then DLENGTH is subtracted from the sum, and the resulting difference is used as the physical address. Otherwise, the sum is used as the physical address.

The following steps outline the process:
1. OFFSET GPR + DBASE counter = ASUM.
2. If ASUM ≤ DLENGTH then ASUM = RAM physical address
   else (ASUM - DLENGTH) modulo (DLENGTH) = RAM physical address.

[Note: as printed, the wrap subtracts DLENGTH (not DLENGTH+1) even though the delay area is "from 0 up to and including DLENGTH" and DBASE reloads to DLENGTH. That gives a period of DLENGTH+1 for DBASE but a wrap of DLENGTH for the sum — a possible off-by-one/documentation imprecision. Treat with care.]

## [p19] doc p. 15

The actual address presented to the RAM is masked by the MEMSIZ register.

In addition to the address calculation performed each instruction cycle, the DBASE counter must be updated once each sample period (microprogram iteration). During each END cycle the Base Address Counter is decremented

    DBASE = DBASE - 1,

the equivalent of one RAM address as determined by MEMSIZ. **When DBASE decrements past zero it is loaded with the value held in the DLENGTH register.**

**Note:** the DBASE counter and DLENGTH register must be carefully initialized when the microprogram is loaded in order to work properly with data tables.

They must be set up before data tables are established to prevent the program's delay lines from overwriting table data. More details on initialization are covered in section 5.4.3.

**Table addressing mode**

Tables can access all of physical memory. This requires less computation than for delay line addressing, since the offset is added directly to the table base register specified in the RAM control field.

    OFFSET GPR + ABASE (or BBASE) = RAM physical address

No boundary comparison or limiting is performed on the resulting address sum, and no overflow detection or correction is performed. Addresses are calculated identically for the ABASE and BBASE table base registers, **even though the RAM control field does not allow for ESP writes to the Table B address space.**

**I/O addressing mode**

I/O addressing actually requires no address computation at all, as the address offset fetched from GPR memory becomes the physical address in I/O Space.

### 3.6 Busses

#### 3.6.1 Internal Operand Busses

The ESP contains three major data busses, labeled X, Y and Z. The X and Y buses are used to fetch operands for the ALU and the Multiplier and are 24 bits wide. **The GPR specified as the ALU's A operand is always fetched on the X bus, and the Multiplier's C operand is always fetched on the X bus. Likewise, GPRs specified as the ALU's B operand or the Multiplier's D operand are always fetched on the Y bus.** The Z ia [sic] a 24 bit wide bus used to deliver results back to the A and C GPRs and/or the DOL FIFO from the outputs of the ALU and the Multiplier.

Whether the Z bus writes back to the A and C GPRS and/or to the DOL FIFO depends on the "addressing mode" specified by the operand select field of the microinstruction. Operand select modes are covered in detail in section 4.1.3.

The Multiplier and ALU share the X, Y and Z busses by trading off their use on different phases of the microinstruction cycle. Bus cycle timing is covered in more detail in section 4.3.1.

## [p20] doc p. 16

All of the ESP's standard GPR register memory can be fetched equivalently over the X and Y busses. GPRs can be freely selected as A, B, C or D operands.

#### 3.6.2 External RAM Busses

**RAM Data/Address Bus**

The RAM data/address bus is controlled by the ESP and allows access to both external memory (RAM) and to externally addressed devices. This bus consists of **16 bi-directional pins** used to pass 16 bits of data between external RAM and the DIL/DOL registers, and **20 bits of address in 2 phases** from the address ALU to external RAM. The 20 address bits are divided into row addresses and column addresses. These addresses can be used directly by dynamic RAM, or latched externally for static RAM. The state of this bus is determined by the RAM Bus Control Lines (below). The RAM data/address bus is multiplexed as follows...

**GPR bits to DRAM mapping** (verified from an upscaled crop)

| RAM Data/Address | ESP GPR Data Bit | RAM ROW Address | RAM COL Address |
|---|---|---|---|
| 15 | 23 | 15 | 23 |
| 14 | 22 | 14 | 22 |
| 13 | 21 | 13 | 21 |
| 12 | 20 | 12 | 20 |
| 11 | 19 | 11 | 19 |
| 10 | 18 | 10 | 18 |
| 9 | 17 | 9 | 17 |
| 8 | 16 | 8 | 16 |
| 7 | 15 | 6 | 7 |
| 6 | 14 | 4 | 5 |
| 5 | 13 | | |
| 4 | 12 | | |
| 3 | 11 | | |
| 2 | 10 | | |
| 1 | 9 | | |
| 0 | 8 | | |

[inferred interpretation: "ESP GPR Data Bit" = data mapping (pin n ↔ GPR/DIL/DOL bit n+8; data is left-justified in bits 23..8). ROW/COL columns = which GPR address-word bit appears on each pin during the row and column phases: row phase pins 15..8 = GPR bits 15..8; column phase pins 15..8 = GPR bits 23..16; for larger memories pin 7 carries bit 6 (row) / bit 7 (col) and pin 6 carries bit 4 (row) / bit 5 (col). So a 64K system uses GPR bits 23..8 (16 bits) and a 1M-word system uses bits 23..4 (20 bits). Pins 5..0 carry no address. Matches the text "lower 8 bits presented as the RAM row address". For an emulator only the linear address (GPR bits 23..4, masked by MEMSIZ) matters, not the row/col multiplexing.]

Note that as more RAM is added to the system the address bit are increased by appending to the lower portion of the bus. To understand the above chart a little further explanation is in order. The interface to DRAM is accomplished through the 16 lines labeled RAM Data/Address. These lines are multiplexed three ways:
1. Row Address
2. Column Address
3. Data Input or Output

The minimum DRAM expected to be used in a system is 64K. Therefore the address presented to the external DRAM is GPR data [23:8], with the lower 8 bits presented as the RAM row address. The 16 bit data is always returned to the ESP DIL left justified to bits [23:8]. As the amount of RAM is increased additional bits are appended to the row

## [p21] doc p. 17

and column addresses. These additional GPR bits [7:4] are distributed to the row and column lines as shown in the table.

**RAM Bus Control Lines**

| Signal | Description |
|---|---|
| RAS | active low row address strobe |
| GATE | active high column latch strobe |
| CAS | active low column address and data in strobe for RAM |
| IOSEL | active low data strobe for external devices (RAM, I/O, etc.) |
| DWEN | active low write enable |

**Memory vs. I/O Space**

The ESP can swap its external data space between addressing RAM and addressing other external devices, such as I/O. These external devices can conceivably be anything from parallel CODEC interfaces to static memory devices to other ESP chips. Bus cycles intended to address the I/O space are distinguished from normal RAM cycles by the assertion of the IOSEL pin instead of the CAS pin. The RAS, GATE, and DWEN pins adhere to their normal timing. Timing of IOSEL for read and write is identical to a CAS read cycle. But CAS is asserted later for write cycles, and IOSEL is not.

Addresses in I/O space are calculated differently by the ESP's Address Generator than RAM delay line or data table addresses (see section 3.4.4 [sic; 3.5.4]). Address offsets fetched from GPRs essentially bypass the Address Generator adders and limiters and go directly out onto the address lines. Thus devices in I/O space must be accessed at their absolute physical addresses. Normal read and write timing considerations apply to I/O space devices however, since their data pass through the DIL and DOL FIFO just like ordinary RAM data (see 3.5.3).

### 3.5.3 [sic — "3.6.3" in the TOC] RAM Data Interface

**Data Input Latch (DIL)**

Data read from RAM or external I/O space enters the ESP and is latched into a Special Purpose Register (SPR) called the Data Input Latch (DIL). **The DIL is 16 bits wide, left justified, so programs accessing the DIL will see the least significant 8 bits as 0.**

Data is strobed into the DIL based on the value of the RAM Control Field in the microinstruction word (see 4.1.6). **Data latched into the DIL by a read operation will remain intact until another read operation is explicitly called for by this field.** This latency allows an ESP assembler to make certain optimizations in the scheduling of reads and writes, especially when duplicate read operations are specified.

**Data Output Latch (DOL) and FIFO**

ALU or Multiplier instructions which use RAM delay lines, tables, or the external I/O space as a destination, send their results to a **2 word deep output FIFO** known as the Data Output Latch (DOL). It's output is multiplexed onto the RAM bus during a write cycle. Like the DIL, it is also **16 bits wide and is left justified** [inferred: DOL captures Z-bus bits 23..8; bits 7..0 are dropped (truncation, no rounding mentioned)].

## [p22] doc p. 18

The DOL FIFO can hold up to 2 words of RAM write data. This was intended as a means for holding data until write cycles become available as read operations are normally considered to have priority over writes. Prioritizing read operations over writes is a valid approach because most musical signal processing algorithms require many more delay line reads than write operations. Read operands must be fetched and ready for use at particular points in the signal processing microprogram, whereas the great majority of write operations are insensitive to the specific time at which their data actually is stored in memory (provided that the write occurs sometime during the same sample period). This fact allows the ESP to hold off relatively infrequent write operations while more urgent read operations are tying up the RAM bus. The output FIFO allows up to 2 write data words to be buffered up while continuous read operations are occurring.

The DOL FIFO is written to by specifying it as an ALU or multiplier destination in a microinstruction. It is unloaded by specifying a RAM write or dump-FIFO operation in a microinstruction. **Two DOL writes will fill both words. Two RAM writes will empty both words. Writing an empty FIFO to RAM will cause the last word written to the DOL to be written to RAM. Overstuffing the FIFO will cause the oldest data to be "bumped out", preserving the last two words written.**

Microcode assemblers are capable of scheduling RAM reads and writes by assembling address offsets and RAM control fields at specific microcode steps (see 4.1.6). They may simulate the run-time execution of the microprogram to a certain degree to know when RAM cycles will be needed by read operations and when they will be free for writes. They should flag the programmer if the DOL FIFO is going to overflow at run-time from a combination of too many write operations and insufficient free RAM cycles between read operations.

Note: **Halting of the microprogram (by execution of an END operation while HALT is asserted) will reset the FIFO pointers.** This has the effect of emptying the FIFO, but a subsequent RAM write may not output the last word written to the FIFO. For this reason, DOL writes and RAM writes cannot span sample boundaries.

[inferred emulator model: DOL = 2-entry queue. Push: if full, drop oldest. RAM write (or dump) pops oldest; a RAM write with the FIFO empty outputs the most recently pushed value; dump with FIFO empty does nothing (p27). Reset to empty on halt-at-END.]

## 4. Operation

### 4.1 Microinstruction Word

The microinstruction word is **48 bits wide. Presently, only 45 bits are used.** It is broken down into 4 eight-bit operand fields, a 4 bit ALU opcode field, a 4 bit operand select field, 2 single control bits, and a 3 bit RAM Control field. The three least significant bits are unused.

**Microinstruction Word Format**

| Bit # | 47⇒40 | 39⇒32 | 31⇒24 | 23⇒16 | 15⇒12 | 11⇒8 | 7 | 6 | 5⇒3 | 2⇒0 |
|---|---|---|---|---|---|---|---|---|---|---|
| Func. | Mul Op D | Mult Op C | ALU Op B | ALU Op A | ALU Opcode | Operand Select | Skip | MAC | RAM Cntrl | Unsed [sic] (0's) |

Field summary:
- bits 47..40: D — multiplier operand D (8-bit register address)
- bits 39..32: C — multiplier operand C, also the multiplier destination (8-bit)
- bits 31..24: B — ALU operand B (8-bit)
- bits 23..16: A — ALU operand A, also the ALU destination (8-bit)
- bits 15..12: ALU opcode (4 bits)
- bits 11..8: Operand Select (4 bits)
- bit 7: Skip
- bit 6: MAC
- bits 5..3: RAM Control (3 bits; B5 B4 B3)
- bits 2..0: unused, 0

## [p23] doc p. 19

#### 4.1.1 Operand fields

The operand fields comprising bits 47 through 16 hold the addresses of the GPRs or special registers specified as the A, B, C, and D operands used in ALU and Multiplier operations. Each address is a number between 0 and 255.

**Addresses from 0 through 191 indicate memory locations occupied by normal 24-bit wide GPRs. Addresses greater than 191 indicate special locations or control registers.** They are numbered in descending order beginning at address 255 to allow the GPR array to expand upward in future implementations. Section 3.1.1 [sic; the table is in 3.2] contains a table summarizing these special locations.

(Addresses 192..233 / $C0..$E9 are not described anywhere on pages 1–28.)

#### 4.1.2 ALU Opcode field

Microinstruction word bits 15 through 12 specify one of sixteen possible ALU operations, or opcodes. These operations are described in detail in section 3.2.1 [sic; 3.3.1], and are summarized below with their opcodes.

**ALU Operation Mnemonics and Opcodes**

| MNEMONIC | OPERANDS | OPCODE | OPERATION |
|---|---|---|---|
| ADD | B, A | 0 | A = A + B with saturation arithmetic * |
| SUB | B, A | 1 | A = A - B with saturation arithmetic |
| ADDU | B, A | 2 | A = A + B |
| SUBU | B, A | 3 | A = A - B |
| CMP | B, A | 4 | A - B (compare A to B) |
| AND | B, A | 5 | A = A and B (bitwise) |
| OR | B, A | 6 | A = A or B (bitwise) |
| XOR | B, A | 7 | A = A xor B (bitwise) |
| ABS | B, A | 8 | A = \| B \| |
| MOV | B, A | 9 | A = B |
| ASL2 | B, A | A | A = B left shifted 2, saturated |
| ASL8 | B, A | B | A = B left shifted 8, saturated |
| LS15 | B, A | C | A = B left shifted 15, sign bit clear |
| DIFF | B, A | D | A = $7FFFFF - B |
| ASR | B, A | E | A = B right shifted 1; sign extended |
| END | | F | PC = 0 restart program |

(The "*" after ADD's description has no corresponding footnote anywhere on this page.)

Section 3.3.3 lists the bits in the Condition Code Register (CCR) which are affected by each of the above opcodes.

#### 4.1.3 Operand Select Modes

**ALU Operand/Bus Mapping**

For ALU instructions not accessing the DOL FIFO, the GPR specified as the A operand is the implied destination register for the operation, and is the GPR written back into by the Z bus. Thus in 2-operand instructions like ADD or XOR, the A and B

## [p24] doc p. 20

operands are operated on and the results are placed in A. In 1-operand instructions (MOV, ABS, etc.), the result goes into A, but only the B register needs to be fetched. (Note that the A and B operand fields may contain identical addresses specifying the same physical register.)

The RAM Data Input Latch (DIL) also can be fetched over the X or Y busses. But since it is read-only, sharing the same address as the write-only MEMSIZ GPR, it must not be specified as a data destination. **In 2-operand instructions, the DIL stands in for the A operand as one source, with the B GPR as the other. The result is written to the A operand, as usual. In this case the DIL must be fetched on the X bus (as A normally would be). In 1-operand instructions, however, the DIL stands in for the B operand, and thus must be fetched on the Y bus.** Again, results are written back to A. The situation is summarized below.

**ALU to Internal Bus Mapping**

| Instruction Type | X BUS | Y BUS | Z BUS |
|---|---|---|---|
| (i) 1-operand | *** | B reg. | A reg. |
| (ii) 1-operand | *** | DIL | A reg. |
| (iii) 2-operand | A reg. | B reg. | A reg. |
| (iv) 2-operand | DIL | B reg. | A reg. |

This mapping provides the most regular bus/operand connections and at the same time makes best use of the operand fields in the microinstruction word. In particular, B inputs to the ALU are always fetched on the Y bus, and A results are always on X and Z (cases i, iii, and iv). Also, the provision for selecting DIL on either X or Y allows DIL to be used effectively in 1-operand instructions (ii) and in addition yields a very powerful 3-operand addressing mechanism (iv). **NOTE: For hardware reasons, when assembling 1-operand DIL instructions (case ii), the assembler must put the DIL's GPR address into the B operand field of the instruction word. This is the only time that DIL must be treated as a GPR.**

[inferred: case (ii) is therefore not selected by the operand-select field but simply by B field = $F4 (DIL). Case (iv) is selected by operand-select values 9..F ("ALU Source = DIL"), where the X-bus ALU input is DIL instead of reg A and the A field names only the destination. In operand-select modes 9..F with a 1-operand opcode, the DIL-on-X has no effect since 1-operand ops only use Y/B.]

**Multiplier Operand/Bus Mapping**

Bus/operand mapping is much more straightforward when considering the multiplier. This is due to the fact that it always receives two operands, and that its operation is orthogonal with respect to its two 24-bit inputs.

Thus for the multiplier, the D register always comes in on the Y bus, with the X bus being swapped between the C operand and DIL for those operations that require it. **The result is always sent back to the C operand on the Z bus.** Using DIL as an operand offers a 3-operand capability in those situations that might require it.

**Multiplier Bus Mapping**

| Multiplier Mapping | X BUS | Y BUS | Z BUS |
|---|---|---|---|
| (i) Non-DIL | C reg | D reg | C reg |
| (ii) Using DIL | DIL | D reg | C reg |

## [p25] doc p. 21

**Operand Select Field**

The sources and destinations of the ALU and Multiplier operations are controlled by the operand and operand select fields of the instruction. The operand select field can be thought of as representing the operand addressing mode for a particular instruction cycle.

In general, the ALU operates on operands A and B and returns its result to A. The Multiplier operates on C and D and puts its results in C. These selections are overridden, however, by specifying the source and/or destination registers to be the DIL register and/or DOL FIFO which are the interfaces to external RAM. The various combinations of A, B, C, D, DIL and the DOL FIFO are encoded into the operand select field, as shown in the table below.

**Operand Select Field Encoding** (bits 11..8; verified from an upscaled crop)

| Operand Field | ALU Source | ALU Dest. | MUL Source | MUL DEST | Example (ALU) | Example (MUL) |
|---|---|---|---|---|---|---|
| 0 | A Reg | A Reg | C Reg | C Reg | ADD B,A >A | D X C >C |
| 1 | A Reg | A Reg | C Reg | FIFO | ADD B,A >A | D X C >Delay |
| 2 | A Reg | A Reg | C Reg | C/FIFO | ADD B,A >A | D X C >C,Delay |
| 3 | A Reg | A Reg | DIL | C Reg | ADD B,A >A | D X Delay >C |
| 4 | A Reg | A Reg | DIL | C/FIFO | ADD B,A >A | D X Delay >C,Delay |
| 5 | A Reg | FIFO | C Reg | C Reg | ADD B,A >Delay | D X C >C |
| 6 | A Reg | FIFO | DIL | C Reg | ADD B,A >Delay | D X Delay >C |
| 7 | A Reg | A/FIFO | C Reg | C Reg | ADD B,A >A,Delay | D X C >C |
| 8 | A Reg | A/FIFO | DIL | C Reg | ADD B,A >A,Delay | D X Delay >C |
| 9 | DIL | A Reg | C Reg | C Reg | ADD B,Delay >A | D X C >C |
| A | DIL | A Reg | C Reg | FIFO | ADD B,Delay >A | D X C >Delay |
| B | DIL | A Reg | C Reg | C/FIFO | ADD B,Delay >A | D X C >C,Delay |
| C | DIL | A Reg | DIL | C Reg | ADD B,Delay >A | D X Delay >C |
| D | DIL | A Reg | DIL | C/FIFO | ADD B,Delay >A,Delay [sic] | D X Delay >C,Delay |
| E | DIL | A/FIFO | C Reg | C Reg | ADD B,Delay >A,Delay | D X C >C |
| F | DIL | A/FIFO | DIL | C Reg | ADD B,Delay >A,Delay | D X Delay >C |

Notes:
- "ALU Source" = what the ALU's A-side (X bus) input is: register A or DIL. The B input is always register B (Y bus).
- "MUL Source" = what the multiplier's C-side (X bus) input is: register C or DIL. D is always register D (Y bus).
- "FIFO" = DOL FIFO only; "A/FIFO" = both reg A and DOL FIFO; "C/FIFO" = both reg C and DOL FIFO.
- "Delay" in example syntax denotes DIL (as source) / DOL FIFO (as destination), written as if a delay line access.
- Row D: the ALU example reads "ADD B,Delay >A,Delay" although the ALU Dest column says "A Reg" (the FIFO destination is the multiplier's, C/FIFO). This looks like a typo in the example column; the column entries are authoritative [inferred].
- Observations [inferred]: at most one of ALU/MUL writes the FIFO in each mode; no mode has both ALU and MUL destined for the FIFO. No mode lets ALU A-source = DIL while ALU writes to FIFO-only (only A/FIFO in E/F).

### 4.1.4 'No output' Multiplier Destinations

In certain circumstances it can be advantageous to multiply the C and D operands and hold the result in the Multiplier's accumulator without overwriting the C register (because we'd like to preserve C's original value) by writing to a dummy RAM location (a waste of RAM bandwidth). The situation might be represented by

    MUL C,D > MAC,

where MAC means that the result should just sit in the Multiplier's accumulator for use on a subsequent step. If an assembler encounters this situation it should generate an operand select code for a write to the DOL FIFO input (field value $1 or $A), but the RAM control flags that signal a RAM write (see Section 4.1.6) will not be generated.

## [p26] doc p. 22

Instead, a flag will be set to discard the FIFO data and preserve that RAM cycle for a read operation.

### 4.1.5 Skip and MAC bits

**The Skip bit, bit 7**, indicates whether the microinstruction is 'skippable', or eligible for conditional execution. See section 4.2 for complete details on flow control and the conditional execution of instructions.

**The MAC bit, bit 6**, controls whether the Multiplier should perform an accumulate as well as a multiply operation during this instruction cycle. The MAC bit is in effect an opcode for the multiplier.

### 4.1.6 RAM Control field

Three bits in the instruction word are used to control the RAM data and address busses. These busses can access I/O devices and other ESP chips as well as various sizes of RAM arrays.

The value of the RAM control field is closely tied to the operand select mode (see 4.1.3) since it is derived in part from the combination of sources and destinations required during an instruction cycle. **In general the RAM control field is pipelined away into a different microinstruction word than the operand select field to which it corresponds.** Details of the RAM pipelining and its effect on the pipelining of microinstruction fields can be found in Section 4.3.4.

**Bit 3 in the RAM control field directly maps to the FIFO output strobe. When it is set, the FIFO outputs its next queued data word. Bit 5 and Bit 4 are memory select bits.** These control RAM access by enabling CAS and the I/O select pin. The various combinations of the FIFO and memory select bits are decoded according to the following table.

**RAM Control Field Encoding** (verified from an upscaled crop)

| RAM Adressing [sic] Mode — Function | B5 | B4 | B3 | CAS | IOSEL | DWEN |
|---|---|---|---|---|---|---|
| read delay line | 0 | 0 | 0 | X | | |
| write delay line | 0 | 0 | 1 | X | | X |
| read table A | 0 | 1 | 0 | X | | |
| write table A | 0 | 1 | 1 | X | | X |
| read delay/dump FIFO | 1 | 0 | 1 | X | | |
| read table B | 1 | 0 | 0 | X | | |
| read from I/O space | 1 | 1 | 0 | | X | |
| write to I/O space | 1 | 1 | 1 | | X | X |

(Column group headings: "Control Field" = B5 B4 B3; "Active ESP Pins" = CAS, IOSEL, DWEN. Note the table row order lists 101 before 100.)

Decoded by value [inferred, from the table]:

| Value (B5..B3) | Function | Address mode |
|---|---|---|
| 0 (000) | read delay line | DBASE + offset, modulo DLENGTH |
| 1 (001) | write delay line | DBASE + offset, modulo DLENGTH |
| 2 (010) | read table A | ABASE + offset |
| 3 (011) | write table A | ABASE + offset |
| 4 (100) | read table B | BBASE + offset |
| 5 (101) | read delay / dump FIFO | DBASE + offset (read), FIFO word discarded |
| 6 (110) | read from I/O space | offset (absolute) |
| 7 (111) | write to I/O space | offset (absolute) |

[Note: there is no "no RAM access" code — every instruction performs some RAM cycle (a read if nothing else) [inferred]. B3=1 pops the FIFO; with B5B4 = 10 the popped word is discarded instead of written (DWEN not asserted) while a delay-line read is performed.]

The read delay/dump FIFO mode provides a way of discarding a word in the DOL FIFO (see Section 4.1.4). Dummy writes to the FIFO are used when a result of the Multiplier is not desired except in the Multiplier's accumulator. Other than this state, the FIFO bit and the read/write pin would be the same function. If the FIFO is holding 2 output words, the first word written is discarded, leaving it holding only the last word

## [p27] doc p. 23

written. If the FIFO is holding only 1 word, that word is discarded. **Dumping an empty FIFO has no effect.**

### 4.2 Conditional Execution

#### 4.2.1 Basic Conditional Mechanism

The ESP has a relatively simple mechanism that allows individual steps of its microcode to be conditionally executed, giving a modicum of execution flow control within signal processing microprograms. Any microinstruction step may be flagged by the programmer as 'skippable', meaning that it will not be executed when the ALU's Condition Code Register (CCR) is in a certain state. This state constitutes the skip condition, and is specified by loading a particular bit pattern into the Condition Mask Register (CMR). By loading various values into the CMR, instructions can be skipped according to a variety of different conditions. The conditions result from combinations of the 5 basic CCR flags N, Z, C, V and LT and their inverses. (See section 3.3.2).

When the Mask and Condition Code Registers correspond, an instruction with it's Skip bit set will execute as a NOP, or null operation. **Both the ALU operations and the Multiplier operations specified by the skipped instruction become null operations.** Skipping is accomplished by executing each phase of the instruction except writing of its final results to the specified target register, including the Multiplier's accumulator. This method of conditional execution maintains consistency both in the ESP's internal pipelines and in the overall timing of the system. The sample rate will remain constant regardless of the number of skipped instructions.

**Instructions which are skippable cannot set the condition codes, regardless of whether they were executed or not.** This restriction allows the mask register to be initialized and a condition tested (that is, have the CCR set by an instruction) at the beginning of a block of skippable instructions, with the condition being held true or false across the entire length of the block.

To detect a Skip condition during the instruction cycle of any skippable instruction step, the following logical computation is performed. The states of N, Z, V, C, and LT flags are logically ANDed with their associated mask bits and ORed together to yield a single SKIP condition. If the NOT bit is set in the mask register this SKIP condition is inverted. If the final result is TRUE (equal to 1) then the current instruction step is skipped. The step becomes a NOP by disabling the output of any results.

    SKIP = NOTm ⊕ ((N•Nm) + (Z•Zm) + (V•Vm) + (C•Cm) + (LT•LTm))

(• = AND, + = OR, ⊕ = XOR; subscript m = the corresponding CMR bit.)

[Unresolved on these pages: (a) whether skipping also suppresses the RAM control field action (RAM read into DIL / FIFO pop / RAM write) and DOL FIFO pushes — the text says "disabling the output of any results" and "except writing of its final results to the specified target register"; (b) the CMR bit layout is not given explicitly — presumably same as CCR bits 23..18 (N,C,V,LT,Z,NOT) [inferred]; (c) note p10 says CMP is not affected by the skip bit, while here skippable instructions "cannot set the condition codes" — so a skippable CMP presumably still executes but does not update the CCR? This is ambiguous.]

#### 4.2.2 Arithmetic Condition Masking

There are 14 commonly used arithmetic conditions which can be tested based on the 5 CCR flags N, Z, C, V and LT. Seven of the conditions are simple combinations of these basic codes:

## [p28] doc p. 24

**Positive Flag Test Conditions**

| Function | Test Flags |
|---|---|
| Equal (to 0) | Z |
| Negative | N |
| Overflowed | V |
| Lower than (unsigned) | C |
| Lower than or same (unsigned) | C OR Z |
| Less than (signed compare) | LT |
| Less than or equal (signed) | LT OR Z |

Their negations result in the following additional seven:

**Negative Flag Test Conditions**

| Function | Test Flags |
|---|---|
| NOT Equal | NOT(Z) |
| Positive | NOT(N) |
| Overflow clear | NOT(V) |
| Higher or same (unsigned) | NOT(C) |
| Higher than (unsigned) | NOT(C OR Z) |
| Greater than or equal (signed) | NOT(LT) |
| Greater than (signed compare) | NOT(LT OR Z) |

The programmer loads a mask with 5 corresponding bits and a NOT bit into the Condition Code Mask register to select the condition which will cause instructions to be skipped.

The mask values for the 14 conditions are listed below. Note that these values are not copies of the bit patterns produced by the CCR when these conditions exist - they are merely the mask values that are necessary and sufficient for detecting the condition. **The mask codes shown in these tables are set up in the negative sense, that is if the condition is met do not skip the subsequent instructions. To change the sense of the test, simply invert the sense of the NOT bit in the mask register.**

(The mask-value table itself — "Arithmetic Condition Code Masks", TOC doc p. 25 — is on the next page, outside this part [continues on esp_29.png].)

[Observation: "Lower than (unsigned) = C" implies C = 1 means borrow after A-B (i.e. A < B unsigned), which conflicts with the p13 definition "C = carry (or no-borrow)". Resolve with hardware tests or other sources.]

---

## Summary of emulator-relevant facts from pages 1–28

- Instruction word: 48 bits (45 used): D[47:40] C[39:32] B[31:24] A[23:16] ALUop[15:12] OpSel[11:8] Skip[7] MAC[6] RAM[5:3] unused[2:0].
- Register space: 0..191 GPRs (24-bit); 234..241 SER0R/0L..SER3R/3L (16-bit); 242 MACL, 243 MACH; 244 DIL(read)/MEMSIZ(write); 245 DLENGTH; 246 ABASE; 247 BBASE; 248 DBASE (20-bit); 249 SIGREG (bit 22 = product shift: 1→<<1, 0→<<2); 250 CCR; 251 CMR; 252..255 ROM constants -1, $800000, $7FFFFF, 0 (writes discarded). 192..233 undocumented.
- ALU opcodes 0..F: ADD SUB ADDU SUBU CMP AND OR XOR ABS MOV ASL2 ASL8 LS15 DIFF ASR END.
- CCR bits: 23 N, 22 C, 21 V, 20 LT, 19 Z, (18 NOT — mask). LT = N xor V.
- SKIP = NOTm xor OR(flag & mask). Skippable instructions never update CCR.
- Multiplier: 24x24 → 48, shifted left by 1 or 2 (SIGREG), 2 guard bits in accumulator, saturated on readout; MAC bit = accumulate. Result high 24 bits → C (or FIFO). Skipped → no accumulation.
- Address generator: delay mode = (DBASE+offset) wrap by DLENGTH, masked by MEMSIZ; table = ABASE/BBASE + offset (no wrap, no MEMSIZ mask on bases); I/O = offset absolute. DBASE decremented by one RAM-address unit at END; reloads DLENGTH when passing zero. Writing MEMSIZ affects DBASE.
- DIL: 16-bit, left-justified at bits 23..8, low 8 bits read 0; persists until next read. DOL: 2-deep FIFO, oldest dropped on overflow, write-from-empty repeats last word, reset on halt-at-END.
- RAM control: 000 rd delay, 001 wr delay, 010 rd tabA, 011 wr tabA, 100 rd tabB, 101 rd delay+dump FIFO, 110 rd I/O, 111 wr I/O. The RAM field is pipelined into a different (later) instruction word than its operand select — details in 4.3.4 (part 2, doc p. 28–29).

Not covered in pages 1–28 (expected in part 2): arithmetic condition mask values table, execution-cycle/pipeline timing (4.3), MACH/MACL preload/postfetch and volatility, RAM pipelining offsets, host interface register map, serial port formats/timing, END/HALT host interaction, pinout, timing diagrams.

