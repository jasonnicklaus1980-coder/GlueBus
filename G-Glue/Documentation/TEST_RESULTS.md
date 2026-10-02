# G-Glue test results

All runs below were made in this session on Ubuntu 24.04 x86_64 (GCC 13.3, JUCE 7.0.5). Full outputs are in
`test-logs/`.

| Suite | What it runs on | Result |
|---|---|---|
| DSP + presets + state (`Tests/DspTests.cpp`) | native x86_64 | **72 / 72** |
| same suite, MPC CPU | armv7 hard-float (MPC X class) under QEMU | **71 / 71** (the CPU budget check is skipped under emulation) |
| MPC bridge, C ABI (`MPCStandalone/BridgeTest.c`) | native x86_64 | **11 / 11** |
| MPC bridge, C ABI | armv7 hard-float under QEMU | **11 / 11** |
| Built VST3 bundle loaded in a JUCE host (`Tests/PluginHostTest.cpp`) | Linux x86_64 | **21 / 21** |
| Built VST2 `.so` loaded in a JUCE host | Linux x86_64 | **21 / 21** |
| `ctest` (DSP + bridge + host test) | Linux x86_64 | **3 / 3 suites** |
| MPC build (`MPC/test/host_test.cpp`, loads `gglue.so` like MPC OS) | native x86_64 | **35 / 35** (`../MPC/docs/test-logs/`) |
| MPC build, ARM binary | armv7 hard-float under QEMU | **34 / 34** (the CPU budget check is skipped) |
| MPC package checks | ARM ELF | libc / libm only, highest glibc symbol 2.34, 3 exports |
| MPC installer / uninstaller | fake MPC root | registers once (idempotent), valid XML, uninstall keeps user presets |
| Windows / macOS builds | CI workflow `gglue.yml` | **not run**: the workflow is written, but pushing to GitHub failed in this session |
| On MPC hardware / MPC Desktop | — | **not tested**: no hardware or MPC Desktop here |

## Coverage of the requested tests
| Requested | Covered by | Measured |
|---|---|---|
| initialization | DSP "Initialization", host load | 44.1 / 48 / 88.2 / 96 kHz × 1x / 2x / 4x oversampling, latency 0 |
| silence | DSP "Silence" | exact zeros out (analog on, +24 dB makeup), GR 0 |
| mono | DSP "Mono and stereo" | 0 dBFS sine compressed to −14.7 dBFS (threshold −20 dB, 4:1) |
| stereo | DSP + host | identical channels stay identical; linked gain: L −13.56 dB, R −13.56 dB |
| threshold | DSP, host | 10 dB below: 0.00 dB GR; host: +10 dB → none, −30 dB → 17.7 dB |
| ratio | DSP static curve | 20 dB over: 10.02 / 15.03 / 18.03 dB GR at 2:1 / 4:1 / 10:1 (expected 10 / 15 / 18); soft knee 0.58 dB at the threshold |
| attack | DSP | 0.3 ms → 0.33 ms, 10 ms → 14 ms, 30 ms → 41 ms (to 63 %, includes the knee) |
| release | DSP | 0.1 / 0.6 / 1.2 s → 0.100 / 0.601 / 1.197 s |
| auto release | DSP | after a 20 ms hit: under 1 dB in 0.33 s; after 2 s of compression: 3.74 s |
| sidechain filter | DSP | 40 Hz bass: 12.5 dB GR with SC off, 0 dB with 200 Hz; the audio itself is never filtered |
| mix | DSP | 0 / 50 / 100 % → 0.00 / −5.30 / −21.20 dB; 0 % without oversampling is bit-exact |
| analog | DSP | off: harmonics at −165 / −155 dB; on at −6 dBFS: H2 −40.7 dB, H3 −53.2 dB, level −0.04 dB, DC 3e-8, transient peaks −0.29 dB |
| bypass | DSP, host | bit-exact; exact 15 ms after switching; switching while playing: largest step 0.0004 (no click) |
| preset loading | DSP, host programs, bridge | 65 presets load; "Parallel Smash" via the host program list |
| preset saving | DSP "Preset library" | save, save as (no silent overwrite), rename (keeps favourite), delete, import ("Name 2"), export, favourites persist, search, prev / next wrap |
| state restoration | DSP, host (VST3 + VST2), bridge | A/B slots, active slot, preset name, modified flag; corrupt state rejected; new instance restores all 11 parameters |
| automation | DSP, host | all continuous parameters randomised every 64 samples: largest step 0.0006 (a 1 dB jump would be 0.06); switches while playing ≤ 0.0096 |
| sample-rate changes | DSP, host | same GR (10.40 dB) at all four rates; re-prepare 44.1 → 96 kHz while running |
| extreme settings | DSP | 40 random min/max combinations with +18 dBFS spikes: finite, peak ≤ 1.0 |
| CPU | DSP | stereo 48 kHz, analog on: 0.47 / 1.05 / 1.93 % of one x86 core at 1x / 2x / 4x. Not measured on MPC hardware |
| denormals | DSP | decay to silence: no subnormal output |
| NaN / infinity | DSP | NaN, +inf, −inf in the input: output finite and processing continues |
| clicks / pops | DSP | 12 preset changes while playing: largest step 0.0028 (steady 0.0032) |
| DC offset | DSP | zero-mean input, analog on, compressing: output DC 4e-9; DC input: stable |
| oversampling / aliasing | DSP | image rejection −151 dB; 15 kHz tone driven hard: alias at 900 Hz −43 dB without, −169 dB with 2x |

## Defects found and fixed by these tests
- **AUTO release**: the slow stage tracked the fast stage, so it kept deepening during release (2.9 s recovery after a
  20 ms hit). It now follows the static curve: 0.33 s.
- **Bypass**: the exponential crossfade took about 140 ms to become exact, and resetting the filters when leaving
  bypass caused near-Nyquist ringing (a click). Now a 15 ms linear ramp, with the chain kept running during bypass.
- **VST2 heap overflow** with the FST header: JUCE's speaker-arrangement allocation assumes Steinberg's layout. Fixed
  with a build-time header patch (`VST2/README.md`).
- **VST3 program changes** arrive on the audio thread. The preset load is now moved to the message thread.
