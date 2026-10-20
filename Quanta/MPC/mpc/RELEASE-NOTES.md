# Quanta 1.0.0 (MPC OS, unofficial VST2 build)

**Not yet tested on an MPC X.** The library is built and checked, and the installer has been run on a test folder
structure. Keep a backup of your project and your MPC settings before you install.

## What is in this build
- **Sound controls:** Intensity, Tone, Density, Glue, Width, Low End, Air, Mix, Output and Mono Bass.
- **Presets:** 12 factory presets plus Custom. Choosing a preset sets the sound controls, including Mono Bass on three of them.
- **Bypass:** a 10 ms crossfade to the exact input, from the Bypass switch or the host.
- **A/B:** two settings to compare while you work. Not saved with the project.
- **Undo and redo:** step back and forward through changes to the sound controls and presets (up to 32 steps). A quick run of turns counts as one step. Not saved with the project.
- **Meters (read-only):** input level, output level, glue reduction and stereo correlation, shown in dB.
- **Readouts (read-only):** RMS input and RMS output over about 300 ms, and momentary loudness over about 400 ms in LUFS.
- **Screen:** a three-column layout with the stereo field, the PRESET box, Bypass and A/B switches, and segmented meters.
- **Saved state:** the preset number is kept, and restoring a project does not re-apply preset values.

## Known limits
- No oversampling, so keep Density moderate on bright material to avoid aliasing.
- Zero latency by design (no lookahead). No host has measured it.
- The output ceiling is a soft limit at about -0.3 dBFS on sample peaks, not a true-peak limiter.
- Bypass and A/B are not saved with the project. A/B starts from the defaults after a restart.
- Bypass does not save CPU, because the engine keeps running so it is ready when bypass ends.
- Loudness is an approximation: the K-weighting is not calibrated against the reference test signals, and it is momentary only (no gated integrated loudness).
- Peak readings have no reset and no clip warning yet.
- Not yet listened to on hardware or compared by ear with other plugins.

## Install and remove
See `INSTALL.md` in this folder. The short version: copy the folder to `/tmp`, run `install.sh` as root, and run
`uninstall.sh` to remove it.
