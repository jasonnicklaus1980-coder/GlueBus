# ASR10 (MPC)

Ensoniq ASR-10 sampler character as a **native MPC OS VST2 insert effect** for the MPC X (and the other Gen1
devices), built from the GlueBus / SP1200 components: the same dependency-free VST2 core, parameter/preset model,
installer, packaging and slider skin pipeline (tested on an MPC X with SP1200).

- **Signal path:** input > 16-bit sigma-delta ADC at **30 kHz** (29.76, band-limited ~13.4 kHz, the dark ASR sound)
  or **44.1 kHz** > OTTO playback with **linear interpolation** (tuning = read rate) > OTTO **4-pole filter** (Filter 1 /
  Filter 2, the chip's four modes, 6-24 dB/oct, no resonance) > 16-bit DAC > mix / volume. Details and sources: `docs/CIRCUIT.md`
- **Tuning:** -12 .. +12 semitones plus Fine -50 .. +50 cents; *Pitch* mode (interpolated pitch shift, within a few
  cents in the tests) or *Rate* mode (tune changes the sampling rate, pitch stays)
- **Sliders, no pads:** seven faders (Input, Tune, Fine, Filter 1, Filter 2, Mix, Volume) on Q-Links 1-7; Sample Rate,
  Filter Mode, Tune Mode and Bypass on 8-11. See `docs/skin-preview.png`
- 12 presets: Init 44.1k, 30k Dark, 30k Drums Punch, Dusty Loop, Pitch Down -3, Rate -5 Crunch, Warm Keys, Thin Break,
  Up +5 Chop, Sub Filter, Hot Input, Parallel 30k
- Links only libc + libm. Installs to `/sdcard/vst/asr10.so`, skin to `/sdcard/Synths/GlueBus - VST - ASR10/`

## Build + install
    make test                         # native x86 build + offline VST2 host test
    ./scripts/package.sh              # ARM build, ELF checks, dist/ASR10-1.0.0-mpc-armv7.zip
    python3 tools/make_skin.py docs/skin-preview.png   # regenerate the skin (needs Pillow)
Device install (Terminus): `mpc/INSTALL.md`.

## Not covered / unverified
- Not yet run on an MPC. The skin reuses SP1200's layout rules, which work on an MPC X (square fader frames, skin
  folder `<manufacturer> - VST - <plugin>`), but these switch groups and the 11-entry Q-Link map are new.
- Not compared against recordings of a real ASR-10; the ESP effects chip is not modelled.
