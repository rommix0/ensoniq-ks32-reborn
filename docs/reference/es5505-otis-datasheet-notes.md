# Ensoniq ES5505 "OTIS" Specification: transcription

Source: 18-page scanned document titled "OTIS SPECIFICATION" (handwritten "5505" at the top of page 1), pages otis_01.png through otis_18.png.

Conventions used here:
- Text in quotation marks or block quotes is verbatim from the scan, including its typos.
- **[inferred]** marks something I worked out that the document does not say.
- **[illegible]** marks text I could not read.
- **[doc inconsistency]** marks a place where the document contradicts itself.
- "pN" gives the page number printed on the scan (it matches the image file number).

---

## 1. Introduction (p1)

The document describes OTIS this way:

> "OTIS is a VLSI device designed in a 2 micron double metal CMOS process. ... These improvements are achieved through the use of frequency interpolation and on board real time digital filters. All calculations in the device are made with at least 16 bit accuracy."

Major features as listed on p1, verbatim:
- 48 PIN DUAL IN LINE PACKAGE
- ON CHIP REAL TIME DIGITAL FILTERS
- FREQUENCY INTERPOLATION
- 32 INDEPENDENT VOICES, UP FROM 25 IN DOCII
- LOOP START AND STOP POSITIONS FOR EACH VOICE
- BIDIRECTIONAL AND REVERSE LOOPING
- 68000 COMPATIBILITY FOR ASYNCHRONOUS BUS COMMUNICATION
- ON BOARD PULSE WIDTH MODULATED D TO A
- 4 CHANNEL STEREO SERIAL COMMUNICATION PORT
- INTERNAL VOLUME MULTIPLICATION AND STEREO PANNING
- A TO D INPUT FOR POTS AND WHEELS
- UP TO 10 MHz OPERATION

---

## 2. Theory of operation (p2)

- OTIS runs in its own memory space, called sound memory. Sound memory can be "up to 2 Mword of 16 bit ram".
- OTIS talks to the microprocessor over a 16-bit data bus and a 4-bit address bus.
- There are 32 independent voices. Each voice is controlled by 16 voice-specific registers. There are also 3 global registers.
- Voices are processed one after another. For each voice:
  - "The 20 bit integer portion of the 29 bit accumulator is placed on the Address Bus (A0 thru A19) along with a 1 bit bank select (BS) to select between 1Mword memory banks."
  - "The sample data S1 is received from the Data Bus and placed in a temporary holding register. The accumulator is then incremented by one and placed on the Address Bus. This sample, S2, is used with the previous sample, S1, to compute SF, the actual sample."
  - "The value SF is now transferred to the filter computation section. In this section a real time 4 pole digital filter is implemented as 4 one pole filters cascaded in a chain. The first two poles are fixed as low pass filters the second two poles can be set independently as either high pass or low pass."
- Address generation per voice uses:
  - a 29-bit Voice Accumulator (20-bit integer part, 9-bit fractional part)
  - a Loop Start Register
  - a Loop End Register
  - a Control Register

  The integer part addresses external sound RAM. The fractional part is used for interpolation. The Control Register controls interrupts, the looping modes, stopping and starting the voice, and a 1-bit bank select that picks a 1 Mword bank.
- "The three global registers are used to set the total number of active voices, the Voice Interrupt Vector, and the Page Register. There are nine additional registers for setting OTIS modes and testing the channel registers."

---

## 3. Pinout (p2, 48-pin DIP)

| Pin | Name | Pin | Name |
|---|---|---|---|
| 1 | A17/D13 | 48 | VSS |
| 2 | A18/D14 | 47 | A16/D12 |
| 3 | A19/D15 | 46 | A15/D11 |
| 4 | BS | 45 | A14/D10 |
| 5 | PWZERO (called PWMZERO in the text) | 44 | A13/D9 |
| 6 | SER0 | 43 | A12/D8 |
| 7 | SER1 | 42 | A11/D7 |
| 8 | SER2 | 41 | A10/D6 |
| 9 | SER3 | 40 | A9/D5 |
| 10 | SERWCLK | 39 | A8/D4 |
| 11 | SERLR | 38 | A7/D3 |
| 12 | SERBCLK | 37 | A6/D2 |
| 13 | RLO | 36 | A5/D1 |
| 14 | RHI | 35 | A4/D0 |
| 15 | LLO | 34 | CLKIN |
| 16 | LHI | 33 | CAS |
| 17 | POT | 32 | AMUX |
| 18 | DTACK | 31 | RAS |
| 19 | R/W | 30 | E |
| 20 | MS (MSB in the text) | 29 | IRQ |
| 21 | CS (CSB in the text) | 28 | A3 |
| 22 | RES | 27 | A2 |
| 23 | VSS | 26 | A1 |
| 24 | VDD | 25 | A0 |

Notes:
- A0-A3 are dedicated pins. A4-A19 are multiplexed with D0-D15.
- RES (pin 22) is not described anywhere in the pin descriptions. Presumably it is reset. [inferred]

---

## 4. Register description (pp3-10)

### 4.1 Host addressing and paging

- The host selects a register with A3-A0, so each page has 16 register locations.
- The Page Register is at location F (hex) **in every page**. 128 pages can be reached through it.
- Page ranges, quoted from p3:
  - "Locations 0 thru A(hex) of pages 0 to 31 control the most often used settings for each voice."
    - [doc inconsistency] The register map also puts ACCL at location 11 (B hex) in pages 0-31.
  - "Locations 1 thru 6 of pages 32 to 63 offer access to the temporary registers for the intermediate data used in the real time filter calculations."
  - "Locations 0 thru 8 of pages 64 to 127 access the serial communication registers and the special mode register."
    - [doc inconsistency] The register map also lists PAR at location 9 in pages 64-127.
- p7 says: "Pages 64 through 128 are used to address eight output channel registers mode register and the pot a/d conversion register. These registers are global to this address space and are accessed when the MSB of the page register is a 1."
  - The register map heading says 64 through 127.
  - The channel registers are global: the voice number does not matter in pages 64-127. [inferred from "global to this address space"]
- p6: "Note that when accessing these pages [32-63] the global registers are still available." (These are ACT, IRQV and PAGE at locations 13, 14 and 15.)
- Decoding the page number [inferred]:
  - bits 4:0 = voice number (0-31)
  - bit 5 = filter-storage page select (pages 32-63)
  - bit 6 = channel/serial page select (pages 64-127)
