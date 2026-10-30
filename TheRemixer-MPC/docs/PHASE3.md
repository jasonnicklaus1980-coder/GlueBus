# The Remixer - Phase 3 status: editable MIDI from the analysis

## What it does (host build, `make test`)
- `rm_chord_notes`: voices each detected chord as MIDI (root position or inversion 0..2), transposed by any number of
  semitones. Chords that leave the MIDI range are skipped.
- `rm_melody_notes`: the polyphonic melody, transposed, optionally quantized to a grid (e.g. 0.25 beat = 16ths).
  Notes that collapse to nothing on the grid are dropped, as are notes out of MIDI range.
- `rm_quantize`: snaps a time to the grid at a tempo.
- `remixer_analyze -t N -i N -g BEATS recording.wav outdir`: writes `chords.txt`, `chords.mid` and `melody.mid`.
  The MIDI tempo and grid use the detected tempo only when its confidence is 0.5 or higher; otherwise 120 BPM, and the
  CLI says so.
- Pad output: `padsend` (vendored unchanged from Velvet Interpolator) turns a note schedule into VST MIDI events
  sample-accurately. Tested here; not yet wired into the plugin.

## Checked
| Check | Result |
|---|---|
| Quantize 0.26 s and 0.49 s on a 1/4-beat grid at 120 BPM | 0.25 s and 0.5 s |
| C major root position | 48, 52, 55 |
| A minor root position | 57, 60, 64 |
| First inversion of C major | 52, 55, 60 |
| Transpose +12 | octave up; out-of-range chords skipped |
| Melody: collapsed note dropped, out-of-range note dropped | yes |
| Written chord MIDI, parsed back | 6 note-ons, 6 note-offs, pitches match the voicing |
| Pad schedule | 6 note-ons, first on its exact sample; stop sends all note-offs |
| Both test programs under `qemu-arm` | pass |

## Known limits
- **No piano-roll editor or MIDI import yet.** Editing is by re-running with different options, not in a UI.
- **The MIDI file is only checked by a parser I wrote**, not by a DAW or the MPC. Loading it on the MPC is still open.
- **Melody notes come from the polyphonic transcriber**, so they inherit its limits on real material (see PHASE2).
- **Pad mapping** (which notes go to which pad, and how the MPC receives them) is not designed yet; `padsend` only
  produces the events.
- **Chord MIDI at 120 BPM** when the tempo is not trusted, so the file starts on the grid but is not in the song's tempo.

## Next steps
1. Decide the pad layout: one chord per pad, or one scale row for a melody, and how the MPC receives the notes.
2. Check the MIDI files on the MPC (import and playback).
3. Add the analysis and pad output to the plugin after the Phase 1 device checks.
