# Building G-Glue

## Requirements
- CMake 3.22+ and a C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+, Xcode 13+)
- JUCE 7: an installed JUCE package (`find_package(JUCE)`), a checkout passed as `-DGGLUE_JUCE_DIR=...`, or nothing
  (CMake then downloads JUCE 7.0.12)
- Linux: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype-dev libfontconfig1-dev
  libgl-dev`, plus `xvfb` for the editor tests
- VST2 (optional): a VST2 SDK folder containing `pluginterfaces/vst2.x/aeffect.h`
  - Steinberg's VST2 SDK. Steinberg has not licensed new VST2 developers since 2018, so use it only if you hold a
    valid VST2 licence agreement.
  - **or** the free FST header (`fst` package on Debian / Ubuntu, GPL-3.0). CMake copies it into the build folder and
    patches it there (see `VST2/README.md`).

## Desktop plugin + tests
    cmake -S G-Glue -B build -DCMAKE_BUILD_TYPE=Release                 # VST3 + standalone (+ AU on macOS)
    cmake -S G-Glue -B build -DGGLUE_VST2_SDK_DIR=/usr/include          # ... + VST2 (here: FST from the fst package)
    cmake --build build --config Release --parallel
    ctest --test-dir build -C Release --output-on-failure              # on Linux: xvfb-run -a ctest ...

Outputs (in `build/GGlue_artefacts/Release/`):
- `VST3/G-Glue Bus Compressor.vst3`
- `VST/libG-Glue Bus Compressor.so` (`.dll` on Windows, `.vst` on macOS)
- `Standalone/G-Glue Bus Compressor`
- `AU/G-Glue Bus Compressor.component` (macOS only)

Other targets:
- `gglue_export_presets Resources/FactoryPresets`: writes the factory presets as `.gglue` files
- `GGlueScreenshot Resources/GUI`: renders the GUI to PNG (needs a display; `xvfb-run -a` on a server)
- `-DGGLUE_BUILD_PLUGIN=OFF`: core, tests and MPC bridge only, without JUCE

## MPC bridge for ARM (MPC hardware CPU)
    make -C G-Glue/MPCStandalone arm      # build/mpc-armv7/libgglue_mpc.a, gglue_dsp_tests, gglue_bridge_test
    make -C G-Glue/MPCStandalone test     # runs both under qemu-arm-static (QEMU=... to point at another binary)
Needs `g++-arm-linux-gnueabihf` and `qemu-user-static`.

## Windows / macOS
GitHub Actions workflow `.github/workflows/gglue.yml` builds and tests on `windows-latest`, `macos-14` (universal
binary) and `ubuntu-24.04`, and uploads the plugins as artifacts. **This workflow was written in this session but
has not run yet**: pushing to GitHub was blocked here. Building locally works the same way:

    cmake -S G-Glue -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release
    cmake -S G-Glue -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" && cmake --build build --config Release

## Licensing notes
- JUCE 7 is dual-licensed: GPLv3 or a commercial JUCE licence. The build sets `JUCE_DISPLAY_SPLASH_SCREEN=0`, which
  is allowed for GPLv3 projects or with a paid JUCE licence. Choose before you distribute.
- The VST3 SDK bundled with JUCE 7 (3.7.x) is dual-licensed: GPLv3 or Steinberg's free VST3 licence agreement.
  From VST 3.8 (October 2025) Steinberg publishes the SDK under the MIT licence.
- FST (VST2 header) is GPL-3.0: a VST2 build made with it must be distributed under the GPL.
