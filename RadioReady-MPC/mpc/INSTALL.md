# RadioReady EQ 1.0.2

A professional 8-band EQ with a QUICK RADIO READY section, live analyzer, EQ curve, meters and 55 presets, built as a
native MPC OS VST2 insert effect and loaded by MPC's built-in plugin host. In the plugin browser it's
**RadioReady EQ** by **RadioReady Audio**. The skin's gold title reads **Marcus And Moni Radio Ready EQ**.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live / Live II, One, Key 61, Force).
- **Root shell access** (SSH). Installing plugins this way is unofficial, so back up first and use it at your own risk.

## Install with Terminus
1. Upload `RadioReady-1.0.2-mpc-armv7.zip` to `/tmp` on the MPC (SFTP).
2. Save your MPC project, then run:
   `cd /tmp && unzip -o RadioReady-1.0.2-mpc-armv7.zip && sh RadioReady-1.0.2/install.sh`
3. Answer `y`. MPC restarts. Add **RadioReady EQ** as an insert on a track, program, bus or the master.

What the installer does:
- copies `radioready.so` to `/sdcard/vst/`
- copies the skin to `/sdcard/Synths/RadioReady Audio - VST - RadioReady EQ/`
- **stops MPC**, backs up `MPC.settings`, adds the plugin to MPC's plugin list and starts MPC again

Re-running it upgrades in place, and `-y` skips the prompt. It installs alongside the Marcus Price Jr Radio Ready EQ,
EQ7, ASR10, SP1200 and GlueBus plugins; none of them share files.

## Screens (tabs)
**All tabs:**
- The gold title bar reads Marcus And Moni Radio Ready EQ.
- The graph shows the EQ curve (±18 dB, including the Quick section) over the live analyzer.
- **Touch strips on the graph:** it is split into 8 strips, B1-B8, one per band. Drag up or down inside a strip to raise
  or lower that band's gain; its value shows at the top of the strip. Double-tap a strip for the value dial. The strips
  are tied to the band number, not to where the band sits on the curve, so move frequencies with the FREQ boxes or
  Q-Links 9-16.
- On the right are the IN and OUT peak meters and **LEVEL COMP**, the automatic level compensation in use.

**EQ GAIN/FREQ and EQ Q/TYPE** (same screen, different Q-Links). Each of the 8 band panels has:
- **power** button
- **FREQ** (20 Hz–20 kHz)
- **GAIN** (±18 dB)
- **Q** (0.1–10)
- **TYPE**: Bell, Low Shelf, High Shelf, High Pass, Low Pass, Notch, Band Pass
- **SLOPE**: 12, 24 or 48 dB/oct, for the pass filters
- **CHAN**: Stereo, Left or Right, or in Mid/Side mode, Mid or Side

To change a box, drag it up or down, turn its Q-Link, or double-tap it for the value overlay.

**RADIO READY:**
- **Presets:** ◀ / ▶ step through the presets, limited to the chosen **CATEGORY** (All, Hip-Hop, Vocals, Drums,
  Master Bus, Instruments, Translation, Favorites).
  - Turning the preset box picks any preset directly.
  - ★ marks the current preset as a favourite.
  - **A / B** compares two settings; **COPY** copies the current side to the other.
  - **UNDO** and **REDO** step through edits and preset loads; **RESET** reloads the current preset.
- **QUICK RADIO READY:**
  - **AMOUNT**: 0% means no effect at all.
  - **LOW-END CONTROL**: sub rumble filter, low shelf weight, tighter boom.
  - **VOCAL CLARITY**: less mud, more presence.
  - **AIR & PRESENCE**: top shelf with a sibilance guard.
  - **PUNCH**: low-mid body plus upper-mid attack.
  - Its combined boost is capped at +4 dB and it's level-matched, so turning it up changes the tone, not the loudness.
- **Levels & modes:**
  - **INPUT** and **OUTPUT** gain (±24 dB).
  - **STEREO / MID/SIDE**.
  - **ZERO LATENCY / HQ 2x**: 2x oversampling, 31 samples of latency, about twice the CPU.
  - **AUTO GAIN**: level-matches your EQ so you judge the tone, not the volume.
  - **VIEW**: which channel the curve shows.
  - **ANALYZER**: Off, Out or In.
  - **BYPASS**.

**Your own presets:** use MPC's plugin preset save/load (the preset icons at the top of the plugin screen). Every
setting is a plugin parameter, so the whole state is saved.

## Q-Links
| Tab | Q-Links 1-8 | Q-Links 9-16 |
|---|---|---|
| EQ GAIN/FREQ | band 1-8 gain | band 1-8 frequency |
| EQ Q/TYPE | band 1-8 Q | band 1-8 type |
| RADIO READY | Amount, Low-End, Vocal, Air, Punch, Input, Output, Preset | Category, Mode, Quality, Auto Gain, Analyzer, A/B, View, Bypass |

## Presets (55 + Init)
- **Hip-Hop:** Modern Radio Hip-Hop, Classic Boom Bap, West Coast G-Funk, East Coast Punch, Trap Radio Ready,
  Dark Hip-Hop, Clean Hip-Hop Mix, Vintage Sample Warmth, Heavy 808 Mix, Commercial Rap
- **Vocals:** Radio Lead Vocal, Warm Male Vocal, Bright Female Vocal, Smooth Rap Vocal, Aggressive Rap Vocal,
  Vocal Presence, Vocal Air, Intimate Vocal, Vocal Clarity, Polished Vocal
- **Drums:** Punchy Kick, Deep 808, Snare Crack, Classic MPC Drums, Tight Drum Bus, Modern Trap Drums, Boom Bap Drums,
  Drum Bus Glue, Bright Hi-Hats, Full Drum Mix
- **Master Bus:** Radio Master, Commercial Loudness Prep, Clean Master Bus, Warm Master Bus, Modern Hip-Hop Master,
  Smooth High End, Tight Low End, Wide Mix Tonality, Analog-Inspired Master, Final Mix Polish
- **Instruments:** Warm Piano, Bright Piano, Vintage Sample, Synth Presence, Electric Bass, Acoustic Guitar,
  String Clarity, Dark Sample Restoration, Bright Sample Restoration, Full Instrument Mix
- **Translation:** Car Speakers, Consumer Headphones, Studio Monitors, Small Speakers, Streaming

Every preset is gain-staged, landing within ±0.2 dB of the input level on pink noise, so switching presets compares
tone rather than loudness.

Presets are starting points; every voice, beat and room is different. EQ is one step of a radio-ready record:
arrangement, balance, compression and mastering still matter, and nothing replaces checking on several systems.

## Uninstall
Unzip the package to `/tmp` again first, then run `sh /tmp/RadioReady-1.0.2/uninstall.sh`. Favourites are kept in
`/sdcard/vst/radioready.favorites`; delete that file to clear them.

## Troubleshooting
- **Not in the plugin list:** it appears after MPC restarts. Check with
  `grep radioready.so /media/az01-internal/Settings/*/MPC.settings`.
- **MPC crashes on load:** run `uninstall.sh`, or restore the `MPC.settings.bak-radioready-*` backup and delete
  `/sdcard/vst/radioready.so`.
- **Plain parameter list instead of the skin:** `ls /sdcard/Synths` must show `RadioReady Audio - VST - RadioReady EQ`.
  Remove and re-insert the plugin after a skin change.
- **Screen sluggish while music plays:** set the analyzer to Off (the curve still works).
- **CPU:** HQ 2x roughly doubles the load; use Zero Latency on tracks and HQ on the master if needed.
