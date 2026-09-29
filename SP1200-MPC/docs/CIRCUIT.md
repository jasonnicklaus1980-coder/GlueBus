# How SP1200 models the SP-1200 circuit

What the plugin does at each stage of the hardware signal path, what the numbers are based on, and where they are
approximate. Everything below is checked by `make test` (see the test numbers at the end).

## 1. Input and ADC
- **Input slider** sets the level into the converter. The SP-1200 has no real anti-alias filter in front of its ADC,
  so the plugin samples the incoming audio directly: anything above 13.02 kHz folds back as aliasing.
- **12-bit converter**: values are clipped at full scale and truncated (floor) to a 12-bit two's-complement code,
  -2048 .. +2047. A signal below 1 LSB (-66 dBFS) collapses to one or two codes, and quiet tails get grainy.

## 2. Sample rate and tuning
- **Clock: 26.04 kHz.** The DAC always runs at 26.04 kHz whatever rate the MPC runs at (44.1, 48 or 96 kHz);
  between clocks the output is held (zero-order hold). That staircase and its images are the SP "crunch".
- **Drop-sample pitch**: the SP-1200 changes pitch by stepping through sample memory faster or slower at the fixed
  DAC clock, with no interpolation. Samples are skipped (tuned up: aliasing) or repeated (tuned down: a lower
  effective sample rate). Tuning is equal-tempered, `ratio = 2^(semitones/12)`.
- **Range: -12 .. +7 semitones.** The slider on the hardware covers +7 / -8; the SET-UP decay/tune default lets a
  sound go a full octave down, so the plugin exposes -12 .. +7 in semitone steps.
- **Tune Mode**
  - *45>33 Grit* (default): the classic trick of sampling a record at 45 rpm and tuning it down in the SP. The pitch you
    hear stays the same, but the audio is sampled at 26.04 kHz x ratio and replayed at 26.04 kHz, so -5 (45 to 33 rpm is
    about -5.4 semitones) samples at 19.5 kHz and -12 at 13.02 kHz.
  - *Pitch*: real drop-sample pitch shifting of the incoming audio. Because an insert works on a live stream rather than
    a one-shot sample, two read heads sweep a 79 ms memory and crossfade for 20 % of the time; the rest of the time one
    head plays alone at the exact tuned rate.

## 3. Decay
The hardware applies decay digitally, before the DAC, so a decaying tail loses resolution. The plugin does the same:
a hit detector (fast/slow envelope, 40 ms hold-off) restarts the decay, the level falls to -60 dB in the slider time,
and the result is re-quantised to 12 bits. At the top of the slider decay is off.

## 4. Output channel filters
The SP-1200 has eight outputs with different filtering (figures from owners' measurements; they vary unit to unit):

| Output | Filter | Plugin |
|---|---|---|
| 1-2 | SSM2044 VCF with a short Z80-generated envelope: open on the hit, then closes to a low cutoff (~250 Hz) | 4-pole OTA ladder, opens to 12 kHz on each hit, closes to *Dyn Floor* with *Dyn Sweep* |
| 3-4 | fixed low-pass ~7.5 kHz | 4-pole at 7.5 kHz |
| 5-6 | fixed low-pass ~10 kHz | 4-pole at 10 kHz |
| 7-8 | unfiltered | only the DAC staircase |

The SSM2044 model is a zero-delay-feedback 4-pole ladder (24 dB/oct) with a tanh-style OTA input stage and the low
fixed resonance of the SP's feedback resistor, gain-compensated so the passband stays at unity.

## 5. Output
A gentle op-amp soft-clip, then *Mix* (dry / SP, exact at both ends) and the *Volume* slider.

## Comparison with the NI Maschine "S1200" engine
NI describes its S1200 sampler mode as tracing the SP-1200's signal flow: 12-bit / 26.04 kHz, aliasing that changes
with tuning, and different filters per output. SP1200 follows the same stages, but as an **insert effect** it works on
whatever you feed it rather than on a sample being played. Use *Pitch* mode or the *45>33 Grit* mode for tuning. This
is an independent model from published specs, not a copy of NI's or E-mu's code.

## What the test checks (`make test`)
- DAC steps per second on Out 7-8: 26,040 +- 150 at host rates 44.1 / 48 / 96 kHz; 13,020 in Grit mode at -12.
- 12-bit: a -80 dBFS sine gives <= 2 output levels; a -40 dBFS sine gives ~41 levels.
- Pitch mode: -12, -8, -4, -1, +1, +4, +7 semitones land within 6 cents of equal temperament (typically ~1 cent).
- Grit mode keeps a 440 Hz tone at 440 Hz at -12, -5 and +7.
- Filters: at 10 kHz, Out 3-4 is darker than Out 5-6, which is darker than Out 7-8; Out 3-4 is flat at 500 Hz;
  each is 1-9 dB down at its cutoff.
- Out 1-2: a 4 kHz burst is >15 dB louder in its first millisecond than once the filter has closed.
- Decay 100 ms: the tone is below -60 dB after 300 ms; with decay off it sustains.
- Mix 0 and Bypass are bit-exact dry; every preset produces finite, bounded output.
