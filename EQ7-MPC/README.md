# EQ7 (MPC)

7-band parametric EQ for the MPC X (and the other Gen1 MPC OS devices) as a **native VST2 insert effect**, built from
the GlueBus / SP1200 / ASR10 components: dependency-free VST2 core, RBJ biquads, parameter/preset model, installer,
packaging and skin pipeline.

- **7 bands:** power button, Type (Peak, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass), Freq 20 Hz-20 kHz,
  Gain +-18 dB, Q 0.1-18, Slope 12 / 24 / 48 dB/oct for the cuts; Output +-18 dB; zero latency; glides without clicks
- **Graph:** EQ curve (64 points, exact response) over a live analyzer (32 bands), laid out like MPC's own visual EQs;
  see `docs/skin-preview.png`
- **42 presets** for drums, bass, vocals, instruments, samples / effects and buses (list in `mpc/INSTALL.md`)
- Q-Links: two tabs (Gain / Freq and Q / Type), Output and Bypass on 15-16
- Links only libc + libm. Installs to `/sdcard/vst/eq7.so`, skin to `/sdcard/Synths/GlueBus - VST - EQ7/`

## Build + install
    make test                         # native x86 build + offline VST2 host test
    ./scripts/package.sh              # ARM build, ELF checks, dist/EQ7-1.0.0-mpc-armv7.zip
    python3 tools/make_skin.py docs/skin-preview.png   # regenerate the skin (needs Pillow)
Device install (Terminus): `mpc/INSTALL.md`. Design and test notes: `docs/DESIGN.md`.
