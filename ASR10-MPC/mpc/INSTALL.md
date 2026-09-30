# ASR10 1.1.0

Ensoniq ASR-10 sampler character (16-bit sigma-delta converters at 30 kHz or 44.1 kHz, OTTO interpolated playback,
4-pole digital filters) as a native MPC OS VST2 insert effect, with an ASR-10 style screen: one Data Entry
slider, Edit buttons and a fluorescent display. Loaded by MPC's built-in
plugin host.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live / Live II, One, Key 61, Force).
- **Root shell access** (SSH). Installing plugins this way is unofficial: back up first, use at your own risk.

## Install with Terminus
1. Upload `ASR10-1.1.0-mpc-armv7.zip` to `/tmp` on the MPC (SFTP).
2. Save your MPC project, then run: `cd /tmp && unzip -o ASR10-1.1.0-mpc-armv7.zip && sh ASR10-1.1.0/install.sh`
3. Answer `y`. MPC restarts; add **ASR10** as an insert (drum program, pad, track or bus).

The installer checks the device, copies `asr10.so` to `/sdcard/vst/` and the skin to
`/sdcard/Synths/GlueBus - VST - ASR10/`, **stops MPC**, backs up `MPC.settings`, adds the plugin to MPC's plugin
list and starts MPC again. Re-running upgrades in place; `-y` skips the prompt. It sits next to GlueBus and SP1200;
they share no files.

## Controls (one screen page, "ASR-10")
Edit like the ASR-10: press an **Edit** button (Input, Tune, Fine, Filter 1, Filter 2, Mix, Volume), then move the
**Data Entry** slider. The display shows the selected parameter and its value ("TUNE +3", "FILTER 1 6.0 kHz") and,
on the second line, the sample rate, filter mode and tune mode. The keys on the right switch those modes.

| Q-Link | Control | Range |
|---|---|---|
| 1 | Data Entry | moves the parameter selected with the Edit buttons |
| 2 | Edit | which parameter Data Entry moves |
| 3 | Input | -24 .. +12 dB into the 16-bit ADC (clips hard at full scale) |
| 4 | Tune | -12 .. +12 semitones |
| 5 | Fine | -50 .. +50 cents |
| 6 | Filter 1 | low-pass cutoff (poles 1-2, and pole 3 in the LP3 modes), 100 Hz .. Open |
| 7 | Filter 2 | high-pass (Off .. 20 kHz) in the HP modes, low-pass (20 Hz .. Open) in the LP modes |
| 8 | Mix | dry / ASR |
| 9 | Volume | -24 .. +12 dB output |
| 10 | Sample Rate | 30 kHz (29.76 kHz, band-limited ~13.4 kHz: the dark ASR sound) / 44.1 kHz |
| 11 | Filter Mode | LP2/HP2, LP3/HP1, LP2/LP2, LP3/LP1 (the OTTO chip's four modes; 6-24 dB/oct, no resonance) |
| 12 | Tune Mode | Pitch (interpolated pitch shift) / Rate (tune changes the sampling rate, pitch stays) |
| 13 | Bypass | |

Q-Links 3-9 reach each parameter directly; when one of them moves the selected parameter, the Data Entry slider
follows.

Note: in the LP modes Filter 2 is a low-pass, so at its lowest setting (20 Hz) it removes almost everything.
Push it up (to Open) when you switch to LP2/LP2 or LP3/LP1.

## Presets (36, the plugin's programs)
- **Basics:** Init 44.1k, 30k Dark, Hot Input, Parallel 30k
- **Drums:** 30k Drums Punch, Boom Bap Drums 30k, Kick Focus, Snare Crack 30k, Thin Hats, Hot 30k Clip, Drums Down -2
- **Loops / samples:** Dusty Loop, Soul Loop 30k, Vinyl Chop 30k, Thin Break, Radio Break, Telephone
- **Pitch / rate:** Pitch Down -3, Slowed -7, Octave Down -12, Up +5 Chop, Vocal Up +7, Chipmunk +12, Rate -5 Crunch,
  Rate -12 Crush, Rate -3 Warm, Parallel Crunch
- **Keys / bass / texture:** Warm Keys, Dark Rhodes, Lo-Fi Keys 30k, Pad Warmth, Sub Filter, Muffled Bass, Sub 808,
  Detune Chorus, Dark Detune

## Uninstall
`sh /tmp/ASR10-1.1.0/uninstall.sh` (unzip the package to `/tmp` again first) removes the plugin, its skin and the
plugin-list entry.

## Troubleshooting
- Plugin doesn't appear: it is not in the list until MPC restarts; check `grep asr10 /media/az01-internal/Settings/*/MPC.settings`.
- MPC crashes on load: run `uninstall.sh`, or restore the `MPC.settings.bak-asr10-*` backup and delete `/sdcard/vst/asr10.so`.
- Skin doesn't show (plain parameter list instead): MPC looks for `/sdcard/Synths/<manufacturer> - VST - <plugin>`,
  here `GlueBus - VST - ASR10` (`ls /sdcard/Synths` to check); remove and re-insert the plugin after a skin change.
