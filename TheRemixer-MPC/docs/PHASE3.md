# The Remixer - Phase 3 status: editable MIDI from the analysis

## What it does (host build, `make test`)
- `rm_chord_notes`: voices each detected chord as MIDI (root position or inversion 0..2), transposed by any number of
  semitones. Chords that leave the MIDI range are skipped.
- `rm_melody_notes`: the polyphonic melody, transposed, optionally quantized to a grid (e.g. 0.25 beat = 16ths).
  Notes that collapse to nothing on the grid are dropped, as are notes out of MIDI range.
- `rm_quantize`: snaps a time to the grid at a tempo.
- `remixer_analyze -t N -i N -g BEATS recording.wav outdir`: writes `chords.txt`, `chords.mid` and `melody.mid`.
  The MIDI tempo and grid use the detected tempo only when its confidence is 0.7 or higher (a click track scores about
  0.9; a sustained chord progression scores about 0.5). Otherwise they use 120 BPM, and the CLI says so.
- Pad output: `padsend` (vendored unchanged from Velvet Interpolator) turns a note schedule into VST MIDI events
  sample-accurately. Tested here; not yet wired into the plugin.

## Pad layout (decided here; the MPC side is not verified)
- **Chords on bank A:** each distinct chord gets its own pad, in order of first appearance, up to 16. A chord past
  the 16th has no pad. `N` (no chord) has none either.
- **Melody on bank B:** each distinct pitch (after transposition) gets a pad, sorted from the lowest pitch. Up to 16
  pitches. Beyond 16, the lowest 16 get pads and the rest fold onto the nearest pad; the count is written to `pads.txt`.
- **Pad notes:** pad *p* sends MIDI note 36 + *p* (pads 1..16 are notes 36..51). This is an **assumption** about the
  MPC's default bank; check it on the device.
- Chords and melody are on different banks, so they never share a pad. Putting both on one bank would leave 16 pads
  for both, which is too few for a progression and a melody together.
- `remixer_analyze` writes this table as `pads.txt`: bank, pad, note, role, content.

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
| Pad layout: chords (order, repeats, N, 16-pad limit), melody (sorted, transposed, folding past 16), pad notes | 22 checks pass |

## Known limits
- **No piano-roll editor or MIDI import yet.** Editing is by re-running with different options, not in a UI.
- **The MIDI file is only checked by a parser I wrote**, not by a DAW or the MPC. Loading it on the MPC is still open.
- **Melody notes come from the polyphonic transcriber**, so they inherit its limits on real material (see PHASE2).
- **Pad mapping** (which notes go to which pad, and how the MPC receives them) is not designed yet; `padsend` only
  produces the events.
- **120 BPM fallback.** When the tempo is not trusted, the MIDI is at 120 BPM whatever the song's real tempo is.

## Next steps
1. Check the pad notes (36..51 from pads 1..16) and the bank switching on the MPC.
2. Check the MIDI files on the MPC (import and playback).
3. Add the analysis and pad output to the plugin after the Phase 1 device checks.
