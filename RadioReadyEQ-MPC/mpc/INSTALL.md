# Marcus Price Jr Radio Ready EQ 1.0.0

7-band parametric EQ with a live analyzer, an EQ curve and 50 radio-ready mix presets, as a native MPC OS VST2 insert
effect. Loaded by MPC's built-in plugin host. In the plugin browser: **Marcus Price Jr Radio Ready EQ** by
**Marcus Price Jr**.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live / Live II, One, Key 61, Force).
- **Root shell access** (SSH). Installing plugins this way is unofficial: back up first, use at your own risk.

## Install with Terminus
1. Upload `RadioReadyEQ-1.0.0-mpc-armv7.zip` to `/tmp` on the MPC (SFTP).
2. Save your MPC project, then run:
   `cd /tmp && unzip -o RadioReadyEQ-1.0.0-mpc-armv7.zip && sh RadioReadyEQ-1.0.0/install.sh`
3. Answer `y`. MPC restarts; add **Marcus Price Jr Radio Ready EQ** as an insert (track, program, bus or master).

The installer copies `radioreadyeq.so` to `/sdcard/vst/` and the skin to
`/sdcard/Synths/Marcus Price Jr - VST - Marcus Price Jr Radio Ready EQ/`, **stops MPC**, backs up `MPC.settings`, adds
the plugin to MPC's plugin list and starts MPC again. Re-running upgrades in place; `-y` skips the prompt. It sits
next to EQ7, ASR10, SP1200 and GlueBus; they share no files.

## The screen
- **Graph:** EQ curve (+-18 dB) over the live **analyzer**. The **ANALYZER** switch turns the analyzer off.
- **Band panels 1-7:** power button; drag the **FREQ / GAIN / Q** boxes up or down (or use the Q-Links); **TYPE**
  (Peak, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass); **SLOPE** 12 / 24 / 48 dB/oct for the cuts.
- **Output** fader (-18 .. +18 dB) and **Bypass**. Presets: the **PRESET** box at the top of the plugin screen.

## Q-Links (two tabs, same screen)
| Tab | Q-Links 1-7 | Q-Links 8-14 | 15 | 16 |
|---|---|---|---|---|
| GAIN / FREQ | band 1-7 gain | band 1-7 frequency | Output | Bypass |
| Q / TYPE | band 1-7 Q | band 1-7 type | Output | Bypass |

## Presets: 50 radio-ready + Flat
- **Vocals (13):** Radio Lead Vocal, Rap Lead Vocal, R&B Lead Vocal, Pop Vocal Sheen, Vocal Presence, Vocal Air Lift,
  Vocal De-Mud, Vocal De-Harsh, De-Ess Soft, De-Ess Hard, Adlibs, Background Stack, Hook Doubles
- **Drums (10):** Kick Radio Punch, Kick Knock, Kick Sub Tight, 808 Radio, 808 Small Speakers, Snare Radio Crack,
  Clap Snap, Hats Crisp, Hats De-Harsh, Drum Bus Radio
- **Bass (3):** Bass Tight, Bass Clarity, Bass Guitar Radio
- **Music (8):** Beat Room For Vocal, Keys Radio, Piano Bright, Guitar Radio, Pads Clean, Strings Air, Synth Lead Cut,
  Sample Radio Clean
- **Mix bus / master (7):** Mix Bus Radio, Master Radio Ready, Master Hip Hop, Master R&B Smooth, Master Pop Bright,
  Master Club, Master Streaming
- **Problem solvers (6):** Low End Cleanup, Mud Remover, Harshness Tamer, Air Band, Rumble Filter, Hum Notch 60/120
- **Effects (3):** Radio Phone FX, Intro Low-Pass, Breakdown High-Pass

Presets are starting points: every voice, beat and room is different, so adjust by ear. "Beat Room For Vocal" dips the
beat where "Radio Lead Vocal" / "Rap Lead Vocal" lift the voice; "808 Small Speakers" adds harmonics so the 808 still
reads on phones and car doors.

## Uninstall
`sh /tmp/RadioReadyEQ-1.0.0/uninstall.sh` (unzip the package to `/tmp` again first).

## Troubleshooting
- Not in the plugin list: it appears after MPC restarts; check `grep radioreadyeq /media/az01-internal/Settings/*/MPC.settings`.
- MPC crashes on load: run `uninstall.sh`, or restore the `MPC.settings.bak-radioreadyeq-*` backup and delete
  `/sdcard/vst/radioreadyeq.so`.
- Plain parameter list instead of the skin: `ls /sdcard/Synths` must show
  `Marcus Price Jr - VST - Marcus Price Jr Radio Ready EQ`; remove and re-insert the plugin after a skin change.
- Screen sluggish while music plays: switch the **ANALYZER** off (the curve still works).
