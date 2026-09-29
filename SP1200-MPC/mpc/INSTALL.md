# SP1200 1.0.2

SP-1200 character (12-bit, 26.04 kHz, drop-sample tuning, SSM2044 / fixed output filters) as a native MPC OS
VST2 insert effect, with an eight-slider screen. Loaded by MPC's built-in plugin host.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live / Live II, One, Key 61, Force).
- **Root shell access** (SSH). Installing plugins this way is unofficial: back up first, use at your own risk.

## Install (scripted)
1. From your computer: `scp -r SP1200-1.0.2 root@<device-ip>:/tmp/`  (or use `scripts/deploy.sh <device-ip>`)
2. Run it: `ssh root@<device-ip> sh /tmp/SP1200-1.0.2/install.sh`

The installer checks the device, copies `sp1200.so` to `/sdcard/vst/` and the skin to
`/sdcard/Synths/GlueBus - VST - SP1200/`, **stops MPC** (save your project first),
backs up `MPC.settings`, adds the plugin to MPC's plugin list and starts MPC again. Re-running upgrades in place.
Add `-y` to skip the confirmation prompt. It can sit next to GlueBus; the two don't share any files.

Then add **SP1200** as an insert (on a drum program, pad, track or bus) from the plugin browser.

## Controls (one screen page, "Sliders")
| Q-Link | Slider | Range |
|---|---|---|
| 1 | Input | -24 .. +12 dB into the 12-bit ADC (clips hard at full scale, like the hardware) |
| 2 | Tune | -12 .. +7 semitones, one step per semitone |
| 3 | Decay | 20 ms .. 4 s to -60 dB after each hit; top = Off |
| 4 | Output | which SP-1200 output the sound leaves from: 1-2 Dyn, 3-4, 5-6, 7-8 |
| 5 | Dyn Sweep | how fast the Out 1-2 SSM2044 filter closes after a hit (1 .. 250 ms) |
| 6 | Dyn Floor | where the Out 1-2 filter settles (100 Hz .. 2 kHz; the RT5/RT6 trim) |
| 7 | Mix | dry / SP |
| 8 | Volume | -24 .. +12 dB output |
| 9 | Tune Mode | 45>33 Grit (tune changes the sample rate, pitch stays) / Pitch (drop-sample pitch shift) |
| 10 | Bypass | |

The 13 factory presets are the plugin's programs.

## Uninstall
`ssh root@<device-ip> sh /tmp/SP1200-1.0.2/uninstall.sh` removes the plugin, its skin and the plugin-list entry.

## Install by hand
1. Copy `payload/vst/sp1200.so` to `/sdcard/vst/sp1200.so` and
   `payload/Synths/GlueBus - VST - SP1200/` to `/sdcard/Synths/GlueBus - VST - SP1200/`.
2. `systemctl stop acvs`
3. Back up `MPC.settings` (`/media/az01-internal/Settings/*/MPC.settings`).
4. Inside `<VALUE name="pluginList-arm"><KNOWNPLUGINS>` add the line from `plugin.xml`
   (create the `pluginList-arm` value just before `</PROPERTIES>` if it doesn't exist).
5. `systemctl start acvs`. If MPC shows default settings, restore your backup (the XML was malformed).

## Troubleshooting
- Plugin doesn't appear: it is not in the list until MPC restarts; check `grep sp1200 /media/az01-internal/Settings/*/MPC.settings`.
- MPC crashes on load: run `uninstall.sh`, or restore the `MPC.settings.bak-sp1200-*` backup and delete `/sdcard/vst/sp1200.so`.
- Skin doesn't show (plain parameter list instead): MPC looks for the skin at `/sdcard/Synths/<manufacturer> - VST - <plugin>`,
  here `GlueBus - VST - SP1200` (`ls /sdcard/Synths` to check). Also check `/sdcard/Synths` is in MPC's SynthContentLocations; a skin-only
  change needs no restart, just remove and re-insert the plugin.
