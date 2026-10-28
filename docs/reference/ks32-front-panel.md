# KS-32 front panel: control inventory

Reference for the app's front panel (Plan 7, `docs/ks32app.md`), which mirrors the real KS-32 front
panel: the emulated firmware drives the display and LEDs and reads the buttons, sliders, wheels and
keys. Revised in Plan 7 with the codes and LED numbers found by running the firmware in Plan 6 and
Plan 7.

Sources, cited per item:

- **[M]** KS-32 Service Manual (`datasheets/ks32.pdf`, git-ignored), page 1, *Figure 2 – KS-32 Front
  Panel*. It is a line drawing that shows the display/arrow block, the Sound/Bank/Screen grid and the
  right-hand System block. It leaves out the tracks/transport section (drawn as a zigzag break).
- **[A]** The approved "Front panel - play screen" artboard (2040 x 720 design units), committed as
  `docs/design/mockups/ks32-app-front-panel.dc.html` and transcribed into `src/app/panel_layout.cpp`.
  It follows Figure 2 and fills in the tracks and transport section that Figure 2 leaves out.
- **[F]** The running firmware, OS v3.01: `docs/ks32-hardware-interface.md`, "Front panel: display
  and LEDs", "Front panel: buttons", "Front panel: transport buttons", "Analog inputs" and
  "Keyboard". Codes and LED numbers come from there and from `src/ks32/panel.h`.
- **[P]** A top-view photo of a KS-32 (1548 x 426, watermarked soundprogramming.net; not in the
  repository). It shows the whole panel, but small labels are blurry at this resolution.

**Layout source: Figure 2 and the artboard.** Where a control sits, and its label, come from [M] and
[A]. The photo is for orientation only: it placed the tracks and transport section, which Figure 2
does not draw, and nothing else is taken from it. Its readings are kept below, marked as such, only
where they differ from Figure 2.

**Codes.** "Code" is the 7-bit button code the panel sends on DUART channel B: a press is
`code | 0x80`, then `00`; a release is `code`, then `00` [F]. "LED" is the LED number in the
firmware's `8D`/`8E`/`8F nn` commands, 0 to 14 [F]. Both are hexadecimal for codes and decimal for
LEDs, as in the interface doc.

Left to right across the panel:

## 1. Performance controls (left of the keys)

| Control | Input | Source | Notes |
|---|---|---|---|
| **Pitch bend wheel** | Analog select 0 (`Analog::PitchWheel`) | [A] [P] [F] | Inverted; the firmware takes its centre at power-on. Rests at 0x200 (K13). |
| **Modulation wheel** | Analog select 2 (`Analog::ModWheel`) | [A] [P] [F] | Inverted: 127 near raw 0, 0 from 0x300. Rests at 0x3FF (wheel down). |
| **Keyboard**, 76 keys | Panel link, notes 28 to 103 (E1 to G7) | [M] [A] [F] | Weighted, with a mono pressure board [M: *76 Note Keyboard*, *Mono Pressure Test*]. A key is `code \| 0x80, strike` on the panel link (K12); pressure is one analog input (select 1), not per key. |

The manual's table of contents also lists a wheel assembly (*Replacing the Wheel Assembly*).

## 2. Sliders

| Control | Input | Source | Notes |
|---|---|---|---|
| **Volume** | Analog `Analog::Volume` | [A] [P] [F] | Master volume. `Vol=` reads 0 at raw 0 and 127 at 0x3FF; rests at maximum. |
| **Data Entry** | Analog select 3 (`Analog::DataEntry`) | [A] [P] [F] | Value slider for the selected parameter. `Knob=` reads 0 to 255; rests at mid (0x16E). |

## 3. Display and value buttons

| Control | Code | Source | Notes |
|---|---|---|---|
| **Display** | — | [M] [A] [F] | **2 rows of 16 characters** [F, High]. Codes 0x20–0x7E are ASCII; 0x00–0x1F and 0x7F are characters without known glyphs (A7). |
| **Up Arrow / Yes** | 0D | [M] [A] [F] | The manual calls it "Up•Yes Arrow" (it answers "Reinitialize sound & seq RAM?"). |
| **Down Arrow / No** | 0C | [M] [A] [F] | Repeats while held. |
| **Left arrow** | 1C | [M] [A] [F] | Moves the edit field (Medium for which of the two is Left). |
| **Right arrow** | 1D | [M] [A] [F] | |
| **Compare** | 2D | [M] [A] [F] | Medium. |
| **Enter / Save** | 2C | [M] [A] [F] | Printed "Enter•Save" [P]. |

## 4. Sound / Bank / Screen grid

### Mode buttons (left column of the grid)

