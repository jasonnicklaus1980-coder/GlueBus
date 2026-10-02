# VST3 target

Built from the JUCE project in `../CMakeLists.txt` (`FORMATS VST3`), against the VST3 SDK that ships with JUCE.
Source: `../Desktop/` (processor and editor), `../GUI/` (look and meters), `../DSP`, `../Parameters` and
`../Presets` (shared core).

| Platform | Status |
|---|---|
| Linux x86_64 | **Built and tested in this session**: `G-Glue Bus Compressor.vst3` (bundle, `Contents/x86_64-linux/*.so`), 21 host checks pass |
| Windows x64 | CMake target ready; built by the CI workflow `gglue.yml`, which has not run yet |
| macOS (universal) | CMake target ready (VST3 + AU); built by `gglue.yml`, which has not run yet |

Identity: "G-Glue Bus Compressor" by "G-Glue Audio", categories `Fx|Dynamics`, stereo or mono in and out,
latency 0. It has 11 parameters plus the "Program" parameter JUCE adds for the 65 factory presets. Parameter IDs are
derived from the stable string IDs (`threshold`, `makeup`, ...) with JUCE's version hint 1, so automation and saved
projects stay valid across versions.
