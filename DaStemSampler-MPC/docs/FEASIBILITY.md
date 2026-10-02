# Da Stem Sampler: AI stem separation on the original MPC X (findings, before building the plugin UI)

## Platform (from the GlueBus plugins already running on MPC X: docs/DESIGN.md of Da Clip Pads)
- Native plugins: VST2 `.so`, 32-bit ARM hard-float, glibc <= 2.34, registered in `MPC.settings` (`pluginList-arm`),
  instruments allowed. Pads arrive as MIDI notes with velocity; no pad-LED or bank API.
- UI: TUI.json skins only (images, filmstrips, buttons, labels bound to parameters). No free drawing: a waveform is
  built from filmstrip bars.
- Hardware: 4x Cortex-A17 (RK3288, NEON), 2 GB RAM shared with the running MPC app.

## Inference engine
- ONNX Runtime: no 32-bit ARM Linux build on PyPI (only aarch64); not usable without a large source build.
- **demucs.cpp** (MIT, C++17 + Eigen, Demucs v3 / v4 / v4 6-stem / v4 fine-tuned): cross-compiled for armv7 + NEON.
  `stemworker` (engine/stemworker.cpp) links it: libc/libm only, glibc 2.34, runs under ARM emulation.
- BS-Roformer / Mel-Band Roformer / MDX-Net: no C++ engine for 32-bit ARM, and the Roformer models are several
  times larger than Demucs. Not supported.

## Measured (x86 build, untrained weights with the exact htdemucs / hdemucs_mmi layer shapes)
| Model | Parameters | Peak memory | Time for 10 s of audio (1 x86 core) |
|---|---|---|---|
| htdemucs (v4, 4 stems) | 41.5 M | 1829 MB | 85 s |
| hdemucs_mmi (v3) | 83.6 M | 1890 MB | 51 s |
Memory does not depend on song length (one 7.8 s segment at a time). Most of it is temporary tensors inside the
frequency-decoder convolutions (up to 94 MB each, several alive at once).

## What this means on the MPC X (estimates until measured with `stemworker bench`)
- Memory: ~1.8-1.9 GB does not fit next to the running MPC app in 2 GB. As is, separation can only run while the
  MPC app is closed, or after reducing the decoder's temporary memory (work on demucs.cpp, result not guaranteed).
- Time: a Cortex-A17 core is several times slower than the x86 core above: roughly 1-3 hours per 4-minute song for
  Studio quality, about 4x that for Maximum.
