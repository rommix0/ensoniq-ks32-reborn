# KS-32 front panel: control inventory

Reference for the future UI (VST / app front end). The UI is expected to mirror the real KS-32 front
panel, with the emulated firmware driving the display and LEDs and reading the buttons and sliders.

Sources, cited per item:

- **[M]** KS-32 Service Manual (`datasheets/ks32.pdf`, git-ignored), page 1, *Figure 2 – KS-32 Front
  Panel*. It is a line drawing that shows the display/arrow block, the Sound/Bank/Screen grid and the
  right-hand System block. It leaves out the tracks/transport section (drawn as a zigzag break).
- **[P]** Top-view photo `docs/Ensoniq_KS32_Top.png` (1548×426, watermarked soundprogramming.net).
  It shows the whole panel, but small labels are blurry at this resolution.

Where the two disagree, or a label is unreadable, it is marked **(uncertain)**. Nothing here says how a
control is wired (keypad matrix row/column, LED driver bit, display controller); that comes from the
firmware's keypad, LED and display code in the firmware-emulation plan. The service manual's
*Keypad/Display Boards* notes (page 3), *Display Self-test Mode* (page 7) and *Test Procedures*
(pages 15–18) are the places to start there.

Left to right across the panel:

## 1. Performance controls (left of the keys)

- **Pitch bend wheel** and **modulation wheel** [P]. The manual's table of contents also lists a
  wheel assembly (*Replacing the Wheel Assembly*).
- The keyboard is **76 keys**, weighted, with a mono pressure (channel aftertouch) board [M: *76 Note
  Keyboard*, *Mono Pressure Test*]. "Weighted Action MIDI Studio" is printed under the logo [P].

## 2. Sliders

| Control | Source | Notes |
|---|---|---|
| **Volume** | [P] | Master volume slider. |
| **Data Entry** | [P] | Value slider for the selected parameter. |

## 3. Display and value buttons

| Control | Source | Notes |
|---|---|---|
| **Display** | [M] [P] | Two-line character display, shown lit in the photo. The size is not checked: it looks like about 2 × 16 characters [P] **(uncertain)**. The firmware's display driver will settle it. |
| **Up Arrow / Yes** | [M] [P] | The manual calls it "Up•Yes Arrow" (it answers "Reinitialize sound & seq RAM?"). |
| **Down Arrow / No** | [M] [P] | |
| **Left arrow** | [M] [P] | |
| **Right arrow** | [M] [P] | |
| **Compare** | [M] [P] | |
| **Enter / Save** | [M] [P] | Printed "Enter•Save" [P]. |

## 4. Sound / Bank / Screen grid

### Mode buttons (left column of the grid)

| Control | LED | Source | Notes |
|---|---|---|---|
| **Edit Sound** | yes | [M] [P] | Selects the lower label row (sound parameters) for the Bank/Screen buttons. |
| **BankSet** | — | [M] [P] | Sits next to the Bank row. |
| **Select Sound** | yes | [M] [P] | Its LED is lit in the photo [P]. |

### Printed label rows above the Bank buttons

These name what the 10 Bank buttons do in the two edit modes. The upper row is boxed with **Edit
Seq/Preset**, the lower row with **Edit Sound** [M].

| Bank button | Upper row (Edit Seq/Preset) [M] | Lower row (Edit Sound) [M] |
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

In the photo the upper row has two-line labels on several buttons, something like "Song / Create",
"Seq / Create", "Track / Create", "Quantize / Track", "Mute / Tracks", "Effects" **(uncertain:
unreadable at this resolution)**. The photo may show a later panel printing than the manual drawing.

### Bank row (10 buttons)

The labels above the buttons name the bank each one selects in Select Sound mode:

