# Da Sample Lab (MPC)

A Serato Sample-style slicing sampler as a **native MPC OS VST2 instrument** for the original MPC X and other Gen1
devices, built on the same VST2 core, installer and skin pipeline as the other GlueBus / RadioReady plugins.

- **Analysis:** BPM (onset autocorrelation, refined to 0.01 BPM, downbeat from kick energy), key (8192-point chroma,
  Krumhansl-Kessler profiles), transients (spectral flux, refined to the attack in the waveform), musical sections.
- **Chop** by transients (sensitivity), beats, bars, sections or equal parts: 4-64 slices on 4 pad banks. **FIND**
  gives another chop of the same kind (other hits / shifted grid); **CHOP** goes back to the original.
- **Slices:** start / end, level, pitch, filter (low-pass / high-pass), attack / release, reverse, loop, mute, choke
  group (1-4), audition, move (rearrange pads), split, merge.
- **Key + tempo:** target key (shortest shift to the key or its relative major / minor), BPM ÷ 2 / × 2.
- **Waveform:** 128 bars, zoom (whole sample / start-end / selected slice), drag on the waveform to move the markers
  (top half = start, bottom half = end).
- **Sampling inside MPC:** a second plugin, **Da Sample Lab Capture** (insert effect), records any track, submix or the
  master to a 24-bit WAV (manual or armed on signal, with pre-roll); **LATEST** in Da Sample Lab loads the newest take.
  **BROWSE** also lists *Recent recordings* (MPC's own samples on the internal drive, newest first), the whole internal
  drive, or USB.
- **Play:** pads with velocity, one-shot or gate, poly or choke, attack / release; chromatic mode (selected slice,
  C3 = original pitch); Repitch or WSOLA Stretch (pitch kept); sync to the MPC tempo.
- **Export** slices from the original file at its own rate and bit depth (bit-identical to the source).
- **Files** on internal storage: `/sdcard/SampleLab/Samples` (USB samples are copied in; internal-drive samples are used
  in place), `Samples/Captures` and `/sdcard/SampleLab/Exports`.
- Full state saved in the project (VST2 chunk). Skin pages SAMPLE, CHOP, PADS + the Capture page (`docs/skin-preview-*.png`).

User guide: [mpc/INSTALL.md](mpc/INSTALL.md).

## Build + test
    make test         # native build + test/host_test: 66 checks over both plugins (analysis, chopping, pads, editing,
                      # export, state, pitch, stretch, waveform zoom, key shift, FIND, filter / envelope / mute / choke,
                      # recent recordings, capture: pass-through, sample-exact recording, armed take, LATEST)
    make test-arm     # the same 66 checks on the ARM builds, under QEMU
    make skin         # both skins + previews (needs Pillow; renders the real plugins with the demo loop)
    make package      # ARM builds + ELF checks + dist/DaSampleLab-1.1.0-mpc-armv7.zip
Requirements: `g++`, `g++-arm-linux-gnueabihf`, Python 3 with Pillow, `zip`, `qemu-arm-static` for `test-arm`.

## How it fits MPC (from the GlueBus plugins already running on MPC X)
| | |
|---|---|
| Format | VST2 `.so`, 32-bit ARM hard-float, glibc <= 2.34, registered in `MPC.settings` (`pluginList-arm`): the sampler as an instrument, Capture as an effect |
| Pads | MIDI notes with velocity through `effProcessEvents` (no API for pad lights or banks) |
| Screen | TUI.json skin: images, filmstrips, buttons and labels bound to parameters; no free drawing (the waveform is 128 filmstrip bars + 2 transparent drag strips) |
| Project | state in the VST2 chunk |
| Binary | C++ runtime linked in and hidden: exports only `VSTPluginMain` + `SL_ParamKey` / `SL_ParamCount` (Capture: `CA_*`); needs libc / libm only |

## Measured
- 66 / 66 checks native and on the ARM builds (QEMU). New in 1.1: zoom shows the selected slice across 128 of 128
  bars, A minor -> C minor = +3 semitones, FIND gives other chops and CHOP restores the original, slice attack 100 ms
  (first 10 ms at 0.3 %), low-pass 200 Hz -7.5 dB on 440 Hz, choke group 0.97 -> 0.50 peak, recordings found newest
  first and loaded in place, Capture bit-transparent and recording sample-exact (44032 of 44032 frames, 24-bit).
- From 1.0: 96.0 BPM and A minor on a 96 BPM loop, G major on a G-C-D-G
  progression, 16 transient slices all within a few ms of the hits, bar slices on the bar lines, pad sound at the
  exact MIDI sample, -12.0 dB slice level, chromatic 440 / 659.2 / 880 Hz, stretch 2x at 440.0 Hz, sync 0.250 s,
  24-bit / 48 kHz export bit-identical to the source, state restored byte for byte.
- CPU (x86, one core): 16 voices Repitch 1.4 %, Stretch 6.8 %. A Cortex-A17 core is roughly 8-10x slower: expect
  about 10-15 % / 55-70 % of one of the MPC X's four cores in that worst case. **Not yet measured on the MPC itself.**
- Not yet tested on the device: loading in MPC's plugin browser, the skins on the MPC screen (including horizontal
  dragging on the waveform), real pad notes, and Capture as an insert on MPC tracks.