- Voice-specific registers (p3): "All bits in all the voice specific registers can be written to and read from."

**Handwritten annotations (p9).** Next to the register numbers in the pages 0-31 and 32-63 tables someone wrote hex numbers equal to 2 × the register number:

| Register | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Annotation | 2 | 4 | 6 | 8 | A | C | E | 10 | 12 | 14 | 16 | 1A | 1C | 1E |

These look like 68000 byte offsets: registers are 16-bit words, so A1-A4 on the CPU side drive OTIS A0-A3. [inferred] A few of the handwritten characters are faint, but the sequence is clear.

### 4.2 Register map: pages 0-31 (per-voice), p9

| Reg | Symbol | Description | Bits | Justification |
|---|---|---|---|---|
| 0 | CR | CONTROL REGISTER | 12 | [11:0] |
| 1 | FC | FREQUENCY CONTROL | 15 | [15:1] |
| 2 | STRT-H | LOOP START REG - HIGH | 13 | [12:0] |
| 3 | STRT-L | LOOP START REG - LOW | 11 | [15:5] |
| 4 | END-H | LOOP END REG - HIGH | 13 | [12:0] |
| 5 | END-L | LOOP END REG - LOW | 11 | [15:5] |
| 6 | K2 | FILTER CUTOFF COEFFICIENT #2 | 12 | [15:4] |
| 7 | K1 | FILTER CUTOFF COEFFICIENT #1 | 12 | [15:4] |
| 8 | LVOL | LEFT VOLUME | 8 | [15:12] EXP, [11:8] MANT |
| 9 | RVOL | RIGHT VOLUME | 8 | [15:12] EXP, [11:8] MANT |
| 10 | ACCH | ACCUMULATOR HIGH | 13 | [12:0] |
| 11 | ACCL | ACCUMULATOR LOW | 16 | [15:0] |
| 13 | ACT | NUMBER OF VOICES | 5 | [4:0] |
| 14 | IRQV | INTERRUPTING VOICE VECTOR | 5 | [4:0] |
| 15 | PAGE | PAGE SELECT REGISTER | 7 | [6:0] |

- Register 12 is not listed.
- The prose (p6) calls the accumulator registers "ACCH/ACCL - high/low - A/B", i.e. hex A and B, which is the same as decimal 10 and 11.
- The prose (p3) calls the global registers "Page Register - F", "IRQV - E" and "ACT - D", i.e. 15, 14 and 13.

### 4.3 Register map: pages 32-63 (filter storage, per voice), p9

| Reg | Symbol | Description | Bits | Justification |
|---|---|---|---|---|
| 1 | O4(n-1) | FILTER 4 TEMP REGISTER | 16 | [15:0] |
| 2 | O3(n-1) | FILTER 3 TEMP REG #1 | 16 | [15:0] |
| 3 | O3(n-2) | FILTER 3 TEMP REG #2 | 16 | [15:0] |
| 4 | O2(n-1) | FILTER 2 TEMP REG #1 | 16 | [15:0] |
| 5 | O2(n-2) | FILTER 2 TEMP REG #2 | 16 | [15:0] |
| 6 | O1(n-1) | FILTER 1 TEMP REGISTER | 16 | [15:0] |
| 13 | ACT | NUMBER OF VOICES | 5 | [4:0] |
| 14 | IRQV | INTERRUPTING VOICE VECTOR | 5 | [4:0] |
| 15 | PAGE | PAGE SELECT REGISTER | **6** | **[5:0]** |

[doc inconsistency] PAGE is listed as 6 bits [5:0] here but 7 bits [6:0] in the pages 0-31 table. Seven bits is the one consistent with 128 pages.

### 4.4 Register map: pages 64-127 (channel / serial / pot), p10

| Reg | Symbol | Description | Bits | Justification |
|---|---|---|---|---|
| 0 | CH0L | CHANNEL 0 LEFT | 16 | [15:0] |
| 1 | CH0R | CHANNEL 0 RIGHT | 16 | [15:0] |
| 2 | CH1L | CHANNEL 1 LEFT | 16 | [15:0] |
| 3 | CH1R | CHANNEL 1 RIGHT | 16 | [15:0] |
| 4 | CH2L | CHANNEL 2 LEFT | 16 | [15:0] |
| 5 | CH2R | CHANNEL 2 RIGHT | 16 | [15:0] |
| 6 | CH3L | CHANNEL 3 LEFT | 16 | [15:0] |
| 7 | CH3R | CHANNEL 3 RIGHT | 16 | [15:0] |
| 8 | SERMODE | SERIAL MODE | 8 | [15:11,2:0] |
| 9 | PAR | POT A/D REGISTER | 10 | [15:6] |

Registers 13, 14 and 15 are not listed for this range. The Page register is said to appear at F in all pages. [inferred]

### 4.5 Bit definitions of the multipurpose registers (p10, verbatim layout)

| Reg | B15 | B14 | B13 | B12 | B11 | B10 | B9 | B8 | B7 | B6 | B5 | B4 | B3 | B2 | B1 | B0 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CR | 1 | 1 | 1 | 1 | LP4 | LP3 | CA1 | CA0 | IRQ | DIR | IRQE | BLE | LPE | BS | STOP1 | STOP0 |
| SERMODE | MSB4 | MSB3 | MSB2 | MSB1 | MSB0 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | SONY/BB | TEST | A/D |

- The "1" entries are unused bits, which presumably read back as 1. [inferred]
- [doc inconsistency] p3 says "The Control Register is an eight bit register". The map says 12 bits [11:0], and the bit table shows 12 used bits.
- Mask form of CR [inferred from the table above]:

| Field | Mask |
|---|---|
| STOP0 | 0x0001 |
| STOP1 | 0x0002 |
| BS | 0x0004 |
| LPE | 0x0008 |
| BLE | 0x0010 |
| IRQE | 0x0020 |
| DIR | 0x0040 |
| IRQ | 0x0080 |
| CA (CA1:CA0) | 0x0300 |
| LP3 | 0x0400 |
| LP4 | 0x0800 |

### 4.6 Global registers (p3)

**Page Register (PAGE), location F**
- Selects which set of 16 registers is visible: 128 pages in total (see 4.1).

**Voice Interrupt Vector Register (IRQV), location E**

Verbatim:

