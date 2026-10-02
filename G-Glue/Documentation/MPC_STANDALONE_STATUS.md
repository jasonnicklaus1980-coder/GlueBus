# MPC Standalone: compatibility status

**Status: no *official* native MPC Standalone plugin can be built, because the SDK is not available. An unofficial
MPC build (VST2 for MPC OS, root access needed) is in `../MPC/`. See below.**
G-Glue's DSP, parameters, presets and state are ready for one (built and tested for the MPC's CPU), but the
piece that makes a plugin *native* on MPC Standalone does not exist in this project.

## What is missing
To build a native G-Glue for MPC Standalone (MPC X, Live / Live II, One, Key 61, Force and later models), these
would be needed from Akai / inMusic. None of them is publicly available, and none was found in this environment:

| Needed | Why | Available? |
|---|---|---|
| Native MPC plugin SDK: headers, plugin API / ABI, lifecycle, threading rules | the interface MPC OS loads plugins through | No: not published; MPC plugins come from inMusic (AIR) and partners (e.g. Spitfire) under agreements |
| Plugin manifest / registration format | so the plugin appears in the MPC plugin browser as an audio effect | No |
| Screen / UI API for MPC OS (touch screen, Q-Link pages) | to show the G-Glue GUI on the hardware | No |
| Packaging, signing and distribution path (e.g. MPC expansion / plugin install) | to install without modifying the OS | No |
| Target toolchain / sysroot spec (glibc, CPU flags) for current MPC OS versions | binary compatibility | Partly: Gen1 devices are known to be 32-bit ARM hard-float Linux; later devices are not covered here |

Environment facts (checked in this session): Ubuntu 24.04 x86_64, GCC 13.3, Clang 18.1, CMake 3.28, JUCE 7.0.5
(with the VST3 SDK it bundles), `arm-linux-gnueabihf-g++` 13.3, QEMU user-mode emulators. No Akai / inMusic SDK of
any kind.

## What was built instead (and what it is not)
`MPCStandalone/` contains the **G-Glue MPC bridge**: a plain C interface (`GGlueMPCBridge.h`) to the shared engine,
parameter table (with Q-Link order and short display names), presets (factory + user, stored in a folder you give it),
full state with A/B, and meters. It is the boundary a native wrapper would call once an SDK exists.

- Built as `build/mpc-armv7/libgglue_mpc.a` for 32-bit ARM hard-float (armv7-a, NEON), the CPU of the original
  MPC X, with `make -C MPCStandalone arm`.
- Tested on that architecture under QEMU (`make -C MPCStandalone test`): 71 DSP checks and 11 bridge checks pass
  (see `Documentation/test-logs/*armv7-qemu.txt`).
- It is **not** a plugin. It does not appear in the MPC browser, has no GUI on the MPC and is not installable.

## The unofficial MPC build (made at your request)
At first this project left out the community route. You then asked for the MPC version, so it is built in
[`../MPC/`](../MPC/README.md):
- On first-generation devices MPC OS loads Linux VST2 `.so` files that are registered in `MPC.settings`. This needs
  root SSH access. The other plugins in this repository use the same route, and the open-source *mpc-vst-plugins*
  project documents it.
- G-Glue's build is a thin VST2 wrapper around the same shared engine, with its own MPC screen skin and an installer
  and uninstaller that back up `MPC.settings`.
- It is tested on the MPC's CPU architecture under QEMU, but **not yet on a real MPC**.
- It is **not** an official native plugin. Akai doesn't support it, and an MPC OS update can break it.

## Running G-Glue with an MPC today
- **Officially supported:** Controller Mode. The MPC is connected to a computer running MPC Desktop with the desktop
  VST3 build of G-Glue (see `INSTALL.md`).
- **On the MPC alone:** the unofficial build in `../MPC/` (root access, Gen1 devices).

## If an SDK becomes available
1. Write `MPCStandalone/<SDK>Wrapper.cpp` that implements the SDK's plugin entry points by calling the bridge:
   - `gglue_mpc_process` from the audio callback
   - `gglue_mpc_set_normalized` / `gglue_mpc_param_text` for parameters, automation and Q-Links
   - `gglue_mpc_get_state` / `gglue_mpc_set_state` for project save and load
   - the preset functions for the preset browser
2. Draw the faceplate with the SDK's UI API. The meter ballistics (`GUI/NeedleBallistics.h`) and the GR scale are
   plain C++ and can be reused.
3. Build with the SDK's toolchain flags, adjusting `MPCStandalone/Makefile` (CPU, ABI, glibc ceiling).
