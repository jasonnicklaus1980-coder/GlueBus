# The Remixer

A MPC X plugin for recording, analysing and remixing music. This is the Phase 1 build: direct audio recording.

- Target: Akai MPC X (Gen1 MPC OS, 32-bit ARMv7, glibc 2.34 or older).
- Plugin format: VST2 (`/sdcard/vst/theremixer.so`).
- Status: see `docs/PHASE1.md` for what is checked on the host and what still needs the MPC X.

## Build and test (no MPC needed)
```
make test        # native build + host tests: passthrough, WAV recording, analysis, CLI
make arm         # ARMv7 build (needs arm-linux-gnueabihf-gcc, or use `make docker`)
scripts/package.sh   # verified ARM build + install zip in dist/
```

## Analysis (Phase 2, host build)
```
build/native/remixer_analyze recording.wav outdir   # tempo, key, chords, notes -> outdir/chords.txt, melody.mid
```
Tests run on synthetic audio with known answers. See `docs/PHASE2.md` for the results and limits.

## Install
See `mpc/INSTALL.md`.