| Control | Code | LED | Source | Notes |
|---|---|---|---|---|
| **Edit Sound** | 35 | 0 | [M] [A] [F] | Selects the lower label row (sound parameters) for the Bank buttons. |
| **BankSet** | 39 | — | [M] [A] [F] | Sits next to the Bank row. In Select Sound it switches between ROM and INT (Medium). |
| **Select Sound** | 34 | 8 | [M] [A] [F] | Lit after the boot, on the play screen. |

### Printed label rows above the Bank buttons

These name what the 10 Bank buttons do in the two edit modes. The upper row is boxed with **Edit
Seq/Preset**, the lower row with **Edit Sound** [M] [A]. The firmware's pages match both rows [F]:
in Edit Sound mode Bank 0–9 open wave, Oct/Semi/Fine, Env1, LFO, filters, Env2, Amp, Vol/Boost,
effect and "Store Sounds"; in Edit Seq/Preset mode Locate, Loop/Countoff, Click, Song, "Create
SEQ/Pset", "Event Edit Track", "Quantize Track", the Mute status, effect and "Store Sequences".

| Bank button | Upper row (Edit Seq/Preset) [M] [A] | Lower row (Edit Sound) [M] [A] |
|---|---|---|
| 0 | Locate | Wave |
| 1 | Control | Pitch |
| 2 | Click | Env1 |
| 3 | Song | LFO |
| 4 | Seq | Filters |
| 5 | Event *(under a "Tracks" bracket)* | Env2 |
| 6 | Quantize *(under "Tracks")* | Amp |
| 7 | Mute *(under "Tracks")* | Output |
| 8 | Effect | Effect |
| 9 | Store Seqs | Store Sounds |

