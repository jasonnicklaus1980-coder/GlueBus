# G-Glue Bus Compressor 1.0.0 for MPC (standalone, unofficial)

G-Glue running **on the MPC itself, without a computer**: the same compressor engine, parameters and 65 presets as the
desktop plugin, with its own MPC-screen faceplate.

> **Unofficial.** Akai publishes no SDK for third-party plugins on MPC Standalone. This build is a Linux VST2
> plugin that MPC OS loads once it is registered in MPC.settings. The other GlueBus MPC plugins use the same route.
> - It needs root SSH access.
> - Akai does not support it, and an MPC OS update can stop it working until you reinstall (or at all).
> - Back up first and use it at your own risk.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM): **MPC X** (not the X SE), Live / Live II, One, Key 61,
  Force.
- Root shell access (SSH).

## Install
1. Copy the zip to the device (for example `scp G-Glue-MPC-1.0.0-armv7.zip root@<mpc-ip>:/tmp/`), then on the MPC:
   `cd /tmp && unzip -o G-Glue-MPC-1.0.0-armv7.zip && sh G-Glue-MPC-1.0.0-armv7/install.sh`
2. The installer:
   - copies `gglue.so` to `/sdcard/vst/`
   - copies the screen skin to `/sdcard/Synths/G-Glue Audio - VST - G-Glue Bus Compressor/`
   - creates `/sdcard/G-Glue/Presets`
   - **stops MPC** (save your project first), backs up `MPC.settings`, registers G-Glue as an **effect**, and starts
     MPC again

   Running it again upgrades in place.
3. In MPC, insert **G-Glue Bus Compressor** (G-Glue Audio) as an insert effect on a track, program, submix or the
   master.

## The screen
- **Meters**: IN and OUT levels, and the analog **gain reduction** needle (0 to 20 dB, with real needle inertia).
- **Knobs**: THRESHOLD, MAKEUP, ATTACK, RELEASE, RATIO, SC FILTER, MIX, INPUT, OUTPUT, plus the **ANALOG** switch.
  Touch a knob and turn the data wheel or a Q-Link; double-tap it for the value overlay.
- **BYPASS**: the sound is passed through untouched (after a 15 ms crossfade).

**Presets**
- Turn the **PRESET** box to browse all 65 factory presets and your own presets (favourites come first), then tap
  **LOAD**. Tapping **◀** or **▶** loads the previous or next preset straight away.
- **★** marks the selected preset as a favourite (tap again to remove it).
- **SAVE** saves the current settings as a new user preset: "User 01", "User 02" and so on, in
  `/sdcard/G-Glue/Presets`. The MPC screen has no keyboard, so to rename a preset, rename its `.gglue` file over SSH,
  or in the desktop G-Glue (the files are the same format).
- **DELETE** deletes the selected *user* preset. Tap it twice within 4 seconds. Factory presets can't be deleted.
- The line under the buttons shows the current preset, `*` if you changed something, and the slot you're hearing:
  `[A]` or `[B]`.
- **A / B** switches between two complete settings. **COPY** copies the slot you're hearing into the other one.

**Q-Links**
- 1 Threshold · 2 Makeup · 3 Attack · 4 Release · 5 Ratio · 6 SC Filter · 7 Mix · 8 Input · 9 Output
- 10 Analog · 11 Bypass · 12 Preset

Everything is saved with the MPC project: all settings, both A/B slots and the preset name. The 11 compressor
parameters can be automated with MPC's automation.

## Uninstall
`sh G-Glue-MPC-1.0.0-armv7/uninstall.sh` removes the plugin, its skin and its plugin-list entry, after backing up
`MPC.settings`. Your presets in `/sdcard/G-Glue` are kept.

## Recovery
- If MPC doesn't start after installing, the installer left a backup next to MPC.settings
  (`MPC.settings.bak-gglue-<date>`). Over SSH: `systemctl stop acvs`, copy the backup over `MPC.settings`, then
  `systemctl start acvs`.
- The plugin never changes MPC's software or start-up. Removing `/sdcard/vst/gglue.so` and its plugin-list entry
  (which is what the uninstaller does) returns MPC to exactly how it was.

## Not yet verified on a real MPC
Built and tested on the MPC's CPU architecture under emulation, and with the same packaging, glibc limits and skin
rules as the other plugins in this repository. Still to check on hardware:
- loading in MPC's plugin browser
- how the skin looks on the screen
- how smoothly the needle meter refreshes while audio plays
- CPU use on the MPC X