> "The Voice Interrupt Vector register is used by the microprocessor to determine which voice has caused an interrupt to occur. When a voice has its interrupt enabled in its corresponding control register and the proper events occur to cause an interrupt to be issued, the voice number is placed into this register so that the processor can read the value and determine which voice requires servicing. The IRQ output line of the OTIS chip will go high after the processor reads this register. Bit 7 of this register is a monitor of the IRQ output line and bits 4 through 0 determine the voice interrupt. Conditions sufficient to cause an interrupt will be defined in the Interrupt Control Section."

- Bit layout: bits 4:0 = voice number; bit 7 = mirror of the IRQ pin.
  - The pin is open-drain and active-low. Bit 7 = 0 means an interrupt is pending.
  - Bits 6:5 are not described.
- The map lists IRQV as 5 bits [4:0], but the text also describes bit 7.

**Active Voice Register (ACT), location D**

Verbatim:

> "This register uses bits 4 through 0, encoding values 31 through 0. The actual number of active voices is equal to the number in this register plus one. The number of active voices determines the output sample rate of OTIS. Each voice takes 1.6 microseconds to be processed if OTIS is running at 10MHz. When all 32 voices are active, this corresponds to a 19.5KHz sample rate. If only 16 voices are active, the sample rate is 39KHz."

Sample rate [inferred formula]:
- 1.6 µs at 10 MHz is 16 CLKIN cycles per voice. That is also two E-clock periods, since E is 4 clocks high and 4 low (see 6.1).
- So Fs = CLKIN / (16 × (ACT + 1)).
- Check: 10 MHz / (16 × 32) = 19.53 kHz, and 10 MHz / (16 × 16) = 39.06 kHz. Both match the text.
- The document gives no formula and no minimum voice count for ACT itself. Section 4.5 of the document does require at least 8 voices for the serial output (see 8.4 here).

### 4.7 Voice registers (pages 0-31)

#### CR: Control Register, reg 0 (pp3-5)

**LP4, LP3.** Filter configuration bits, described as "lowpass pole 4 and lowpass pole 3, respectively."

Filter Configuration and Coefficient Assignments (p4, verbatim):

| LP4 | LP3 | Filter Pole 4 Coefficient | Filter Pole 3 Coefficient |
|---|---|---|---|
| 0 | 0 | HP / K2 | HP / K2 |
| 0 | 1 | HP / K2 | LP / K1 |
| 1 | 0 | LP / K2 | LP / K2 |
| 1 | 1 | LP / K2 | LP / K1 |

> "Note that the above chart shows both the mode of the filter pole as well as the coefficient value used in the filter computation. In the digital filter poles one and two are low pass and use coefficient K1."

Read as rules:
- Pole 4 always uses K2. LP4 = 1 makes it low-pass; LP4 = 0 makes it high-pass.
- Pole 3 depends only on LP3:
  - LP3 = 1: LP with K1
  - LP3 = 0: HP with K2, **except** when LP4 = 1, where it is LP with K2.
  - The table is reproduced exactly. Pole 3 with LP4 = 1 and LP3 = 0 is "LP / K2", not HP.

**CA1, CA0.**

> "The Channel Assign information is a 2 bit code normally used to specify an output channel for the current voice. There are four serial output channels 0 to 3. Channel 0 is also connected to the pulse width modulated playback outputs."

**IRQ.**

> "A status flag showing that this voice has requested service through the Voice Interrupt Vector or has an interrupt request pending and is waiting for the Voice Interrupt Vector to be cleared of the current voice."

**DIR.**

> "This bit indicates the direction the sample is being scanned by the address ALU. A zero indicates the forward (incrementing) through memory, a one indicates the decrementing case. If the bidirectional looping mode has been selected the address ALU will automatically toggle this bit when the address reaches either the Loop Start or Loop End positions."

**IRQE.** "This bit enables the Interrupt for the voice if IRQE=1."

**BLE.** "Bidirectional looping enabled if BLE=1 and LPE=1."

**LPE.**

> "Looping is enabled if LPE=1. Looping refers to restoring the address accumulator to the Loop Start position when the address accumulator exceeds the value in the Loop Stop register. If the LPE bit is a zero then the accumulator will stop when it reaches Loop Stop. The role of the Start and Stop positions is reversed if the DIR bit is a one. The state of BLE is ignored if LPE=0."

**BS.**

> "This bank select bit is output on the BS pin at the same time as address for a memory cycle. It can be used to distinguish between 2 1Mword banks of ram in a 2Meg system. It can also be used as an external indication of the state of the voice counter."

**STOP1.**

> "This bit is used by the processor to stop the address ALU from modifying any bits is a voice during its normal processing. For example, if this bit is set then the newly calculated address for the next sample will not be stored in to the address accumulator or the direction bit will not be changed. This feature allows the microprocessor to change the contents of either the control register or accumulator with concern."

"with concern" is probably meant to read "without concern". [inferred]

**STOP0.**

> "This bit performs the same function as the STOP1 bit. The difference is that this bit can be set by the address ALU. This bit is set when Looping is disabled and the accumulator reaches the Loop Stop position. Note that both STOP1 and STOP0 must equal zero for the voice to run."

The document does not say whether a stopped voice still produces output through the filter and volume stages, i.e. holds its last sample, or is muted.

Loop mode summary [inferred from the bits above]:

| LPE | BLE | Behaviour |
|---|---|---|
| 0 | x | One-shot. At the loop end (loop start when DIR = 1) the ALU sets STOP0 and the voice stops. |
| 1 | 0 | Forward loop, or reverse loop if DIR = 1. Wraps from End back to Start, or Start to End when reversed, keeping the remainder. |
| 1 | 1 | Bidirectional. DIR toggles at each boundary. |

In every mode, IRQ is raised at the loop end if IRQE = 1, regardless of loop mode (see 9).

#### FC: Frequency Control, reg 1 (p5)

> "The Frequency Control Register is used to define the rate at which the address ALU will step through the sample memory. The register is a 15 bit word, left justified, that is divided into a 6 bit integer portion and a 9 bit fractional portion. At 10Mhz operation this yields an effective 2 cents resolution."

- Bits are [15:1]: bits 15:10 hold the 6-bit integer, bits 9:1 the 9-bit fraction.
- The step added to the accumulator is (FC >> 1) in units of 1/512 sample. That is the same 9-bit fraction scale as the accumulator. [inferred]
- So FC = 0x0400 is a step of 1.0 sample per output sample, and the maximum step is just under 64. [inferred]

#### STRT: Loop Start, reg 2 (high) and reg 3 (low) (p5)

