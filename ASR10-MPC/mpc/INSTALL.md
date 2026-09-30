# ASR10 1.0.0

Ensoniq ASR-10 sampler character (16-bit sigma-delta converters at 30 kHz or 44.1 kHz, OTTO interpolated playback,
4-pole digital filters) as a native MPC OS VST2 insert effect, with a seven-slider screen. Loaded by MPC's built-in
plugin host.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live / Live II, One, Key 61, Force).
- **Root shell access** (SSH). Installing plugins this way is unofficial: back up first, use at your own risk.

## Install with Terminus
1. Upload `ASR10-1.0.0-mpc-armv7.zip` to `/tmp` on the MPC (SFTP).
2. Save your MPC project, then run: `cd /tmp && unzip -o ASR10-1.0.0-mpc-armv7.zip && sh ASR10-1.0.0/install.sh`
3. Answer `y`. MPC restarts; add **ASR10** as an insert (drum program, pad, track or bus).

The installer checks the device, copies `asr10.so` to `/sdcard/vst/` and the skin to
`/sdcard/Synths/GlueBus - VST - ASR10/`, **stops MPC**, backs up `MPC.settings`, adds the plugin to MPC's plugin
list and starts MPC again. Re-running upgrades in place; `-y` skips the prompt. It sits next to GlueBus and SP1200;
they share no files.

## Controls (one screen page, "Sliders")
| Q-Link | Control | Range |
|---|---|---|
| 1 | Input | -24 .. +12 dB into the 16-bit ADC (clips hard at full scale) |
| 2 | Tune | -12 .. +12 semitones |
| 3 | Fine | -50 .. +50 cents |
| 4 | Filter 1 | low-pass cutoff (poles 1-2, and pole 3 in the LP3 modes), 100 Hz .. Open |
| 5 | Filter 2 | high-pass (Off .. 20 kHz) in the HP modes, low-pass (20 Hz .. Open) in the LP modes |
| 6 | Mix | dry / ASR |
| 7 | Volume | -24 .. +12 dB output |
| 8 | Sample Rate | 30 kHz (29.76 kHz, band-limited ~13.4 kHz: the dark ASR sound) / 44.1 kHz |
| 9 | Filter Mode | LP2/HP2, LP3/HP1, LP2/LP2, LP3/LP1 (the OTTO chip's four modes; 6-24 dB/oct, no resonance) |
| 10 | Tune Mode | Pitch (interpolated pitch shift) / Rate (tune changes the sampling rate, pitch stays) |
| 11 | Bypass | |

Note: in the LP modes Filter 2 is a low-pass, so at its lowest setting (20 Hz) it removes almost everything.
Push it up (to Open) when you switch to LP2/LP2 or LP3/LP1.

The 12 factory presets are the plugin's programs.

## Uninstall
`sh /tmp/ASR10-1.0.0/uninstall.sh` (unzip the package to `/tmp` again first) removes the plugin, its skin and the
plugin-list entry.

## Troubleshooting
- Plugin doesn't appear: it is not in the list until MPC restarts; check `grep asr10 /media/az01-internal/Settings/*/MPC.settings`.
- MPC crashes on load: run `uninstall.sh`, or restore the `MPC.settings.bak-asr10-*` backup and delete `/sdcard/vst/asr10.so`.
- Skin doesn't show (plain parameter list instead): MPC looks for `/sdcard/Synths/<manufacturer> - VST - <plugin>`,
  here `GlueBus - VST - ASR10` (`ls /sdcard/Synths` to check); remove and re-insert the plugin after a skin change.