The photo [P] seems to show two-line labels on several upper-row buttons ("Song / Create", "Seq /
Create" and the like), which may be a later panel printing. The firmware's page names fit Figure 2's
labels, which the app uses.

### Bank row (10 buttons)

| Button | Code | Label | Source |
|---|---|---|---|
| 0 | 25 | Internal | [M] [A] |
| 1 | 15 | ROM A | [M] [A] |
| 2 | 05 | ROM B | [M] [A] |
| 3 | 37 | Card A | [M] [A] |
| 4 | 07 | Card B | [M] [A] |
| 5 | 0F | — | [M] [A] |
| 6 | 17 | — | [M] [A] |
| 7 | 27 | — | [M] [A] |
| 8 | 1F | Drums | [M] [A] |
| 9 | 2F | Drums | [M] [A] |

In Select Sound, from ROM 55, Bank n gives ROM n5, and Banks 8 and 9 give DRUM sounds [F]. The photo
seems to read the labels of Banks 1–3 differently ("ROM", "Card A", "Card B"); Figure 2's are used.

### Screen row (10 buttons, numbered 0–9)

Labels below the buttons are the per-track parameter screens; in Edit Track mode the firmware opens
these pages [F]. From ROM 55, Screen n gives ROM 5n.

| Screen | Code | Label | Source |
|---|---|---|---|
| 0 | 24 | Volume / Pan | [M] [A] [F] |
| 1 | 14 | Output (Bus) | [M] [A] [F] |
| 2 | 04 | Key Range | [M] [A] [F] |
| 3 | 36 | X-pose (Transpose) | [M] [A] [F] |
| 4 | 06 | Channel / Program | [M] [A] [F] |
| 5 | 0E | MIDI Status | [M] [A] [F] |
| 6 | 16 | Pressure Type | [M] [A] [F] |
| 7 | 26 | Sustain Pedal | [M] [A] [F] |
| 8 | 1E | Timbre | [M] [A] [F] |
| 9 | 2E | Release | [M] [A] [F] |

## 5. Sequencer / preset section

Figure 2 does not draw the tracks and transport; their places are from the artboard, which took them
from the photo.

| Control | Code | LED | Source | Notes |
|---|---|---|---|---|
| **Edit Seq/Preset** | 38 | 9 | [M] [A] [F] | Selects the upper label row for the Bank buttons; the Locate page. |
| **Edit Track** | 1A | 6 | [M] [A] [F] | Lights LEDs 6 and 9. |
| **Select Seq/Preset** | 19 | 1 | [M] [A] [F] | "PRESET 00 Loc=00". |
| **Track 1** | 10 | 2 | [A] [P] [F] | Two rows of four under a "Tracks" heading. A track button lights LED 1 and its own LED. |
| **Track 2** | 20 | 3 | [A] [P] [F] | |
| **Track 3** | 18 | 4 | [A] [P] [F] | |
| **Track 4** | 28 | 5 | [A] [P] [F] | |
| **Track 5** | 11 | 10 | [A] [P] [F] | |
| **Track 6** | 21 | 11 | [A] [P] [F] | |
| **Track 7** | 08 | 12 | [A] [P] [F] | |
| **Track 8** | 30 | 13 | [A] [P] [F] | |
| **Record** | not found | — | [A] [P] | No code changes the display, the LEDs or MIDI out (A14). The app draws it dimmed. |
| **Stop / Continue** | 29 | — | [A] [P] [F] | Sends MIDI Stop (`FC`); shares a "Continue" label with Play. Medium for the name. |
| **Play** | 09 | — | [A] [P] [F] | Sends MIDI Start (`FA`). Medium for the name. |

## 6. Right-hand group

| Control | Code | LED | Source | Notes |
|---|---|---|---|---|
| **Replace Track Sound** | 2A | 14 | [M] [A] [F] | The manual shows "(Replace w/FX)". It acts on the release; LED 14 blinks over "Trk01 INT 00" (Medium). A bracket links it to the Track buttons. |
| **Make Default Preset** | 32 | — | [M] [A] [F] | Secondary function "Clone Preset" ("Copy FROM Preset" from Edit Seq/Preset). Medium. |
| **System/MIDI** | 0A | 7 | [M] [A] [F] | "Master Tune= +00 / Global Bend= 02". |

## 7. LED numbers

All 15 LEDs of the firmware are the 15 LED controls above [F]. High for the tracks and for LEDs 0, 1,
7, 8 and 9; Medium for 6 and 14.

| LED | Control |
|---|---|
| 0 | Edit Sound |
| 1 | Select Seq/Preset |
| 2–5 | Track 1–4 |
| 6 | Edit Track |
| 7 | System/MIDI |
| 8 | Select Sound |
| 9 | Edit Seq/Preset |
| 10–13 | Track 5–8 |
| 14 | Replace Track Sound |

## 8. Printed panel graphics (not controls)

- **Default Preset Settings** table [P], one column per Track 1–8, with rows **Status** (LOCAL /
  MIDI), **Key Zone** (Full / Lower / Upper), **Channel** and **Sound** (Current / Bass / Previous …).
  It is too blurry to transcribe reliably **(uncertain)**, and the app does not draw it. The
  firmware's default-preset data is the authoritative source.
- **Rear-panel jack legend** printed along the top edge [P]: Right / Left (Mono) audio outputs,
  Pedal/CV, Memory Card, Foot Switch, MIDI In / Out / Thru, Power. Not drawn in the app.

## 9. Button combinations

The firmware reads chords (one button held while another is pressed), so the UI sends press and
release events, not clicks. The app's chord latch (right-click or Shift-click, A9) holds one button
while another is clicked.

- **Software version check** [M, page 2]: press **Edit Seq/Preset**, then hold **Bank 9** and press
  **Screen 1**. The display reads "ENSONIQ KS-32 / OS Version 3.01" [F]. The mode button must be
  released before Bank 9 is pressed [F].
- **Reinitialize sound & sequence RAM** [M, page 1]: press **Edit Seq/Preset**, hold **Bank 9**,
  press **Screen 9**, then answer **Up•Yes Arrow** [F: the prompt appears; Down/No leaves it without
  erasing].
- **Analog Tests** [M]: Edit Seq/Preset, then Bank 9 held with **Screen 0** [F].
- **Replace Track Sound with FX** [P]: hold Replace Track Sound and press a Track button
  **(uncertain wording; not tested)**.

## 10. Open points

Resolved by running the firmware (Plan 6 and Plan 7):

- **How each control reaches the 68000.** Not a matrix the CPU scans: the keypad/display board talks
  to the CPU over DUART channel B. Buttons and keys arrive as two-byte events with the codes above,
  the display and LEDs are driven by command bytes (`87` cursor, `8C` blank, `8D`/`8E`/`8F` LED
  on/off/blink), and the sliders and wheels are analog inputs read through OTIS's converter by a
  multiplexer on DUART OP3..OP0 [F]. How the panel board scans its own buttons is not visible to the
  firmware and does not matter to the emulation.
- **The display geometry**: 2 x 16 characters [F, High].
- **The photo's label wording against Figure 2**: the firmware's page names fit Figure 2's labels, so
  the app uses Figure 2's.

Still open:

- **The character set** for 0x00–0x1F and 0x7F, and the custom character the firmware defines at
  boot (`93`, which looks like a note). Drawn blank in the app, with a hex overlay (A7).
- **The backlight** and the display's colour: not visible to the firmware. The app uses the
  artboard's amber.
- **Record**: no code found (A14).
- **Continue**: code 29 sends MIDI Stop. Whether Continue is a Stop + Play chord or a second press
  was not tested, and no MIDI Continue (`FB`) has been seen.
- **The blink rate** of blinking LEDs and display cells: not measured (A8).
- **The foot switches, the sustain pedal jack and the CV pedal**: no input for the foot switches
  was found; the CV pedal is an analog input that rests unplugged. Not on the app's panel.
