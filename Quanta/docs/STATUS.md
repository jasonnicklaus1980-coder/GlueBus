# Quanta: status against the brief

Decisions in force (from the project owner):
- The MPC skin is the only interface. No JUCE or OpenGL GUI is used, because neither is verified on the MPC X.
- The desktop controller-mode build is dropped.
- Parameter names stay as they are (Intensity, Tone, Density, Glue, Width, Low End, Air, Mix, Output, Mono Bass).
- The advanced page and the clipping indicator are on hold.

## Done and verified on the host
| Item | Status |
|---|---|
| DSP engine: tone, saturation, glue, width, mono bass, DC blocker, mix, output | built; 667 engine checks pass |
| Parameter smoothing, denormal guard, no allocation in the audio path | built and tested |
| Sample rates 44.1 to 192 kHz; silence, overdriven, DC and denormal input | tested (robustness tests) |
| Bypass (10 ms crossfade) and A/B compare (session only) | built; 46 wrapper checks pass |
| Undo and redo (32 steps, quick edits join one step, session only) | built and tested; header buttons on the skin |
| Factory presets (12) and Custom | built; preset selection tested |
| State save and restore | built; preset number kept, values not re-applied |
| Read-only meters: in, out, glue reduction, correlation | built (real measurements), shown in dB |
| RMS readings (input and output, 300 ms) | built; engine test checks a -20 dBFS tone reads about -23 dB |
| Momentary loudness (LUFS, 400 ms) | built as an approximation (K-weighting not calibrated); engine test checks it against the RMS-based value within 1.5 dB |
| ARM library: only libc and the loader; glibc 2.25 max | checked |
| Installer, uninstaller, fake-root install test | built and tested |
| Quanta GitHub Actions workflow | built; runs on each push |

## Not verified
- Nothing has run on an MPC X: no install, no audio, no screen check.
- Listening tests and a comparison with other plugins have not been done.

## Not done (from the brief)
- Custom JUCE or OpenGL interface: not used. Not verified on the MPC.
- Desktop VST3 / AU / controller-mode build: dropped.
- Controls named INPUT, PUNCH, SATURATION and STEREO: not added. The current names are kept.
- Advanced page (EQ frequencies, compressor timing, limiter settings): on hold.
- Clipping and peak indicator: on hold. Peak readings have no reset.
- Adaptive limiter: built as a basic limiter with drive and a 1 ms attack, no lookahead. Not calibrated.
- Multiband dynamics: built as three bands with compression, not calibrated against reference material.
- Mid-range EQ and input target marker: built (-3 to +3 dB at 1 kHz; -18 dB marker).
- Sidechain input: not built. It needs a second input bus, which the MPC host may not support. Adding one could stop the plugin loading, so it is left out until the host is verified.
- Gated integrated loudness (full BS.1770): not built. Only momentary loudness is shown.
- Oversampling: not built, so Density should stay moderate on bright material. Adding it changes latency and CPU, which needs your decision first.
- User presets: four slots saved as text files in /sdcard/Quanta. Not verified on the MPC. Search and favourites: not built.
- Tests while audio plays, under automation, and while presets load: not done on the device.

## Limits to keep in mind
- The output ceiling is a soft limit at about -0.3 dBFS on sample peaks, not a true-peak limiter.
- The engine has no oversampling and no lookahead, so its latency is zero by design. No host has measured it.