> "The Start Register, also called the Loop Start Register, is a 24 bit value divided into two register locations, namely registers 2 and 3. Register 2 contains the upper 13 bits of the Loop Start position while register 3 contains the lower 11 bit of the position. The upper 13 bits are right justified and lower 11 bits are left justified (See Register Map). The Loop Start position, as its name implies, is used to define where in the sample memory the accumulator will jump to when it reaches the Loop End position. Note that the Loop Start register has a 20 bit integer portion and a 4 bit fractional nibble. This fractional positioning capability allows much finer frequency resolution tuning when looping a waveform."

- STRT-H is in [12:0]. STRT-L is in [15:5].
- Alignment with the accumulator [inferred]:
  - STRT-H[12:0] lines up with ACCH[12:0], i.e. integer address bits 19:7.
  - STRT-L[15:9] lines up with integer address bits 6:0.
  - STRT-L[8:5] holds the 4 fractional bits, matching the top 4 of the accumulator's 9 fractional bits.
  - So the 29-bit compare value is (STRT-H << 16 | STRT-L), with the low 5 fractional bits zero.

#### END: Loop End, reg 4 (high) and reg 5 (low) (p5)

> "The End Register, also called the Loop End Register, is partitioned the same way as the Start Register, a right justified 13 bit MSW and a left justified 11 bit LSW. It is used to mark the end of a loop in the sample memory. Note that the Start and End Registers reverse their roles if the Direction bit is a one. The End register contains the same integer and fractional partition as the Start register."

#### K2: Filter Cutoff High, reg 6 (p5)

> "The Filter Cutoff High Register is used to determine the 3dB point of poles three and four of the digital filter. It is a 12 bit register, left justified, using bits 15 through 4."

#### K1: Filter Cutoff Low, reg 7 (p5)

> "The Filter Cutoff Low Register is used to determine the 3dB point of poles one and two of the digital filter. It is a 12 bit register, left justified, using bits 15 through 4."

The table on p4 also uses K1 for pole 3 when LP3 = 1.

#### LVOL: Left Volume, reg 8 (p6)

> "The Left Volume Register is an 8 bit left justified register used to define the effective amplitude of the left channel of the sampled signal. It contains a 4 bit mantissa and a 4 bit exponent. The output of the filter is multiplied by a 5 bit value consisting a leading '1' followed by the 4 bit mantissa. The result of the multiplication is then shifted by the 4 bit exponent as shown below."

- Bits [15:12] are the exponent. Bits [11:8] are the mantissa.
- The "shown below" table actually appears in section 4.4 Stereo Panning (p16). It is reproduced in section 8 here.

#### RVOL: Right Volume, reg 9 (p6)

> "This register is used to define the effective amplitude of the right channel of the sampled signal. Its' contains match those of LVOL. Together with LVOL it allows amplitude manipulation and stereo panning."

**There is no volume ramp or envelope hardware.** The document describes only these static 8-bit LVOL and RVOL registers. Nothing describes ramping, ramp rate or envelope registers. Any envelope must be done by the host CPU rewriting LVOL and RVOL. [inferred from absence]

#### ACCH / ACCL: Accumulator, regs 10/11 (A/B hex) (p6)

> "The Accumulator is a 29 bit register divided into two parts, Accumulator High and Accumulator Low. The Accumulator high is 13 bits right justified (ACCH12 thru ACCH0) and the Accumulator low is a full 16 bit word (ACCL15 thru ACCL0). The Accumulator is comprised of an integer portion and a fractional portion. The integer part is 20 bits using ACCH12-ACCH0, ACCL15-ACCL9. This 20 bit value is the actual information used to fetch the sample data from memory. The 9 bit fractional part is needed to obtain proper frequency resolution and also for the interpolation calculation to be discussed in the Signal Processing section.
> The Accumulator is also used as the waveform start position. This is not to be confused with the use of the Loop Start register. The initial Accumulator value is the beginning of the waveform while the Loop Start position defines the beginning of the sustain part of the waveform."

Accumulator format:

| Field | Bits | Width |
|---|---|---|
| Integer (address) | ACCH[12:0] : ACCL[15:9] | 20 |
| Fraction | ACCL[8:0] | 9 |

- As a single number: ACC29 = (ACCH & 0x1FFF) << 16 | ACCL. [inferred]
- The word address is ACC29 >> 9 (20 bits). [inferred]
- BS from CR supplies address bit 20, giving 2 Mwords. [inferred]

### 4.8 Filter storage registers (pages 32-63) (pp6-7)

> "Pages 32 through 63 are used to address six Filter Storage Registers in each voice. These registers are used as temporary storage location for the real time digital filters in each voice. Each digital filter requires six sixteen bit registers to complete is calculations. These six registers are addressable by the processor mainly for testing purposes and initialization, but there may be some yet to be determined audio benefits achieved by some real time processor intervention."

| Reg | Name | Description (verbatim/condensed) |
|---|---|---|
| 1 | Filter Pole 4(n-1) | "the last output of final stage of the digital filter. This register is the actual data that is presented to the signal D to A converter." |
| 2 | Filter Pole 3(n-1) | "the last output of the third stage ... becomes the input to the fourth stage and the data pushed into Pole 3(n-2) on the next cycle." |
| 3 | Filter Pole 3(n-2) | "records the output of the third stage of the digital filter from the previous sample time. This information is needed to process the high pass filter mode of filter pole 4." |
| 4 | Filter Pole 2(n-1) | "the last output of the second stage ... becomes the input to the third stage and the data pushed into Pole 2(n-2) on the next cycle." |
| 5 | Filter Pole 2(n-2) | "records the output of the second stage of the digital filter from the previous sample time. This information is needed to process the high pass filter mode of filter pole 3." |
| 6 | Filter Pole 1(n-1) | "the last output of the first stage ... becomes the input to the the second stage." |

What this means for an emulator [inferred]:
- Each voice's filter state is O1, O2, O2prev, O3, O3prev and O4.
- High-pass poles need the previous input X(n-1). For pole 3 that is the previous output of pole 2, held in O2(n-2). For pole 4 it is the previous output of pole 3, held in O3(n-2).

### 4.9 Channel registers (pages 64-127) (pp7-8)

**CH0L/CH0R, regs 0 and 1**

> "These sixteen bit registers contain the accumulation of the result of the left and right volume multiplications. The registers are connected to serial port 0 and the PWM D/A output. They can be accessed only for testing these output sections. Therefore they can be written when the test bit of the SERMODE register is set, but can not be read."

