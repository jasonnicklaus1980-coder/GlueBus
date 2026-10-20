# Quanta on MPC OS (unofficial VST2 build)

**Unofficial.** Akai publishes no third-party plugin SDK for MPC Standalone. MPC OS loads Linux VST2 plugins that are
registered in `MPC.settings`, which needs root SSH access to the device. Back up first; use at your own risk. This is the
same route as the other plugins in this repository.

**Not yet verified on a real MPC.** The ARM library is built and checked (dependencies, glibc level, exports), and the
installer and uninstaller have been run against a fake MPC root. Nothing has run on an MPC X yet.

## Requirements
- A first-generation MPC OS device: MPC X, Live, Live II, One, Key 61 or Force. The installer refuses other devices.
- Root SSH access to the device (Termius works: connect as `root` on port 22).

## Install
1. Download `Quanta-1.0.0-mpc-armv7.zip` and unzip it on your computer. Copy the whole `Quanta-1.0.0-mpc-armv7` folder to
   the device's `/tmp` with SFTP.
2. Save your MPC project. The installer stops MPC and starts it again.
3. In the terminal, as root:
   `sh /tmp/Quanta-1.0.0-mpc-armv7/install.sh`
   Answer `y`. The installer checks the file checksums, copies `quanta.so` to `/sdcard/vst`, copies the screen skin to
   `/sdcard/Synths/Quanta Audio - VST - Quanta`, backs up `MPC.settings`, and registers the plugin.
4. Add an insert effect to a track, program, submix or the master, and choose **Quanta** from the effects list.

## Use
- **PARTICLE** (large, left): overall intensity. It adds a little to Density, Glue, Low End and Air, and trims the
  processed signal by up to 1 dB. It is not a loudness control.
- **TONE**: darker at the bottom, brighter at the top. **DENSITY**: saturation with a little even-harmonic colour.
  **GLUE**: linked compression with makeup gain. **WIDTH**: 0 mono, middle unchanged, wider above. **LOW END**: bass
  weight and less low-mid buildup. **AIR**: high-frequency openness. **MIX**: dry to processed. **OUTPUT**: trim.
- **MONO BASS**: on makes everything below about 120 Hz mono. Useful for club playback and mono-compatible masters.
- **Meters** (bottom left): input level, output level, glue reduction and stereo correlation. They are read-only.

## Uninstall
`sh /tmp/Quanta-1.0.0-mpc-armv7/uninstall.sh` removes the plugin, its skin and its registration, and restarts MPC.
A backup of `MPC.settings` is kept next to it.

## Troubleshooting
- **Quanta doesn't appear in the effects list:** check that `/sdcard/vst/quanta.so` exists, then restart MPC.
- **The skin doesn't show:** check that `/sdcard/Synths` is one of MPC's content locations. The installer warns if not.
- **MPC starts with default settings:** restore the `MPC.settings.bak-quanta-…` file the installer made.
- **Installer says the files are damaged:** copy the folder to the device again.

## Limits
- Twelve factory presets, plus Custom (which changes nothing). Choosing a preset sets the ten sound controls, including Mono Bass on three of them. Turning the PRESET box or choosing a program loads it.
- Bypass and A/B are session switches: they are not saved with the project. A/B holds two settings while you work; after a restart both slots start from the defaults.
- Bypass fades over about 10 ms. It does not save CPU: the engine keeps running so it is ready when bypass ends.
- No oversampling: keep Density moderate on bright material to avoid aliasing.
- The peak control is a sample-peak soft ceiling at about -0.3 dBFS, not a true-peak limiter.
