# Da Clip Pads (MPC plugin)

A clip launcher and sample-performance instrument for Akai MPC standalone (Gen1 MPC OS, 32-bit ARM). It has:
- 16 clips on a 4×4 pad grid and 4 scenes
- quantised launching locked to the MPC's tempo, and follow actions
- pitch in Resample or Stretch mode, and Stretch To bars
- CHOP with transient detection, FLIP with locks and undo
- an optional vintage SAMPLER stage, per-clip filter / drive / sends, and master FX
- 54 factory presets, kits, and full project recall

User guide: [mpc/INSTALL.md](mpc/INSTALL.md) · MIDI: [docs/MIDI.md](docs/MIDI.md) ·
Design, API findings, limitations and performance: [docs/DESIGN.md](docs/DESIGN.md) ·
Screens: `docs/skin-preview-*.png`

## Build
Requirements:
- Linux with `gcc` / `g++` and `make`
- `g++-arm-linux-gnueabihf` for the MPC build (or Docker)
- Python 3 with Pillow for the skin
- `zip` for packaging

There are no other libraries: the VST2 ABI is in `src/vst2.h`, and the plugin needs only libc, libm and libpthread.

```
make test                 # native build + demo project + offline host test (99 checks)
make arm                  # build/arm/daclippads.so for the MPC
python3 tools/make_skin.py docs    # regenerate the skin (+ previews); needs `make native` first
python3 tools/gen_presets.py       # regenerate the factory presets
./scripts/package.sh      # ARM build + checks (ARM/32-bit/hard-float/glibc <= 2.34) + dist/DaClipPads-1.0.0-mpc-armv7.zip
./scripts/deploy.sh <mpc-ip>       # copy + install over SSH
```
On Ubuntu 22.04 the ARM toolchain keeps the plugin within GLIBC_2.34, like the other MPC packages. Newer
toolchains work too, because the code avoids the symbols that would need a newer glibc (`fmod`, `atoi`/`strtol`).

## Install on the MPC
Upload the zip to `/tmp`, then run:
`cd /tmp && unzip -o DaClipPads-1.0.0-mpc-armv7.zip && sh DaClipPads-1.0.0/install.sh`.
Then make a plugin track with **Da Clip Pads**. See [mpc/INSTALL.md](mpc/INSTALL.md).

## Test project
`make demo` writes `test/demo/Clips`:
- 10 synthesised clips: a 90 BPM break, bass, 48 kHz keys, a 96 BPM soul chop, vocal, stab, kick, snare, hat and a
  riser
- `Kits/Demo Kit.dcpkit`, which puts them on pads 1-10 with 4 scenes

The installer copies both to `/sdcard/Clips`.

## Status
- **Tested offline:** all 99 checks pass on the real plugin binary, through the VST2 interface.
- **Not yet tested on an MPC:** loading as an instrument on a plugin track, pad MIDI, host tempo / transport,
  project recall through the chunk, and the skin. See the limitations in docs/DESIGN.md.
