# Da Space: design notes

## Engine (src/Reverb.h)
Floats throughout, for speed on the MPC's ARM CPU. Everything is sample-rate independent.

- **FDN (Room, Hall, Ice, Meta, Reflex):**
  - 8 delay lines (31–74 ms × a per-algorithm size range) with a Householder feedback matrix.
  - Lines are whole-sample when unmodulated and use cubic reads when modulated. Linear interpolation lost high end
    on every pass and shortened the decays.
  - Each line has a Jot decay filter. Its DC gain comes from RT60, and its Nyquist gain from a shorter HF RT60 set
    by Damping (the ratio is (damp / 20 kHz)^0.85).
  - Input diffusion (4 allpasses per side) and early reflections: 10 taps per side, spaced by Size, filtered by the
    low cut.
  - Per-algorithm settings:

    | Algorithm | Extras |
    |---|---|
    | Room | early reflections |
    | Hall | lighter early reflections |
    | Ice | high-passed input (250 Hz) and 4 resonators on the tail (2.6–8.3 kHz, pushed up by Color) |
    | Meta | 3 ms modulation depth and output diffusion |
    | Reflex | early-reflection dominated, tail RT = decay × 0.2 (max 1.2 s) |

- **Plate:** a Dattorro tank. Four input diffusers feed two cross-fed branches, each with a modulated allpass (cubic
  read), delay, damping, a decay stage and a second allpass. There are 7 output taps per side. The decay comes from
  RT60 over the loop length.
- **Classic:** Freeverb-style. 8 whole-sample damped combs and 4 allpasses per side, with a 23-sample stereo
  spread. The feedback comes from RT60 per comb, capped at 0.985. It has no shimmer: with comb feedback that high,
  an octave loop builds up energy.
- **Shimmer:** a two-head, 60 ms octave-up pitch shifter on the tail, fed back through a soft limiter.
- **Freeze:** no loss, no damping, no new input and no early reflections.
- **Shared stages:**
  - A 12 dB low cut before the pre-delay, so the early reflections are filtered too.
  - Colour tilt (±6 dB around 900 Hz), M/S width, and ducking (a 5 ms / 250 ms envelope of the dry signal).
  - Mix law: dry at full level up to 50%, wet at full level from 50%.
  - A per-algorithm level trim, measured so every mode sits at −6 dB wet on noise.
  - A safety reset if the tank ever exceeds ±16.
- **Algorithm switching:** the wet fades out over 20 ms, the tank is cleared, and the wet fades back in.

## Test results (test/host_test.cpp)
- **RT60** (Schroeder T20 × 3) for Decay 2 s / 6 s:

  | Algorithm | 2 s | 6 s |
  |---|---|---|
  | Room | 2.00 | 6.00 |
  | Hall | 2.00 | 5.99 |
  | Plate | 2.06 | 5.77 |
  | Classic | 1.68 | 4.95 |
  | Ice | 1.99 | 6.01 |
  | Meta | 2.00 | 6.01 |

  Hall is also 1.4–2.7 s at 48 and 96 kHz.
- **Mix 0** is bit-exact dry.
- **Pre-delay:** the first reverb arrives at the set time.
- **Damping 2.5 kHz:** the tail at 6 kHz is at least 12 dB lower (relative to 500 Hz) than with damping open.
- **Low cut 800 Hz** removes 60 Hz from the reverb by more than 20 dB.
- **Shimmer** adds more than 10 dB an octave up.
- **Freeze** holds the tail within 3 dB for 5 s and ignores new input.
- **Ducking 100%** gives more than 10 dB of dip.
- **Width:** 0 is mono, and the tail correlation at 100% is under 0.5.
- **Stability:** every algorithm at maximum size, decay, modulation, shimmer and diffusion stays bounded and decays.
  All 34 presets are stable.
- **Algorithm switch:** no step larger than the tail's own.
- **Denormals:** none in a 20 s tail.
- **CPU:** 0.6–1.5% of one x86 core per instance.
