# Da DJ Decks: design notes

## Why an insert effect
MPC hosts VST2 effects as inserts; the plugin keeps MPC's own audio (MPC IN) and adds the two decks. Loading it on
the master or an audio track gives a DJ mixer that sits alongside the sequencer.

## Threads
- **Loader thread** (pthread, started at open, joined at close). It scans `/sdcard/DJ` (or `$DJ_FOLDER`, one
  sub-folder level, up to 256 WAVs, sorted by name), reads WAVs into 16-bit stereo, and analyses the beat.
  - The library is swapped under a mutex that only the loader and the UI thread take.
  - A finished track goes into a per-deck atomic `pending` slot.
- **Audio thread**:
  - takes `pending` at a block boundary
  - moves the old track to a `retired` slot, which the loader frees
  - never allocates, frees or touches the SD card
- **UI thread** (`setParameter`): button presses become atomic requests (load, cue, sync, nudge, rescan), and the
  audio thread serves them at the next 64-sample chunk.

## Beat analysis (src/Wav.h)
- **Onset strength:** 10 ms hops over the first 90 s, looking for rises in log energy of a <150 Hz band plus a
  high band. The 1 s average is removed.
- **Tempo:** the onsets are folded onto one beat (64 phase bins) for every tempo from 78 to 156 BPM in 0.05 steps,
  then refined ±0.06 in 0.002 steps. The tempo whose strongest phase gathers the most onset energy wins, and that
  phase becomes the first-beat offset.
- **Tests:** synthetic kick/hat tracks at 95, 100, 120 and 128 BPM (16-bit, 24-bit mono at 48 kHz, float) are
  detected within 0.15 BPM, with the first beat within 30 ms.

## Deck (src/Deck.h)
- **Playback:** 4-point Hermite interpolation. The step is file rate / host rate × (1 + pitch), so tempo and key
  change together.
- **Clicks:** jumps (cue, nudge, sync, loop wrap) crossfade the old and new read positions over 3 ms, and
  play/stop ramps take 5 ms.
- **Loops** are snapped to the beat grid.
- **SYNC** picks the pitch (×1, ×2 or ×0.5 of the other deck's effective BPM) that is closest, and widens the range
  if needed. It then shifts this deck by the beat-phase difference: now if playing, otherwise when PLAY is pressed.
  If the deck is parked on its first beat, the shift goes forward instead of before the start of the track.
- **EQ:** a 3-band isolator from two Linkwitz-Riley 24 dB crossovers (300 Hz, 4 kHz). −24 dB on a band is a full
  kill.
- **Filter:** a 12 dB resonant low-pass (20 kHz → 60 Hz) or high-pass (20 Hz → 8 kHz), with a dead zone at the
  centre.
- **Gains** are smoothed over 10 ms, and the fader law is a square curve.

## Mixer and restore
- **Crossfader:** smooth is constant-power with both decks at full level in the centre; cut is a fast cut at the
  ends.
- **MPC IN:** at 100% the input is passed through bit-exact (tested).
- **Restore:** `Loaded Track` (a hidden parameter, library index + 1) is set by the plugin when a load completes.
  When MPC restores it with a project, that track is loaded again.

## glibc
The ARM toolchain's `fmod` binds to GLIBC_2.38, but MPC OS has 2.34, so the code uses its own `fmodd`. pthread
symbols bind to GLIBC_2.34, where libpthread is part of libc.

## Skin
- **Layout:** one tab with deck A, the mixer and deck B.
- **Filmstrips:** platters (150 px, 128 rotation frames), pitch and channel faders, a crossfader (horizontal drag)
  and knobs.
- **LED parts:** 44-segment progress bars, beat lights and meters are one solid colour per frame, so they are right
  whatever frame offset MPC uses.
- **Value boxes** (track, loop, range) are drawn in the background, with transparent drag strips.
- **Memory:** about 78 MB of images when decoded.