**CH1L/CH1R, regs 2 and 3.** "same function as CH0L/CH0R but they are not connected to PWM outputs. The registers are connected to serial port 1 and can be access for testing in the same way as CH0L/CH0R."

**CH2L/CH2R, regs 4 and 5.** "Same as CH1L/CH1R, but connected to serial port 2."

**CH3L/CH3R, regs 6 and 7.**

> "These registers serve the output function on serial port 3 as described above. Port can also be designated as a serial input by setting the A/D bit of the SERMODE register. This is to allow OTIS to accept digital information from an external A/D and hold the information for retrieval by the processor. For this reason these two registers are readable in any mode, but can only be written for testing as described above."

**SERMODE, reg 8** (bits [15:11] and [2:0])

**MSB[4:0]** (bits 15:11):

> "The channel registers are 16 bits with 5 guard bits to prevent overflow during accumulation. Since the bus interface is 16 bits, these 5 bits are prevented and are written to the 5 MSB's when a channel register is written. If these bits are not either all 0's or all 1's, then overflow has occurred and the output will be saturated at either most positive or most negative depending on the state of MSB[4]. These bits are used to test the overflow detect logic of the channel registers."

- So each channel accumulator is 21 bits: 5 guard bits plus 16. At output it saturates to 16 bits.
- MSB4 is the sign bit used to pick the saturation direction.

**SONY/BB** (bit 2):

> "This bit designates that the serial input on port 3 during A/D mode follows either SONY or BURR-BROWN convention. SONY convention is that the first bit transferred occurs in the BCLK cycle immediately following assertion of WCLK. In BURR-BROWN format there is a 1 BCLK cycle delay after WCLK falls before the first bit of data is valid."

The document does not say which bit value selects which format.

**TEST** (bit 1):

> "This bit turns of the signal accumulation function and allows write access to the channel registers. In this way the serial and PWM output sections can be isolated for testing."

**A/D** (bit 0):

> "This bit puts serial port 3 in input mode for accepting serial data from and external A/D. The serial data received is stored in CH3L/CH3R for access by the processor. This bit is set on chip reset to tristate the output to avoid contention."

So the reset state is A/D = 1, and SER3 is an input after reset.

**PAR: Pot A/D register, reg 9** (10 bits, [15:6])

> "This register contains the digital value resulting from the A/D conversion for tracking pots and wheels. The conversion is performed by comparing the input voltage on the POT pin to an internal reference ~2.8V. A internal pulldown on the POT pin discharged the pin. The pulldown is turned off allowing an external R-C to charge while the counter is clocked. When the voltage level on the input reaches the reference the value in the counter is loaded into this register for reading by the processor. The counter is clocked at 1.25Mhz, therefore, a conversion is done every 1.6ms"

1.25 MHz is CLKIN/8 at 10 MHz. [inferred]

---

## 5. Pin descriptions (pp11-13)

The text groups the pins into six categories: "1) clocks, 2) control, 3) address/data transfer, 4) serial port 5) playback outputs and 6) potentiometer a/d."

### 5.1 Clocks (p11)

**CLKIN**

> "The CLKIN pin is the input clock for OTIS. The clock can be any frequency from 1 Mhz to 10Mhz. The clock input should be as close to 50-50 duty cycle as possible, since the chip times events on both input clock edges."

The AC table on p18 gives 5 to 10 MHz instead.

**RAS**
- An output clock that strobes the row address into DRAM.
- It occurs when OTIS fetches from memory (E low), or when MSB is low and E is high.
- "The RAS clock is high for one clock cycle and low for three clock cycles."

**CAS**
- An output clock that strobes the column address. It occurs under the same conditions as RAS.
- "The CAS clock is high for two clock cycles and low for two clock cycles."

**AMUX**
- An output that multiplexes row and column addresses to the DRAM. It is generated whenever RAS and CAS occur.
- "This clock is high for 1.5 clock cycles and low for 2.5 clock cycles, which means it occurs exactly between the RAS falling edge and the CAS falling edge."

**E**
- An output that shows which phase the chip is in:
  - When E is low, OTIS accesses memory for sound generation.
  - When E is high, OTIS and sound memory are available to the processor.
- "The E clock is high for 4 clock cycles and low for four clock cycles."
- It is normally used to enable tristate buffers and to gate the direction of data and address to the DRAM.
- "Note that even though the system shares memory the CSB, MSB and DTACK lines create as simply asynchronous bus communication environment."

### 5.2 Control (pp11-12)

**R/W**
- An input. Low means the processor is writing to OTIS.
- It is strobed on the rising edge of E. If CSB is low, OTIS responds.
- Setup 0 ns; hold 100 ns after the E rising edge.

**CSB (chip select)**
- An input, strobed on the rising edge of E.
- If CSB is low, OTIS performs the requested operation and generates DTACK.
- Setup 0 ns; hold 100 ns after E rises.

**MSB (memory select)**
- An input that helps the processor access shared DRAM.
- It is strobed on the rising edge of E. Setup 0 ns; hold 100 ns.
- "When the MSB line is low OTIS will generate RAS,CAS,AMUX, and DTACK during an E clock high cycle."

**DTACKB**
- An open-drain output used to synchronise 68000 transfers.
- It goes low during an E-high cycle whenever CSB or MSB was low at the rising edge of E.
- "The DTACK line goes low two clock cycles after the rising edge of the E clock and will release at the falling edge of the E clock."

**IRQ**
- An open-drain output. It goes low when a voice requires an interrupt.
- "When the processor reads the Voice Vector register the line will then be released."

### 5.3 Address/data transfer (p12)

**A0-A3** are bidirectional:
- When E is high they are inputs that select an OTIS register. "These address inputs must be valid 100ns. after the rising edge of the E clock and are latched at the falling edge of DTACK."
- When E is low they are outputs that address external sound RAM. "These pins will output the voice address at the falling edge of the E clock. The address will remain valid until the falling edge of CAS at which time the pins will go to tristate."

**A4/D0 - A19/D15** are bidirectional:
- When E is low they first output the address for the sound memory access. "The lines A0 through A3 together with A4 through A19 form the entire 20 bit address generated by OTIS. The addresses A4 through A19 will turn on 50ns. after E goes low and are valid 35ns. prior to the fall of RAS. The address will remain valid until the fall of CAS at which time the bus will go to the input mode to accept the data from the memory (D0 through D15)."
- When E is high they carry data only:
  - "If the CSB is low and the R/W line is high at the rising edge of the E clock then the chip will output the data from the register requested on pins A0 through A3."
  - "If both the CSB and the R/W lines low then the chip will accept the data from the bus and write it into the register specified by A0 through A3. The data is latched into the chip on the falling edge of the E clock."

