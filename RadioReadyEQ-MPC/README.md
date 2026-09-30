# Marcus Price Jr Radio Ready EQ (MPC)

7-band parametric EQ with 50 radio-ready mix presets for the MPC X (and the other Gen1 MPC OS devices), as a **native
VST2 insert effect**. Same engine and graph as EQ7, built from the GlueBus / SP1200 / ASR10 components.

- **Title bar:** "Marcus Price Jr Radio Ready EQ" in gold cursive; see `docs/skin-preview.png`
- **7 bands:** power button, Type (Peak, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass), Freq 20 Hz-20 kHz,
  Gain +-18 dB, Q 0.1-18, Slope 12 / 24 / 48 dB/oct for the cuts; Output +-18 dB; zero latency; glides without clicks
- **Graph:** exact EQ curve (64 points) over a live 32-band spectrum analyzer, laid out like MPC's visual EQs
- **50 radio-ready presets + Flat:** vocals, drums, bass, music, mix bus / master, problem solvers, effects
  (list in `mpc/INSTALL.md`)
- Installs to `/sdcard/vst/radioreadyeq.so`, skin to `/sdcard/Synths/Marcus Price Jr - VST - Marcus Price Jr Radio Ready EQ/`

## Build + install
    make test                         # native x86 build + offline VST2 host test
    ./scripts/package.sh              # ARM build, ELF checks, dist/RadioReadyEQ-1.0.0-mpc-armv7.zip
    python3 tools/make_skin.py docs/skin-preview.png   # regenerate the skin (Pillow; Great Vibes in tools/fonts/)
Device install (Terminus): `mpc/INSTALL.md`. Design, test and font notes: `docs/DESIGN.md`.
