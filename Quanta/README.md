# Quanta: stereo mix enhancement (MPC OS, first build)

An original mix-enhancement plugin: Tone, Density, Glue, Width, Low End, Air, Mix and Output, with a Particle macro that
adds a capped share of Density, Glue, Low End and Air. It is not based on The God Particle and makes no claim to match it.

## Status

| Part | State |
|---|---|
| Shared DSP engine (`DSP/`) | **built and tested** (x86 and ARM compile). Tests: `make test` |
| Parameters, defaults, display text | built. Nine controls plus Mono Bass, a Preset control (0 = Custom), and Bypass and A/B switches (session only) |
| VST2 wrapper for MPC OS (`MPC/`) | **built as an ARM library and checked**: needs only libc and the loader, highest glibc symbol 2.25, exports only `VSTPluginMain`. Wrapper tests pass on x86. Read-only meters are host parameters (In, Out, Glue GR, Correlation) |
| MPC screen skin (`MPC/tools/make_skin.py`) | **generated** with the Particle knob, eight macro knobs, the Mono Bass switch, four meters and a PRESET browser in the header. Rule checks pass (72 px filmstrips). The preview is rendered from the same images, not a device screenshot |
| MPC installer (`MPC/mpc/`, `MPC/scripts/package.sh`) | **built and tested on a fake MPC root**: install, reinstall and uninstall, with checksums. Not run on a device |
| Run on an MPC X | **not done**: no device access here. The ARM library has not been run (QEMU is not installed here) |
| Factory presets | **built and tested**: twelve presets (`DSP/QuantaPresets.cpp`). Engine checks cover names, ranges and relative character; wrapper checks cover program selection and state round trip. Not auditioned on hardware |
| Bypass | **built and tested**: host bypass (VST2 `effSetBypass`) and a Bypass switch on the MPC screen. 10 ms crossfade to the exact input |
| A/B compare | **built and tested**: two session slots with a switch on the MPC screen. Not saved with the project |
| Desktop VST3 / AU (Controller Mode) | **abandoned**: not planned. No desktop build exists in this repository |
| CPU benchmark | 10 s of stereo at 48 kHz with every control on takes about 110 ms on this machine (about 1% of one core). Not measured on the MPC |

## Build and test

    make test          # engine and wrapper tests (x86)
    make arm           # ARM engine object
    make -C MPC check  # ARM plugin library: builds it and checks its dependencies
    python3 MPC/tools/make_skin.py docs   # regenerate the skin and the preview
    sh MPC/scripts/package.sh             # the installable zip (MPC/dist/)

## Design notes

- **Particle** adds at most 0.25 to Density, Glue, Low End and Air, and trims the processed signal by up to 1 dB.
  The trim is not on the dry signal, so Mix 0 is the input exactly.
- **Tone**: a tilt of up to 3 dB at 200 Hz and 5 kHz, in opposite directions.
- **Density**: saturation at up to about 12 dB of drive, normalised so quiet signals keep their level, with a little even-harmonic bias.
- **Glue**: a linked feed-forward compressor, threshold -10 to -20 dB, ratio up to 2.5:1, a 6 dB soft knee, 20 ms attack and 250 ms release, and up to 2 dB of makeup.
- **Width**: mid/side, 0 mono to 2x side. **Mono Bass** makes content below 120 Hz mono when on.
- **Air** and **Low End** are shelves, with a low-mid cut that follows Low End.
- **Output** trims -12 to +12 dB. The peak control is a soft ceiling at about -0.3 dBFS on the sample peak: it is **not** a true-peak limiter.
- Parameters are smoothed over about 10 ms. A DC blocker sits at 3 Hz.

## Known limitations

- No oversampling yet. Density at high drive can alias, so keep it moderate until oversampling is added.
- The glue detector follows the peak, not the RMS level, so it reacts quickly to transients.
- The compressor adds no look-ahead, so there is no latency to report.
- Nothing has been listened to, so the tuning is by measurement only.
- No MPC hardware has run this build.