Sample words are 16 bits wide, read from D0-D15. **No 8-bit, compressed or other sample formats are mentioned.** [inferred from absence]

**BS**

> "When the E clock is low this pin is driven based on the BS bit of the voice dependent control register. It can be used to select between 2 1Mword banks of sound memory. Since the state of this bit is voice dependent, it can also be used as an external indication as to the state of the voice counter."

### 5.4 Serial output ports (p13)

**SERBCLK.** "This is the bit clock for transfers on the serial port. It is a free running 2.5 Mhz clock." That is CLKIN/4 at 10 MHz. [inferred]

**SERWCLK.** "This is the word clock for transfers on the serial port. A low going transition on this clock signals the end of a 16 bit transfer. The 16 bits shifted previous to the transition should be latched as valid data."

**SERLR.** "This is the left/right clock for transferring stereo information. It signifies left when in the high state and right when in the low state. Transitions on this clock occur with the first bit of a 16bit transfer."

**SER0,1,2,3**

> "These are the serial data lines. Data is transferred on these lines in 2 word of 16bits each for stereo. Transfer begins with SERLR going high and the left MSB output on the data line. The each additional bit is clocked out on the falling edge of SERBCLK until the LSB of the 16 bit word is output. On the next falling edge of SERBCLK, the LSB of left is followed by the MSB for right, SERLR is negated and SERWCLK is driven low. SER3 is also capable of data input when the A/D bit of SERMODE is set."

Frame summary:
- One frame is 32 bits: left 16 bits MSB-first, then right 16 bits MSB-first.
- Data changes on the falling edge of SERBCLK.
- SERLR is high for left and low for right.
- SERWCLK has a falling edge at each word boundary.

### 5.5 Playback outputs (p13)

**LHI, LLO, RHI, RLO.** The text prints these as "LHI,LHO / RHI,RLO" and says: "These outputs are left and write D/A outputs for the value in the channel 0 left and right registers."

**PWMZERO.** "This is a zero reference pulse to be used along with the above four signals for the playback scheme."

### 5.6 Potentiometer A/D (p13)

**POT.** "This pin is the input to a 10 bit A/D converter for tracking pots and wheels in the system."

---

## 6. Algorithms (pp14-17)

### 6.1 Address generation (p14)

The chip uses "Phase Accumulating Counters": an adder, an accumulator and a frequency control word, each with an integer and fractional part. At each voice time slot the FC word is added to the accumulator.

The document's example: transposing a waveform up a half octave needs a step of 1.5, so sample "35.5" is approximated by interpolating halfway between samples 35 and 36.

> "In order to loop on a waveform the following algorithm is performed.
> 1. Fetch the Accumulator and put integer portion of accumulator on external address bus.
> 2. Add 1 to Accumulator and place on external address bus. This is used for interpolation. The incremented value is not retained.
> 3. Add Frequency Control Word to Accumulator
> 4. Subtract Loop End from Accumulator
> 5. If Loop End > Accumulator then add Frequency Control to Accumulator else add remainder of subtraction in Step 4 to Loop start.
>
> As can be seen the remainder of the subtraction in step 4 is important to keep the phase of the waveform intact when looping and transposing. The counting algorithm presented here is used for all modes of looping except that the roles of the Loop Start and Loop End registers will be swapped depending on the direction of the count."

Notes on these steps:
- Step 2 adds 1 to the **integer** part, so S2 is at address int + 1. [inferred]
- With DIR = 1 the second sample is presumably still int + 1, or possibly int - 1. **The document does not say.**
- Steps 3 and 5 as literally written would add FC twice. The intended meaning is probably:

  ```
  new = acc + FC
  if new < END: acc = new
  else:         acc = START + (new - END)
  ```

  [inferred]
- The text does not say:
  - whether the comparison is >= or >
  - exactly how bidirectional mode reflects the remainder
  - what the accumulator holds after a one-shot stop. It is clamped, or not updated, once STOP0 is set by the ALU.

### 6.2 Interpolation (p14)

> "Once two successive samples have been fetched from the sound memory it is necessary to use linear interpolation to approximate a new sample. This approximation is done using by performing the following calculation.
> SF = S1 + ACCfr * (S2 - S1), where
> SF is the new sample,
> S1 is first sample fetched,
> S2 is the second sample fetched,
> ACCfr is the 9 fractional bits of the Accumulator."

ACCfr is used as a fraction of 512, i.e. SF = S1 + ((S2 - S1) × ACCL[8:0]) / 512. [inferred] The document does not specify rounding or truncation.

### 6.3 Digital filtering (pp15-16)

> "Each voice in OTIS is passed through four one pole filters. These digital filters each simulate a one pole Butterworth filter. As previously mentioned in the discussion of the filter configuration bits in register 9, filters one and two are fixed as low pass and filters three and four are selectable as either low pass or high pass."

[doc inconsistency] "register 9" is wrong. The configuration bits are LP3 and LP4 in CR, register 0.

Low-pass pole:

```
Yn = K * ( Xn - Yn-1 ) + Yn-1
```

where Yn is the new output, K the cutoff coefficient, Xn the input, and Yn-1 the previous output.

High-pass pole:

```
Yn = Xn - Xn-1 + K * Yn-1
```

where Yn is the new output, K the cutoff coefficient, Xn the input, and Xn-1 the previous input.

"The four filters are cascaded such that the output of the first is the input to the second and so on."

Cascade [inferred]:
- The chain is SF → pole 1 (LP, K1) → pole 2 (LP, K1) → pole 3 (LP3/LP4, per the table) → pole 4 (LP4, K2) → volume/pan.
- The pole-4 output is held in O4(n-1), which the document calls "the actual data that is presented to the signal D to A converter".

Choosing K for the low-pass filter (p15, verbatim; K is always less than 1):

```
K = { 1-cos(W)-√[cos²(w) + (2/N²)*[1-cos(W)]-1] } /[1-(1/N²)] , where
```

- K is the cutoff coefficient.
- W = 2πf/F, where f = frequency of interest and F = the sampling frequency.
- N is the attenuation factor, .707 for -3dB.

Choosing K for the high-pass filter (verbatim):

