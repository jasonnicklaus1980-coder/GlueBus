# The Remixer - Phase 2 status: analysis of a recording

## What it does (host build, `make test`)
- `rm_wav_read_mono`: reads 16/24/32-bit PCM and 32-bit float WAV, mixes to mono. Rejects non-WAV and missing files.
- `rm_estimate_bpm`: tempo from an onset envelope and autocorrelation over 60 to 200 BPM, with a confidence value.
- `rm_analyze`: one report with tempo, key, chord segments (from `cc_analyze`), and polyphonic notes (from `vi_poly_notes`).
- `remixer_analyze recording.wav [outdir]`: prints the report, writes `chords.txt` and `melody.mid`.
- The analysis modules are vendored unchanged from the Velvet Interpolator project (see `src/analysis/VENDORED.md`).

## Checked on synthetic audio with known answers
| Check | Result |
|---|---|
| 120 BPM click track | 120.12 BPM |
| 96 BPM click track | 95.81 BPM |
| Silence | no tempo |
| C Am F G progression, 2 s each | all four chords in the right blocks |
| C major scale + C E G triad | key C major (confidence 1.00), triad notes found |
| 24-bit / 16-bit / float WAV round trips | sample error 9e-8 / 2e-5 / 0 |
| Non-WAV and missing files | rejected |

## Known limits
- **Tempo needs a beat.** A sustained chord progression has no onsets, so the tempo reading is meaningless there.
  The CLI shows the confidence; treat a low value as "no tempo found".
- **Chord boundaries** come from analysis frames of 2048 samples, so each boundary is accurate to about 46 ms.
- **Synthetic audio only.** Real recordings (drums, reverb, noise, a sampled piano) will score lower. No real-recording
  accuracy has been measured yet.
- **Not on the MPC yet.** The analysis runs on the host only. The plugin build is unchanged, and nothing here has run
  on the device.
- **Speed and memory** on the MPC X are unmeasured. The poly and chord analysis use a 65536-note cap and static buffers.

## Next steps
1. Test on real recordings: a drum loop for tempo, a piano or keys take for chords and key.
2. Measure analysis time per minute of audio on ARM before moving it into the plugin.
3. Run analysis in a background thread inside the plugin, after a recording is saved, never on the audio thread.
