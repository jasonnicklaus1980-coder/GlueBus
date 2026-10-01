# SP1200 2.0 (MPC)

A circuit-informed **SP-1200 hardware model** as a native MPC OS VST2 insert effect for the MPC X and other Gen1
devices, built on the GlueBus VST2 core, installer and skin pipeline. Same plugin ID as 1.x, so it upgrades in place.

The DSP is the SP-1200 signal path in hardware order, each stage its own module (`src/sp/`):

    AnalogInput > ADC (SAR on Quantizer12Bit's ladder) > SamplerMemory > PlaybackEngine / PitchEngine
    > DAC (12-bit x 8-bit level, S/H) > ReconstructionFilter (ZOH, output filters) / SSMModel (Out 1-2)
    > AnalogOutput, with NoiseModel, HardwareVariation, AliasingEngine and Analyzer

- 26.04 kHz clock (27.5 kHz for SP-12), real 12-bit SAR conversion, drop-sample pitch (HW -8..+7 st), ZOH images kept
  without fold-back, 8-bit level DAC decay, SSM2044 dynamic filter on Out 1-2, ~7.5 / ~10 kHz filters on Out 3-4 / 5-6.
- Noise at each stage, calibrated to the published 90 dB (A) S/N; HISS, HUM, GROUND, DIGITAL / ANALOG / CONVERTER
  NOISE, NOISE COLOR, NOISE LEVEL.
- Component variation with repeatable "units"; ECO / NORMAL / ACCURATE / REFERENCE quality.
- Two skin pages drawn like the SP-1200's front panel: **PERFORM** (Output, Tune Mode and Machine keys, display,
  Bypass, and the eight numbered sliders Input, Pitch, Decay, Drive, SSM, Hiss, Output, Mix) and **SETUP** (sample
  rate, bits, pitch range, Out 1-2 dynamic filter, analog / variation / unit, quality, noise level, reconstruction).
  Every other model parameter is in MPC's parameter list. See `docs/skin-preview-*.png`.
- 54 factory presets.

Evidence, modelled vs assumed values for every stage: **`docs/CIRCUIT.md`**. Measured behaviour: **`docs/MEASUREMENTS.md`**.
This is a model built from published specifications, parts lists and owners' measurements, not an exact reproduction,
and no third-party code, algorithms or presets were used.

## Build + install
    make test                         # stage_test (36 DSP checks, writes docs/MEASUREMENTS.md) + host_test (21 VST2 checks)
    ./scripts/package.sh              # cross-compiles for ARM, verifies the ELF, makes dist/SP1200-2.0.0-mpc-armv7.zip
    ./scripts/deploy.sh <mpc-ip>      # tar-over-ssh, runs install.sh
    python3 tools/make_skin.py docs   # regenerate the skin and previews (needs Pillow and a native build)
Device install steps: `mpc/INSTALL.md`.

## Notes
- Projects saved with 1.x reopen with 2.0, but the parameters changed: check the SP1200 settings in old projects.
- Latency is 40 samples (reported to the host). CPU on x86 roughly 1.5 to 7 % of one core depending on QUALITY; on
  the MPC X use ECO or NORMAL if you run several instances.
- Untested on the device yet: CPU load on the MPC X's ARM, and the analyzer's refresh rate there.