```
K = {cos(W)-√[cos²(w)+ (2/N²)*[1-cos(W)]-1]} , where
```

The same definitions of K, W and N apply.

The scan prints lower-case "w" inside cos²(w) and upper-case "W" elsewhere. Both refer to the same W. [inferred]

High-pass offset, verbatim:

> "The most useful range of a high pass filter is generally below 4kHz. In order to maximize the resolution of the filter in that range an offset of .5 has been added to the K coefficient. This offset means the highest cutoff frequency achievable is approximately 8000Hz. for one high pass pole.
> To determine the 12 bit digital value required for the K register perform the following calculation. For the low pass filter, multiply the value obtained from the previous equation by 4096. For the high pass filter subtract 0.5 and multiply by 8192.
> Remember that the equations presented here are for one pole only, and the value for the attenuation factor N must be scaled for the number of poles. For a one pole filter the 3dB point means N is equal to .707, a two pole filter requires each one pole filter to have N equal to .84, a three pole and N equals .89, etc."

Register-to-coefficient mapping [inferred inverse of the above]. K12 is the 12-bit value in register bits [15:4]:

| Pole type | Register value | Effective coefficient |
|---|---|---|
| Low-pass | K12 = K × 4096 | K = K12 / 4096 |
| High-pass | K12 = (K − 0.5) × 8192 | K = 0.5 + K12 / 8192 (range 0.5 to ~1.0) |

- **Ambiguity:** K2 can serve as an LP coefficient (pole 4 with LP4 = 1, or pole 3) or an HP coefficient. The text implies the hardware interprets the same 12 bits differently by mode: /4096 for LP, 0.5 + x/8192 for HP. This is not stated explicitly.
- Fixed-point details are not given: internal width beyond "at least 16 bit accuracy", rounding, and saturation inside the filter.

### 6.4 Stereo panning and volume (p16)

> "The filter outputs go through a stereo pan calculation in which the signal is multiplied by left and right volume values to split into left and right signals. This allows panning with the same resolution as volume adjust. The volume values are contained in registers as a 4 bit mantissa and a 4 bit exponent. The computation is performed as follows:
> 1. The 16bit signal is multiplied by the value 1MMMM where M represents the bits of the mantissa.
> 2. The 16 MSB's of the result of step 1 are then shifted by the exponent as follows."

Shift table, transcribed exactly. The 16-bit input is labelled "abcdefghijklmnop" (a = MSB, which is also the sign); the columns show the 16-bit result, MSB on the left.

| Exp | 16-bit result |
|---|---|
| 0 | 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 |
| 1 | a a a a a a a a a a a a a a a b |
| 2 | a a a a a a a a a a a a a a b c |
| 3 | a a a a a a a a a a a a a b c d |
| 4 | a a a a a a a a a a a a b c d e |
| 5 | a a a a a a a a a a a b c d e f |
| 6 | a a a a a a a a a a b c d e f g |
| 7 | a a a a a a a a a b c d e f g h |
| 8 | a a a a a a a a b c d e f g h i |
| 9 | a a a a a a a b c d e f g h i j |
| A | a a a a a a b c d e f g h i j k |
| B | a a a a a b c d e f g h i j k l |
| C | a a a a b c d e f g h i j k l m |
| D | a a a b c d e f g h i j k l m n |
| E | a a b c d e f g h i j k l m n o |
| F | a b c d e f g h i j k l m n o p |

In other words:
- result = (int16)input >> (15 − exp), an arithmetic shift with sign extension, for exp = 1..F.
- exp = 0 gives exactly 0 (muted), not all sign bits.
- exp = F passes the value through unshifted.
- Each exponent step is 6 dB. The mantissa gives 1.0000 to 1.1111 (binary), i.e. ×16/16 to ×31/16 relative.

These are [inferred] readings of the table.

Volume pipeline [inferred]:
- product = filter_out (16-bit signed) × (16 + mant), a 21-bit result.
- top16 = product >> 5. These are the "16 MSB's".
- out = exp ? (top16 >> (15 − exp)) : 0.

Rounding is not specified.

Channel routing:

> "The result of the stereo pan calculation will be assigned to one of four channels based on the two voice dependent channel assign bits. These four channels are implemented as eight accumulating registers (left and right for each of four channels) and will sum signals during a complete sample cycle."

So each voice's L and R values are added into CH[CA]L and CH[CA]R. Those are 21-bit accumulators with saturation to 16 bits on output; see SERMODE.MSB above.

### 6.5 Serial output and playback (pp16-17)

> "Once a cycle of the total number of voices has completed, the results of the channel accumulations are latched into shift registers and transmitted out through the serial port for used by external DSP's or D/A's. The accumulators are cleared to begin the next voice cycle. Since the bit clock for serial transmission is a 2.5MHz clock and each voice takes 1.6uS, A minimum of 8 voices must be used to insure enough time to transfer a full 32 bits of stereo information."

Timing check: 8 voices × 1.6 µs = 12.8 µs, and 32 bits / 2.5 MHz = 12.8 µs.

PWM playback (pp16-17, verbatim):

> "At the same time that the results are being output serially, the values in CH0L and CH0R are output on the playback outputs. The playback scheme is a pulse width modulated method. Each 16 bit register is separated into high and low bytes corresponding to the LHI/LLO and RHI/RLO pins. At the start of a cycle all outputs are asserted, an internal 8 bit counter is clocked at a 10MHz rate and when the value of the counter matches one of the registers bytes, the output associated with that byte will be reset. The result is a pulse on the outputs whose width as measured in cycles of the 10MHz clock is the value in the register. Integrating these pulses and mixing the HI and LO with a ratio of 256:1 will produce an analog signal correspond to the 16 bit value for left and right. 256 cycles of the 10MHz clock are required to represent the maximum 8 bit value. This times corresponds to 16 voices at 1.6uS/voice and represents a 39KHz playback rate. More than 16 voices can be used but the playback rate will slow proportionally.
> With more than 16 voices a negative offset would be introduced proportional to the number of voices above 16 (full scale). The PWMZERO output is supplied as a zero reference pulse with opposite polarity as the signal pulses. Because of the opposite polarity, greater than 16 voices will produce a positive offset on the reference pulse. The reference can therefore be mixed with the signal pulses to eliminate the offset problem."

---

## 7. Interrupt handling (p17)