| Button | Label | Source |
|---|---|---|
| 0 | Internal | [M] [P] |
| 1 | ROM A | [M]; the photo seems to read just "ROM" **(uncertain)** |
| 2 | ROM B | [M]; the photo seems to read "Card A" here **(uncertain)** |
| 3 | Card A | [M]; the photo seems to read "Card B" here **(uncertain)** |
| 4 | Card B | [M] |
| 5–7 | — | [M] [P] |
| 8 | Drums | [M] [P] |
| 9 | Drums | [M] [P] |

### Screen row (10 buttons, numbered 0–9)

Labels below the buttons are the per-track or per-preset parameter screens:

| Screen | Label | Source |
|---|---|---|
| 0 | Volume / Pan | [M] [P] |
| 1 | Output (Bus) | [M]; the photo reads "Output Bus" [P] |
| 2 | Key Range | [M] [P] |
| 3 | X-pose (Transpose) | [M]; the photo reads "Trans-pose" [P] |
| 4 | Channel / Program | [M] [P] |
| 5 | MIDI Status | [M] [P] |
| 6 | Pressure Type | [M] [P] |
| 7 | Sustain Pedal | [M] [P] |
| 8 | Timbre | [M] [P] |
| 9 | Release | [M] [P] |

## 5. Sequencer / preset section

| Control | LED | Source | Notes |
|---|---|---|---|
| **Edit Seq/Preset** | yes | [M] [P] | Selects the upper label row for the Bank buttons. |
| **Edit Track** | yes | [M] [P] | |
| **Select Seq/Preset** | yes | [M] [P] | A bracket in the photo joins Edit Track and Select Seq/Preset, labelled something like "Lock" **(uncertain)**. |
| **Track 1–8** | yes, one each | [P] | Two rows of four under a "Tracks" heading. Not in the manual drawing. |
| **Record** | — | [P] | |
| **Stop** | — | [P] | Stop and Play share a "Continue" label underneath, probably Stop + Play = continue **(uncertain)**. |
| **Play** | — | [P] | |

## 6. Right-hand group

| Control | LED | Source | Notes |
|---|---|---|---|
| **Replace Track Sound** | yes | [M] [P] | The manual shows "(Replace w/FX)". The photo's note reads something like "To include FX hold & press a Track button" **(uncertain)**. A bracket links it to the Track buttons. |
| **Make Default Preset** | — | [M] [P] | Secondary function "Clone Preset". |
| **System/MIDI** | yes | [M] [P] | |

## 7. Printed panel graphics (not controls)

- **Default Preset Settings** table [P], one column per Track 1–8, with rows **Status** (LOCAL /
  MIDI), **Key Zone** (Full / Lower / Upper), **Channel** and **Sound** (Current / Bass / Previous …).
  It is too blurry to transcribe reliably **(uncertain)**. The firmware's default-preset data is the
  authoritative source.
- **Rear-panel jack legend** printed along the top edge [P]: Right / Left (Mono) audio outputs,
  Pedal/CV, Memory Card, Foot Switch, MIDI In / Out / Thru, Power.

## 8. Button combinations the UI must support

The firmware reads chords (one button held while another is pressed), so the UI needs press and
release events, not just clicks:

- **Software version check** [M, page 2]: press **Edit Seq/Preset**, then hold **Bank 9** and press
  **Screen 1**. The display then reads "ENSONIQ KS-32 OS Version X.XX".
- **Reinitialize sound & sequence RAM** [M, page 1]: press **Edit Seq/Preset**, hold **Bank 9**,
  press **Screen 9**, then answer **Up•Yes Arrow**.
- **Replace Track Sound with FX** [P]: hold Replace Track Sound and press a Track button
  **(uncertain wording)**.

## 9. Open points for the UI and firmware plans

- How each button, LED and slider reaches the 68000: the keypad scan matrix, LED drive, slider ADC
  channels, and the display controller and its character set. These come from the firmware.
- The exact display geometry, character set and backlight.
- Whether "Continue" is a Stop+Play chord or a shifted function.
- Whether the photo's label wording (later panel printing) or the manual drawing matches OS v3.01.
