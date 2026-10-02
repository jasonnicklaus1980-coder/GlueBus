# G-Glue architecture

```
                 Parameters/  (11 parameters, stable IDs, ranges, display text)
                 Presets/     (65 factory presets, .gglue user presets, favourites, search, state + A/B)
                 DSP/         (engine: no GUI, no framework)
                      |
        +-------------+------------------------------+
        |                                            |
  MPCStandalone/ GGlueMPCBridge (C ABI)        Desktop/ (JUCE AudioProcessor + editor) + GUI/
  -> armv7 static lib, tested under QEMU       -> VST3 / VST2 / AU / standalone
  -> waits for an official MPC SDK             -> MPC Desktop (Controller Mode), DAWs
```

## DSP (`DSP/GGlueEngine.*`)
Per sample, at the oversampled rate (1x / 2x / 4x; 2x by default):
1. **Input gain**, smoothed.
2. **Up-sampling**: polyphase IIR half-band filters (12 + 6 all-pass coefficients, designed at start-up). Measured
   image rejection −151 dB, flat to 20 kHz at 44.1 kHz, and zero added latency.
3. **Sidechain HPF**: 2nd-order Butterworth at 30 / 60 / 90 / 120 / 150 / 200 Hz. It filters only the detector, never
   the audio.
4. **Linked detector**: a true-peak full-wave detector per channel; the louder channel controls both (the published
   G-series behaviour). RMS and peak/RMS blend modes exist as engine options.
5. **Feedback side-chain** (default, as in the classic console bus compressor): the detector hears the signal after
   gain reduction. A slope of (R - 1) on that level gives a static R:1 ratio, and the knee softens the harder it is
   driven. The soft knee (8 / 6 / 4 dB at 2:1 / 4:1 / 10:1) moves with the ratio, so lower ratios start compressing
   lower. The attack constant is scaled by (1 + k) so measured attack times match the panel values.
   Feed-forward is available as an engine option.
6. **Ballistics** in dB, with separate attack and release:
   - attack 0.1–30 ms; release 0.1 / 0.3 / 0.6 / 1.2 s (time constants)
   - **AUTO**: a fast stage (100 ms release) plus a slow stage that follows the static curve with a 0.5 s attack and
     a 1.2 s release; the deeper of the two applies. Short peaks recover in about 0.3 s, long compression in about
     3–4 s (measured).
7. **One linked gain** for both channels (keeps the stereo image), then makeup.
8. **Analog stage** (optional, 15 ms crossfade): an original asymmetric soft-saturation curve
   (`tanh(0.35x + bias) − tanh(bias)`, normalised to unity slope). The bias grows with gain reduction, so harder
   compression adds a little more even-order colour. Only the added harmonics pass through an 8 Hz DC blocker, so the
   clean signal is untouched. ANALOG also adds the console's low noise floor (−92 dBFS RMS). Measured at −6 dBFS:
   2nd harmonic −40.7 dB, 3rd −53 dB, level change −0.04 dB, and transient peaks within 0.3 dB.
9. **Parallel mix** of dry and compressed signals. Both pass through the same oversampling, so there's no comb
   filtering.
10. **Output gain**, then **output protection**: transparent below −0.5 dBFS, then a smooth knee that never exceeds
    0 dBFS.
11. **Down-sampling**, a final ±1.0 safety clamp, and the **bypass** crossfade (15 ms linear). When bypassed, the
    chain keeps running so switching back is seamless, and the output is the input bit for bit.

Robustness:
- Flush-to-zero / denormals-are-zero during processing (SSE MXCSR, ARM FPSCR / FPCR), plus flushing of the filter
  state.
- NaN and ±inf inputs are replaced by 0; if non-finite values ever reach the internal state, it resets.
- Every continuous control has a per-sample smoother (20 ms); the switches use linear ramps.

## Threading
- Audio thread: `Engine::process` only. It never allocates, locks or touches the GUI.
- Parameters: the host / JUCE atomics are read once per block (`pushParametersToEngine`).
- Meters: the engine publishes atomics (input / output peak with a 24 dB/s fall, and GR). The GUI timer (60 Hz) reads
  them and runs the needle physics (`GUI/NeedleBallistics.h`, spring-mass, about 3 % overshoot).
- VST3 program changes arrive on the audio thread. The preset load is moved to the message thread
  (`setCurrentProgram`).
- Presets, files and state are handled on the message / control thread only.

## Parameters (`Parameters/GGlueParameters.*`)
| # | ID | Range | Default | Display |
|---|---|---|---|---|
| 1 | threshold | −30 … +10 dB | −10 dB | `-12.0 dB` |
| 2 | makeup | 0 … +24 dB | 0 dB | `+3.0 dB` |
| 3 | attack | 0.1 / 0.3 / 1 / 3 / 10 / 30 ms | 3 ms | `10 ms` |
| 4 | release | 0.1 / 0.3 / 0.6 / 1.2 s / AUTO | AUTO | `AUTO` |
| 5 | ratio | 2:1 / 4:1 / 10:1 | 4:1 | `4:1` |
| 6 | scfilter | OFF / 30 / 60 / 90 / 120 / 150 / 200 Hz | OFF | `90 Hz` |
| 7 | mix | 0 … 100 % | 100 % | `75 %` |
| 8 | input | −24 … +24 dB | 0 dB | `+0.0 dB` |
| 9 | output | −24 … +24 dB | 0 dB | `-3.0 dB` |
| 10 | analog | OFF / ON | OFF | `ON` |
| 11 | bypass | OFF / ON | OFF | `OFF` (also the host's bypass) |

Order is fixed, so Q-Links 1–4 map to Threshold, Makeup, Attack and Release. Display texts are at most 8 characters.

## Presets and state (`Presets/`)
- Preset file `.gglue` (text): `name=`, `category=`, then `id=value` per parameter (plain values). Unknown keys are
  ignored, values are clamped, missing ones take their default. Presets never store bypass.
- Library: factory presets (built in) plus a folder of user presets. It supports save, save as (refuses to overwrite
  silently), rename, delete (user presets only), import (never overwrites: "Name 2"), export, favourites
  (`favorites.txt`), search by name or category, and previous / next with wrap-around.
- State (host chunk / MPC project): preset name, modified flag, active slot, and both A/B slots with every parameter.