> "An interrupt can be caused by any active voice in OTIS. A voice will request interrupt servicing when the interrupt enable bit has been set and the accumulator has reached the loop end point (loop start if the direction bit is set). An interrupt will be issued by a voice regardless of the state of the loop mode bits when the accumulator reaches the loop end.
> When an interrupt has been requested, the interrupt bit (bit 7 in the Control Register) is set, the voice number is latched into the Voice Interrupt Vector register and bit 7 of the Voice Vector register is cleared low. When the processor reads the Voice Vector register, bit 7 of the Voice Vector register is set high and when the voice which caused the interrupt is next processed, bit 7 of its Control Register is cleared.
> If a voice has reached an interrupt condition and the Voice Vector is already contains information from another voice, that new requested will be stacked. This stacking occurs automatically since the interrupt bit in the Control Register of the voice will be set when the interrupt condition occurs. Eventually the processor reads the Voice Vector clearing the old interrupt vector information and the new vector will be placed in the register when the new voice is again processed. Note this ability allows the processor to force an interrupt from a voice simply by setting the interrupt enable bit and the interrupt bit of a particular voice."

The resulting model [inferred]:
1. A voice with IRQE = 1 reaches its loop boundary, in any loop mode. Its CR.IRQ is set to 1.
2. On each voice's processing slot: if CR.IRQ = 1 and IRQE = 1, and IRQV is free (bit 7 = 1), then:
   - IRQV[4:0] = voice
   - IRQV bit 7 = 0
   - the IRQ pin goes low.
3. A host read of IRQV returns the vector. It then sets IRQV bit 7 = 1 and releases the IRQ pin.
4. The next time that voice is processed, its CR.IRQ is cleared.
5. Other voices with CR.IRQ = 1 wait until IRQV is free. They are serviced in voice-scan order.

The document does not say:
- whether step 4 happens only after the read
- the read value of IRQV bits 6:5
- the exact cycle at which IRQV is reloaded.

---

## 8. Electrical specifications (pp17-18)

### 8.1 Maximum ratings (p17)

| Rating | Symbol | Value | Units |
|---|---|---|---|
| Supply Voltage | VDD | -3 to 7 | V |
| Input Voltage | VIN | -3 to 7 | V |
| Operating Temperature | TA | 0 to 70 | C |
| Storage Temperature | TSTG | -55 to 150 | C |

### 8.2 DC characteristics (p18): VDD = 5.0V ±5%, VSS = 0V

| Characteristic | Symbol | MIN | MAX | Units |
|---|---|---|---|---|
| Input High Voltage | VIH | 2.0 | VDD | V |
| Input Low Voltage | VIL | -0.5 | 0.8 | V |
| Input Leakage Current, VSS ≤ VIN ≤ VDD | IIN | - | (blank) | uA |
| Hi-Z leakage @2.4V/0.5V | ITSI | (blank) | - | (blank) |
| Output High Voltage, IOH = (blank) (except DTACKB open collector) | VOH | 2.4 | (blank) | V |
| Output Low Voltage, IOL = (blank) | VOL | - | 0.5 | V |
| Input Capacitance | CIN | - | (blank) | (blank) |

Several cells are blank in the original. The exact column placement of the "-" marks for IIN, ITSI and CIN is uncertain on the scan.

### 8.3 AC characteristics (p18)

| Characteristic | Symbol | MIN | MAX | Units |
|---|---|---|---|---|
| Frequency of operation | f | 5 | 10 | Mhz |
| Clock Pulse Width | tCL,tCH | 45 | 55 | ns |
| Rise and Fall Times | tR,tF | 5 | 5 | (blank) |
| Clock low to E,RAS,CAS,AMUX | tCLA | | | |
| Address valid to RAS setup DOC read | tAVR | | | |
| Clock to DTACKB | | | | |
| CSB,MSB,R/W setup to E | | | | |
| Clock to Address valid | | | | |
| Clock to Address tristate | | | | |
| SERBCLK low to SERx | | | | |
| SERBCLK low to SERWCLK | | | | |
| SERBCLK low to SERLR | | | | |

Every row below "Rise and Fall Times" is blank in the original.

**There are no timing diagrams in the document.** All timing numbers are the ones quoted in the prose:

| Item | Value |
|---|---|
| E clock | 4 CLK high / 4 CLK low |
| RAS | 1 high / 3 low |
| CAS | 2 high / 2 low |
| AMUX | 1.5 high / 2.5 low |
| DTACK | low 2 clocks after E rises; released at E fall |
| R/W, CSB, MSB | setup 0 ns, hold 100 ns relative to E rise |
| A0-A3 host inputs | valid 100 ns after E rise; latched at DTACK fall |
| A4-A19 sound-memory address | on 50 ns after E falls; valid 35 ns before RAS falls; valid until CAS falls |
| Host write data | latched at E fall |
| Serial bit clock | 2.5 MHz |
| Pot counter | 1.25 MHz, one conversion per 1.6 ms |
| PWM counter | 10 MHz, 8-bit |

---

## 9. Emulator checklist: present vs. missing

**Present in the document:**
- full register map, with widths and justification
- CR and SERMODE bit layouts
- 29-bit accumulator format (20.9) and 24-bit loop start/end format (20.4)
- FC format (6.9)
- paging scheme
- voice count and sample-rate relationship
- linear interpolation formula
- one-pole LP and HP difference equations
- K design formulas and register scaling (×4096 LP; (K − 0.5) × 8192 HP)
- filter-mode table
- volume mantissa/exponent multiply and exact shift table
- 4-channel routing via CA
- 5-guard-bit saturation
- serial frame format
- PWM scheme
- IRQ/IRQV protocol, including stacking
- pot ADC
- pinout and bus handshake timings in prose

**Missing or ambiguous:**
- No envelope or volume-ramp hardware is described. Volume is static LVOL/RVOL only.
- No compressed, 8-bit or µ-law sample formats. Samples are 16-bit words only.
- Exact loop boundary comparison, the bidirectional reflection arithmetic, and the address of the S2 fetch in reverse direction.
- Behaviour of a stopped voice (does it still output or filter?).
- Rounding and truncation in interpolation, filter and volume.
- Exact LP/HP interpretation of K2 when it is used in LP mode.
- Polarity of the SONY/BB bit.
- Reset values. Only A/D = 1 at reset is stated.
- IRQV bits 6:5.
- Register 12 and registers 10-15 in pages 64-127.
- PAGE width: 7 bits vs. 6 bits.
- CR width: "eight bit" in the text vs. 12 bits in the map.
- The AC timing table is mostly empty, and there are no timing diagrams.
