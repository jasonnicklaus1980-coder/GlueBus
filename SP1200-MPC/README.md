# SP1200 (MPC)

SP-1200 character as a **native MPC OS VST2 insert effect** for the MPC X (and the other Gen1 devices), built from
the GlueBus components: the same dependency-free VST2 core, parameter/preset model, installer, packaging and skin
pipeline.

- **Circuit-based signal path:** input > 12-bit ADC (hard clip, no anti-alias filter) > drop-sample playback on the
  **26.04 kHz** DAC clock > decay > 12-bit DAC (zero-order hold) > output filter (**Out 1-2** SSM2044 dynamic VCF,
  **Out 3-4** ~7.5 kHz, **Out 5-6** ~10 kHz, **Out 7-8** unfiltered) > output amp. Details and sources: `docs/CIRCUIT.md`
- **Tuning:** -12 .. +7 semitones, equal-tempered, in two modes: *45>33 Grit* (the tune changes the sample rate and the
  pitch stays) and *Pitch* (a drop-sample pitch shift, within ~1 cent in the tests)
- **Sliders, no pads:** one screen page drawn as the SP-1200's slider bank. Eight vertical faders (Input, Tune, Decay,
  Output, Dyn Sweep, Dyn Floor, Mix, Volume) sit on Q-Links 1-8, with Tune Mode and Bypass on 9-10. See `docs/skin-preview.png`
- 13 factory presets as VST programs (Init Out 5-6, Clean Out 7-8, Dusty Out 3-4, Tom Dyn Out 1, Kick Dyn Thump,
  45 to 33 Break, Pitch Down -4, Chop Decay, Tight Hat Decay, Crunch -12, Up +7 Pitch, Hot Input Clip, Parallel Dirt)
- Links only libc + libm (highest glibc symbol 2.27). Installs to `/sdcard/vst/sp1200.so`, skin to
  `/sdcard/Synths/SP1200 - VST - SP1200/`

## Build + install
    make test                         # native x86 build + offline VST2 host test (clock, 12-bit, tuning, filters, decay)
    ./scripts/package.sh              # cross-compiles for ARM, verifies the ELF, makes dist/SP1200-1.0.0-mpc-armv7.zip
    ./scripts/deploy.sh <mpc-ip>      # tar-over-ssh, runs install.sh (stop MPC, copy, back up, register, restart)
    python3 tools/make_skin.py docs/skin-preview.png   # regenerate the skin (needs Pillow)
On GitHub, every push builds and publishes the zip as a release tagged `sp1200-build-N` (`.github/workflows/sp1200.yml`).
Device install steps: `mpc/INSTALL.md`.

## Not covered / unverified
- Not run on a real MPC yet. The skin uses the GlueBus/JV-880 TUI.json format. The sliders are vertical-drag filmstrip
  controls, which that format supports, but no one has seen them on an MPC screen yet.
- The Q-Link map assigns only Q-Links 1-10. How MPC treats the unassigned 11-16 is untested.
- The filter cutoffs and the Out 1-2 envelope come from owners' measurements of the hardware, not from a schematic
  simulation. Real units vary, and so do their trimmers.
