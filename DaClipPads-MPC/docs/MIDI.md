# Da Clip Pads: MIDI mapping

MPC gives plugins no direct access to its pads. A plugin track sends the pads (and the sequencer) to its instrument
as **MIDI notes**, and Da Clip Pads maps notes to clips. Everything below is standard MIDI and fully configurable.

## Clips
| Clip | Default note | MPC pad (bank A) |
|---|---|---|
| 1 | C1 (36) | A01 |
| 2 | C#1 (37) | A02 |
| 3 | D1 (38) | A03 |
| 4 | D#1 (39) | A04 |
| 5 | E1 (40) | A05 |
| 6 | F1 (41) | A06 |
| 7 | F#1 (42) | A07 |
| 8 | G1 (43) | A08 |
| 9 | G#1 (44) | A09 |
| 10 | A1 (45) | A10 |
| 11 | A#1 (46) | A11 |
| 12 | B1 (47) | A12 |
| 13 | C2 (48) | A13 |
| 14 | C#2 (49) | A14 |
| 15 | D2 (50) | A15 |
| 16 | D#2 (51) | A16 |

Note names follow Akai's convention (middle C = C3 = 60). The screen grid matches the pad layout: clip 1 is bottom-left.

- **Change a note:** EDIT > MIDI NOTE (or SETTINGS > SELECTED CLIP NOTE), −1 = Off. Or press **LEARN** and hit a
  pad / key: the next note-on is assigned to the selected clip.
- Two clips may share a note: both play (layering).
- **Note-on** triggers the clip by its TRIGGER mode (One Shot, Loop and Toggle as on screen). In **Gate** mode the
  **note-off** stops it.
- **Velocity** scales the level: VEL SENS 0% = always full, 100% = fully velocity-controlled.
- Launch quantise applies to MIDI too. Notes are handled on their exact sample in the audio block.

## Slices
The slices of the **selected clip** play from **SLICE NOTE** upwards: default C3 (60) = slice 1 ... D#5 (91) =
slice 32. Slices play at once (no quantise), as one-shots, with their own pitch and reverse.

## Channel
SETTINGS > MIDI CHANNEL: Omni (default) or 1-16.

## What isn't mapped
- No MIDI CC or program change mapping; use MPC's Q-Links / automation on the plugin parameters instead (every
  control is an automatable parameter, apart from the buttons and the read-only displays).
- The plugin doesn't send MIDI.
