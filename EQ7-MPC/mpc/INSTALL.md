# EQ7 1.0.0

7-band parametric EQ with a curve display, live analyzer and 42 presets, as a native MPC OS VST2 insert effect.
Loaded by MPC's built-in plugin host.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live / Live II, One, Key 61, Force).
- **Root shell access** (SSH). Installing plugins this way is unofficial: back up first, use at your own risk.

## Install with Terminus
1. Upload `EQ7-1.0.0-mpc-armv7.zip` to `/tmp` on the MPC (SFTP).
2. Save your MPC project, then run: `cd /tmp && unzip -o EQ7-1.0.0-mpc-armv7.zip && sh EQ7-1.0.0/install.sh`
3. Answer `y`. MPC restarts; add **EQ7** as an insert (track, program, pad, bus or master).

The installer copies `eq7.so` to `/sdcard/vst/` and the skin to `/sdcard/Synths/GlueBus - VST - EQ7/`, **stops MPC**,
backs up `MPC.settings`, adds the plugin to MPC's plugin list and starts MPC again. Re-running upgrades in place; `-y`
skips the prompt. It sits next to GlueBus, SP1200 and ASR10; they share no files.

## The screen
- **Graph:** the EQ curve (+-18 dB) over the live **analyzer** (the output spectrum). The curve updates when you
  change a band; the analyzer about 14 times a second. The **ANALYZER** switch turns the analyzer off.
- **Band panels 1-7:** power button, then drag the **FREQ / GAIN / Q** boxes up or down (or use the Q-Links),
  **TYPE** (Peak, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass) and **SLOPE** (12 / 24 / 48 dB/oct,
  for Low Cut and High Cut; at 12 dB the Q sets the resonance).
- **Output** fader (-18 .. +18 dB) and **Bypass**.

Presets: the **PRESET** box at the top of the plugin screen.

## Q-Links (two tabs, same screen)
| Tab | Q-Links 1-7 | Q-Links 8-14 | 15 | 16 |
|---|---|---|---|---|
| GAIN / FREQ | band 1-7 gain | band 1-7 frequency | Output | Bypass |
| Q / TYPE | band 1-7 Q | band 1-7 type | Output | Bypass |

## Presets (42)
- **Drums:** Kick Punch, Kick Sub, Snare Crack, Snare Fat, Hats Bright, Hats Tame, Drum Bus, Boom Bap Drums, Toms
- **Bass:** 808, 808 Clean, Bass DI, Synth Bass
- **Vocals:** Vocal, Rap Vocal, Vocal Air, De-Ess, Backing Vocals, Warm Vocal
- **Instruments:** Keys, Rhodes, Piano, Pads, Guitar, Strings, Brass
- **Samples / effects:** Sample Chop, Dusty, Telephone, Radio, Underwater, Thin Out, Bass Only, No Bass, Mids Only,
  Hum Notch 60, Mud Cut, Smile Curve
- **Buses:** Flat, Mix Bus, Master Polish, Loudness

## Uninstall
`sh /tmp/EQ7-1.0.0/uninstall.sh` (unzip the package to `/tmp` again first) removes the plugin, its skin and the
plugin-list entry.

## Troubleshooting
- Plugin doesn't appear: it is not in the list until MPC restarts; check `grep eq7 /media/az01-internal/Settings/*/MPC.settings`.
- MPC crashes on load: run `uninstall.sh`, or restore the `MPC.settings.bak-eq7-*` backup and delete `/sdcard/vst/eq7.so`.
- Skin doesn't show (plain parameter list instead): check `ls /sdcard/Synths` shows `GlueBus - VST - EQ7`; remove and
  re-insert the plugin after a skin change.
- The screen feels sluggish while music plays: switch the **ANALYZER** off (the curve still works).
