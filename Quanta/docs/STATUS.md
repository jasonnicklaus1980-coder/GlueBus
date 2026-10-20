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
| Read-only meters: in, out, glue reduction, correlation | built (real measurements) |
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
- Clipping and peak indicator: on hold.
- Oversampling: not built, so Density should stay moderate on bright material. Adding it changes latency and CPU, which needs your decision first.
- User preset save, search and favourites: not built. Saving presets to files on the MPC needs your decision on where they live first.
- Tests while audio plays, under automation, and while presets load: not done on the device.

## Limits to keep in mind
- The output ceiling is a soft limit at about -0.3 dBFS on sample peaks, not a true-peak limiter.
- The engine has no oversampling and no lookahead, so its latency is zero by design. No host has measured it.
