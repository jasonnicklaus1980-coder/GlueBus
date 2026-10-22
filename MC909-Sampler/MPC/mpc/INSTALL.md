# Legacy Sampler on MPC OS (unofficial VST2 build)

**Unofficial.** Akai publishes no third-party plugin SDK for MPC Standalone. MPC OS loads Linux VST2 plugins that are
registered in `MPC.settings`, which needs root SSH access to the device. Back up first; use at your own risk.

**Not yet verified on a real MPC.** The ARM library is built and checked (dependencies, glibc level, exports) and the
installer and uninstaller have been tested against a fake MPC root. Nothing has run on an MPC X yet, and the note-to-pad
mapping is an assumed convention (see Limits).

## What it does
An instrument with 16 pads. It plays WAV and AIFF sample files that you name in a project file. It reads only documented
formats. Roland MC-909 project, sample and system files are not decoded. Export
samples from the MC-909 as WAV or AIFF first (see `COMPATIBILITY.md` in the repository).

## Install
1. Unzip `Legacy-Sampler-0.1.0-mpc-armv7.zip` on your computer. Copy the whole `Legacy-Sampler-0.1.0-mpc-armv7` folder to
   the device's `/tmp` with SFTP.
2. Save your MPC project. The installer stops MPC and starts it again.
3. In the terminal, as root:
   `sh /tmp/Legacy-Sampler-0.1.0-mpc-armv7/install.sh`
   Answer `y`. The installer checks the file checksums, copies `mc909_sampler.so` to `/sdcard/vst`, backs up
   `MPC.settings`, and registers the instrument.
4. Create the project file (below), then load **Legacy Sampler** as an instrument on a program or track.

## Project file
The plugin reads `/sdcard/LegacySampler/project.mc909` when it opens. Create the folder, put your WAV or AIFF files in it,
and write the project as plain text. Paths are absolute:

    MC909SAMPLER-PROJECT 1
    name=Demo kit
    [pad 0]
    file=/sdcard/LegacySampler/kick.wav
    vel=0 1
    window=0 1
    tune=0
    volume=1
    pan=0
    loop=0
    reverse=0
    mute=0
    choke=0
    env=0.001 1 1 0.2

Pads are numbered 0 to 15. Each `[pad N]` block defines one pad. Missing or unreadable files are skipped. The plugin has no screen to say why, so check each file on your computer with
`mc909_import_check` before copying it over.

## Uninstall
`sh /tmp/Legacy-Sampler-0.1.0-mpc-armv7/uninstall.sh` removes the plugin and its registration and restarts MPC. A backup
of `MPC.settings` is kept next to it. Your project folder is kept.

## Troubleshooting
- **Legacy Sampler doesn't appear as an instrument:** check that `/sdcard/vst/mc909_sampler.so` exists, then restart MPC.
  If it is still missing, restore the `MPC.settings.bak-mc909-…` file the installer made.
- **Silent pads:** check the project file path and that the WAV or AIFF file is one the importer reads in full
  (`mc909_import_check` says so).
- **Installer says the files are damaged:** copy the folder to the device again.

## Limits
- No custom user interface yet: the plugin has only a Volume parameter. Use the MPC's own pad and program screens.
- Sixteen voices, one-shot, loop and reverse playback, choke groups and velocity layers are in the engine. The MPC
  build exposes them through the project file only.
- Notes map to pads with an assumed convention (notes 36 to 51 to pads 0 to 15). This is not verified on the MPC X yet.
- Projects in other MPC or MC-909 formats are not read.
