# Marcus Price Jr Radio Ready EQ: design notes

Same engine and graph as EQ7 (notes below), with 50 radio-ready presets and the gold title bar.

## Filters
Each band is built from RBJ "Audio EQ Cookbook" biquads (the same component GlueBus uses):
- **Peak, Low Shelf, High Shelf:** one section; Gain -18 .. +18 dB, Q 0.1 .. 18. A band at 0 dB is skipped
  (bit-transparent).
- **Low Cut / High Cut:** 12 dB/oct = one section with the band's Q (resonance); 24 dB/oct = 2 and 48 dB/oct = 4
  sections at Butterworth Qs (flat passband, -3 dB at the frequency).
- **Notch, Band Pass:** one section; Q sets the width.
- Zero latency. Frequency (in octaves), gain and Q glide over ~20 ms, with coefficients redesigned every 16 samples,
  so sweeping a band doesn't zipper or click. A band switched on after being skipped starts from silence.

## Graph
MPC plugin screens can't draw arbitrary graphics, only filmstrip pictures chosen by a parameter's value. The graph is
therefore made of read-only parameters:
- **Curve:** 64 columns at log-spaced frequencies 20 Hz .. 20 kHz. When any band changes, the plugin evaluates the
  exact combined magnitude response there (from the same coefficients the audio uses) and sends each column that moved
  (audioMasterAutomate), at most ~20 times a second. In the tests it matches the measured response within 0.05 dB.
- **Analyzer:** 32 bands, 2048-point Hann-windowed FFT of the output, ~14 frames/s, peak bin per band, 0 dB = a
  full-scale sine, falls at 30 dB/s. A band is only sent when it moves more than 2 dB, so a steady sound sends nothing.
These parameters report "not automatable", so MPC shouldn't record them.

## What the tests check (`make test`)
- Every type and slope against its expected response (peak +12.00 dB, shelves within 0.05 dB, cuts -12.3 / -24.1 /
  -48.2 dB one octave out, notch below -30 dB, band pass 0 dB at the centre), output gain, flat = bit-exact.
- The curve against the measured response of the Kick Radio Punch preset (within 0.05 dB wherever it is on the
  +-18 dB display); curve traffic only on changes.
- Sweeping a +12 dB band 200 Hz -> 5 kHz and flipping it to -12 dB mid-note makes no step larger than the signal's own.
- Analyzer: a -6 dBFS 1 kHz tone lights the 974-1209 Hz band at about -6 dB (-7.1 with window scalloping), quiet
  elsewhere; no traffic once settled; switching it off clears it once and then sends nothing.
- Power buttons, all 51 presets at 44.1 / 48 kHz, denormals, bypass bit-exact, slow Q-Link turns through every type
  and slope.

## Not verified on an MPC
- How smoothly MPC redraws 96 read-only graph controls (the curve updates only when you edit; the analyzer ~14 times a
  second while the sound changes). If the screen lags, switch the analyzer off.
- The value boxes (a flat filmstrip with a value label on top, dragged vertically) and the two tabs sharing one screen.

## The title
"Marcus Price Jr Radio Ready EQ" is drawn in Great Vibes (Robert Leuschke, SIL Open Font License 1.1, from Google Fonts)
with a gold gradient, shadow and glow, into the background image. The font file isn't part of the project; the
generated images are. To regenerate the skin with the script title, download Great Vibes into `tools/fonts/`
(`GreatVibes-Regular.ttf` or `.woff`); without it the generator uses a plain italic and says so.
