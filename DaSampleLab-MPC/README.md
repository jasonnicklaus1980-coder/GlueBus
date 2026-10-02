# Da Sample Lab (MPC)

A Serato Sample-style slicing sampler as a **native MPC OS VST2 instrument** for the original MPC X and other Gen1
devices, built on the same VST2 core, installer and skin pipeline as the other GlueBus / RadioReady plugins.

- **Analysis:** BPM (onset autocorrelation, refined to 0.01 BPM, downbeat from kick energy), key (8192-point chroma,
  Krumhansl-Kessler profiles), transients (spectral flux, refined to the attack in the waveform), musical sections.
- **Chop** by transients (sensitivity), beats, bars, sections or equal parts: 4-64 slices on 4 pad banks.
- **Slices:** start / end, level, pitch, reverse, loop, audition, move (rearrange pads), split, merge.
- **Play:** pads with velocity, one-shot or gate, poly or choke, attack / release; chromatic mode (selected slice,
  C3 = original pitch); Repitch or WSOLA Stretch (pitch kept); sync to the MPC tempo.
- **Export** slices from the original file at its own rate and bit depth (bit-identical to the source).
- **Files** on internal storage: `/sdcard/SampleLab/Samples` (USB samples are copied in) and `/sdcard/SampleLab/Exports`.
- Full state saved in the project (VST2 chunk). Three skin pages: SAMPLE, CHOP, PADS (`docs/skin-preview-*.png`).

User guide: [mpc/INSTALL.md](mpc/INSTALL.md).

## Build + test
    make test         # native build + test/host_test: 41 checks (analysis, chopping, pads, editing, export, state, pitch, stretch)
    make test-arm     # the same 41 checks on the ARM build, under QEMU
    make skin         # skin + previews (needs Pillow; renders the real plugin with the demo loop)
    make package      # ARM build + ELF checks + dist/DaSampleLab-1.0.0-mpc-armv7.zip
Requirements: `g++`, `g++-arm-linux-gnueabihf`, Python 3 with Pillow, `zip`, `qemu-arm-static` for `test-arm`.

## How it fits MPC (from the GlueBus plugins already running on MPC X)
| | |
|---|---|
| Format | VST2 `.so`, 32-bit ARM hard-float, glibc <= 2.34, registered in `MPC.settings` (`pluginList-arm`) as an instrument |
| Pads | MIDI notes with velocity through `effProcessEvents` (no API for pad lights or banks) |
| Screen | TUI.json skin: images, filmstrips, buttons and labels bound to parameters; no free drawing |
| Project | state in the VST2 chunk |
| Binary | C++ runtime linked in and hidden: exports only `VSTPluginMain`, `SL_ParamKey`, `SL_ParamCount`; needs libc / libm only |

## Measured
- 41 / 41 checks native and on the ARM build (QEMU): 96.0 BPM and A minor on a 96 BPM loop, G major on a G-C-D-G
  progression, 16 transient slices all within a few ms of the hits, bar slices on the bar lines, pad sound at the
  exact MIDI sample, -12.0 dB slice level, chromatic 440 / 659.2 / 880 Hz, stretch 2x at 440.0 Hz, sync 0.250 s,
  24-bit / 48 kHz export bit-identical to the source, state restored byte for byte.
- CPU (x86, one core): 16 voices Repitch 1.4 %, Stretch 6.8 %. A Cortex-A17 core is roughly 8-10x slower: expect
  about 10-15 % / 55-70 % of one of the MPC X's four cores in that worst case. **Not yet measured on the MPC itself.**
- Not yet tested on the device: loading in MPC's plugin browser, the skin on the MPC screen, and real pad notes.
