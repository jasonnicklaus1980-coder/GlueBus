# SP1200 2.0.0

A circuit-informed SP-1200 hardware model (12-bit SAR converter, 26.04 kHz drop-sample playback, 8-bit level DAC,
zero-order hold, SSM2044 / fixed output filters, staged noise) as a native MPC OS VST2 insert effect, with a four-page skin. Loaded by MPC's built-in plugin host.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live / Live II, One, Key 61, Force).
- **Root shell access** (SSH). Installing plugins this way is unofficial: back up first, use at your own risk.

## Install (scripted)
1. From your computer: `scp -r SP1200-2.0.0 root@<device-ip>:/tmp/`  (or use `scripts/deploy.sh <device-ip>`)
2. Run it: `ssh root@<device-ip> sh /tmp/SP1200-2.0.0/install.sh`

The installer checks the device, copies `sp1200.so` to `/sdcard/vst/` and the skin to
`/sdcard/Synths/GlueBus - VST - SP1200/`, **stops MPC** (save your project first),
backs up `MPC.settings`, adds the plugin to MPC's plugin list and starts MPC again. Re-running upgrades in place.
Add `-y` to skip the confirmation prompt. It can sit next to GlueBus; the two don't share any files.

Then add **SP1200** as an insert (on a drum program, pad, track or bus) from the plugin browser.

## Controls
Four pages (tabs). Q-Links 1-16 follow the page you are on.

**SP-1200** (Q-Links 1-10 = the sliders)
| Q-Link | Control | |
|---|---|---|
| 1 | Input | -24 .. +24 dB into the input stage and converter (clips like the hardware) |
| 2 | Drive | 0 .. +24 dB into the output op-amp stage (level compensated) |
| 3 | Pitch | semitones; HW range -8 .. +7, Ext -12 .. +12 (CIRCUIT page) |
| 4 | Sample Rate | 4 .. 48 kHz; 26.04 kHz = hardware |
| 5 | Bits | 4 .. 16; 12 = hardware |
| 6 | SSM Char | level into the SSM2044 (Out 1-2): more = warmer / more saturated |
| 7 | Analog | nonlinearity of the analog stages: input amp, SSM2044, output amp (0 = linear, 100 % = modelled, 200 % = exaggerated) |
| 8 | Hiss | output-stage hiss (HW = calibrated default) |
| 9 | Output | -24 .. +12 dB |
| 10 | Mix | dry / SP (dry is latency-matched) |
| 11-16 | Output Channel, Tune Mode, Decay, Dyn Sweep, Dyn Floor, Machine | |

Output Channel: Out 1-2 (SSM2044 dynamic filter), 3-4 (~7.5 kHz), 5-6 (~10 kHz), 7-8 (unfiltered).
Tune Mode: 45>33 Grit (pitch kept, sample rate changes), Pitch (live drop-sample shift), Replay (each hit replays at
the tuned rate, the hardware's behaviour; best on drums). Quality: Eco / Normal / Accurate (default) / Reference.

**CIRCUIT**: per-stage controls and stage bypasses (Analog / Digital / SSM / Amp / Filters), quantizer, alias,
reconstruction, level DAC, S/H settling. **NOISE**: every noise source, component variation and unit.
**ANALYZER**: input and output spectrum, scope with a selectable tap, alias meter.

The 54 factory presets are the plugin's programs. `docs/CIRCUIT.md` explains each stage.

**Upgrading from 1.x:** the plugin ID is unchanged, so projects reopen with 2.0, but the parameter set is new: check
SP1200 settings in older projects. CPU is higher than 1.x; with several instances use Quality Eco or Normal.

## Uninstall
`ssh root@<device-ip> sh /tmp/SP1200-2.0.0/uninstall.sh` removes the plugin, its skin and the plugin-list entry.

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
