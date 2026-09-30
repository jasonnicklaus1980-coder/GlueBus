# How ASR10 models the Ensoniq ASR-10

What the plugin does at each stage, what each number is based on, and where it is approximate. `make test` checks
every point below (numbers at the end).

## 1. Converters and sampling rate
- The ASR-10 samples 16-bit at **29.7619 kHz ("30k") or 44.1 kHz** through **64x-oversampling sigma-delta**
  converters. A sigma-delta converter's decimation filter removes everything above roughly half the sampling rate
  before sampling, so unlike the SP-1200 **nothing aliases**. Modelled as an 8-pole Butterworth low-pass at 0.45 x
  the sampling rate (13.4 kHz at 30k), then 16-bit rounding with a hard clip at full scale.
- Producers describe the ASR-10 as dark and gritty mostly because it was used at 30k; the band limit is that
  high-end roll-off. At 44.1k the path is flat.

## 2. Playback and tuning (OTTO)
- Samples are played by Ensoniq's OTTO voice chip, which changes pitch by stepping through memory at a different
  rate and **linearly interpolates** between neighbouring samples (MAME's ES5505/ES5506 emulation:
  `sample1 * (1 - frac) + sample2 * frac`). Linear interpolation dulls the top a little and leaves faint images: a
  softer grit than the SP-1200's drop-sample playback.
- **Tune** -12 .. +12 semitones and **Fine** -50 .. +50 cents, equal-tempered.
- **Pitch** mode: as an insert works on a live stream, two interpolating read heads sweep a 4096-sample memory
  (93 ms at 44.1k, 138 ms at 30k) and crossfade for 20 % of the time; one head plays alone otherwise.
  **Rate** mode: the tune changes the sampling rate and the pitch stays, like sampling a record at another speed and
  retuning it; the band limit follows (e.g. 30k at -12 = 14.9 kHz, band-limited to 6.7 kHz).

## 3. Filters (OTTO)
The chip has four one-pole digital filter stages per voice with two cutoffs (K1 = Filter 1, K2 = Filter 2) and
**no resonance**. Poles 1-2 are always low-pass on Filter 1; poles 3-4 follow the mode bits (from MAME's chip code):

| Mode | Pole 3 | Pole 4 | Chip bits |
|---|---|---|---|
| LP2 / HP2 | high-pass, Filter 2 | high-pass, Filter 2 | 0 |
| LP3 / HP1 | low-pass, Filter 1 | high-pass, Filter 2 | LP3 |
| LP2 / LP2 | low-pass, Filter 2 | low-pass, Filter 2 | LP4 |
| LP3 / LP1 | low-pass, Filter 1 | low-pass, Filter 2 | LP3+LP4 |

A high-pass stage is the chip's `x - lowpass(x)`. Slopes are 6 dB/oct per pole, up to 24 dB/oct. When the mode
changes, the re-purposed poles start by passing their input and settle from there, so there is no click.

## 4. Output
16-bit DAC rounding, then **Mix** (dry / ASR, exact at both ends) and **Volume**. Not modelled: the ESP effects
chip (reverbs, delays; the MPC has its own), the analogue output stage.

## Sources
- Specs (16-bit, 29.7619 / 44.1 kHz, 64x sigma-delta, 2 digital filters in series up to 4-pole, 6-24 dB/oct):
  Wikipedia "Ensoniq ASR-10", Vintage Synth Explorer, Sonicstate.
- OTTO chip (interpolation, filter poles and mode bits, 18-bit+ internal accuracy): MAME `src/devices/sound/es5506.cpp`,
  Wikipedia "Ensoniq ES-5506 OTTO".
- 30k character: Gearspace "ASR-10 users: 44.1 or 30kHz?".
- Not verified against recordings of a real ASR-10; the filter cutoff scale is the plugin's own (Hz), not the ASR's
  0-127 parameter values.

## What the test checks (`make test`)
- 44.1k: flat at 1 kHz and 15 kHz. 30k: 1 kHz flat, 10 kHz within 3 dB, 15 kHz below -20 dB, 18 kHz below -30 dB;
  an 18 kHz tone makes no alias at 11.76 kHz (below -40 dB).
- 16-bit: a -100 dBFS sine gives at most 3 output levels.
- Pitch mode at 30k and 44.1k: -12, -5, -1, +3, +7, +12 semitones within 6 cents (typically under 1); Fine -37 and
  +25 cents within 3 cents.
- Rate mode keeps a 440 Hz tone at 440 Hz and band-limits: 30k at -12 passes 5 kHz (about -3 dB, linear-interpolation
  droop) and cuts 8 kHz below -15 dB.
- Filters at 1 kHz: 2, 3 and 4 poles each cut 4 kHz about 12 dB more; the 4-pole drops another 15+ dB by 8 kHz;
  no gain above 0 dB around the cutoff (no resonance); high-pass 500 Hz cuts 100 Hz below -20 dB.
- Mix 0 and Bypass bit-exact dry; all presets finite and bounded at 44.1 and 48 kHz; slow Q-Link turns step through
  every Tune and Fine value; changing Tune off 0 or the filter mode mid-note doesn't click.
