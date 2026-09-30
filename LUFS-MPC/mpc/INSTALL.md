# RadioReady LUFS Meter 1.0.1

A loudness meter for MPC, in the style of classic hardware loudness meters. It shows Momentary, Short Term,
Integrated, Loudness Range, True Peak and elapsed time, and has presets for streaming platforms (Spotify, Apple Music,
YouTube and more). It's a native MPC OS VST2 insert effect. In the plugin browser it's **RadioReady LUFS Meter** by
**RadioReady Audio**, and the screen's gold title reads **Da Lufs Plug**. Audio passes through untouched.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live / Live II, One, Key 61, Force).
- **Root shell access** (SSH). Installing plugins this way is unofficial, so back up first and use it at your own risk.

## Install with Terminus
1. Upload `RadioReadyLUFS-1.0.1-mpc-armv7.zip` to `/tmp` on the MPC (SFTP).
2. Save your MPC project, then run:
   `cd /tmp && unzip -o RadioReadyLUFS-1.0.1-mpc-armv7.zip && sh RadioReadyLUFS-1.0.1/install.sh`
3. Answer `y`. MPC restarts. Add **RadioReady LUFS Meter** as the **last insert on the master**, after your limiter,
   so it measures what listeners will hear.

The installer copies `radioready_lufs.so` to `/sdcard/vst/` and the skin to
`/sdcard/Synths/RadioReady Audio - VST - RadioReady LUFS Meter/`. It then **stops MPC**, backs up `MPC.settings`, adds
the plugin to the plugin list and starts MPC again. It installs alongside RadioReady EQ and the other plugins.

## Reading the meter
- **Integrated** (big number): the loudness of the whole song. This is the number streaming services use. Press
  **RESET**, play the song from start to finish, then read it.
- **Short Term** (3 s) and **Momentary** (0.4 s) show the loudness right now. The small numbers are the maximums.
- **Range (LU):** how much the loudness moves between quiet and loud parts. Typical values are about 4–8 LU for
  hip-hop and pop, and more for dynamic music.
- **True Peak (dBTP):** the highest peak, including peaks that form *between* samples when the track is converted or
  encoded. The red light turns on above the platform's ceiling.
- **Time:** how long the meter has been measuring. **PAUSE** holds the measurement.
- **LED bar:** momentary loudness compared with the target. The gold arrow marks the target (0), and red means louder
  than the target.
- **History:** the last 60 seconds of short-term loudness against the target. Red is above the target, teal is below.

## Platform presets
Pick one with ◀ ▶, by turning the platform box, or with Q-Link 1. Each preset sets a target and a true-peak ceiling.
The status lines then tell you what that platform will do with your track.

| Platform | Target | Ceiling | What the platform does |
|---|---|---|---|
| Spotify | -14 LUFS | -1 dBTP | Turns loud tracks down and quiet tracks up (limited by peaks) |
| Spotify Loud | -11 | -1 | Spotify's "Loud" listening setting |
| Apple Music | -16 | -1 | Sound Check: down and up |
| YouTube | -14 | -1 | Turns loud tracks down only |
| Amazon Music | -14 | -2 | Down only |
| Tidal | -14 | -1 | Down only |
| Deezer | -15 | -1 | Down only |
| SoundCloud | -14 | -1 | No normalization; -14 is a common reference |
| TikTok / Reels | -14 | -1 | No published target; -14 is a common reference |
| Apple Podcasts | -16 | -1 | |
| Spotify Podcasts | -14 | -1 | |
| Broadcast EBU R128 | -23 | -1 | European TV / radio |
| US TV ATSC A/85 | -24 | -2 | US broadcast |
| CD / Club Master | -9 | -0.3 | A loud reference, no normalization |
| Custom | yours | yours | Set **TARGET** and **PEAK CEILING** yourself |

These are the targets the platforms publish or that are commonly cited. Platforms change their policies, so check
their current guidance for important releases.

**Gain to Target** is the change the platform will apply to your track: negative means it gets turned down. A master
louder than the target gains nothing on these platforms; it just loses punch. **Peak Headroom** is the space left
below the ceiling, and a negative value means your peaks are over it.

## Q-Links
1 Platform, 2 Target, 3 Peak Ceiling, 4 Pause.

## Uninstall
`sh /tmp/RadioReadyLUFS-1.0.1/uninstall.sh` (unzip the package to `/tmp` again first).

## Troubleshooting
- **Not in the plugin list:** it appears after MPC restarts. Check with
  `grep radioready_lufs /media/az01-internal/Settings/*/MPC.settings`.
- **MPC crashes on load:** run `uninstall.sh`, or restore the `MPC.settings.bak-radioready-lufs-*` backup and delete
  `/sdcard/vst/radioready_lufs.so`.
- **Plain parameter list instead of the meter screen:** `ls /sdcard/Synths` must show
  `RadioReady Audio - VST - RadioReady LUFS Meter`.
- **Integrated shows "-inf" or doesn't move:** make sure audio is playing through the channel the meter is on, and
  that PAUSE is off.
