# Da DJ Decks 1.0.0

A two-deck DJ player for MPC. It loads WAV tracks from the SD card and gives you tempo sync, beat loops, a 3-band kill
EQ and filter per deck, a crossfader, spinning platters and beat lights. It's a native MPC OS VST2 insert effect. In
the plugin browser it's **Da DJ Decks** by **RadioReady Audio**, and the screen's gold title reads **Da DJ Decks**.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live / Live II, One, Key 61, Force).
- **Root shell access** (SSH). Installing plugins this way is unofficial, so back up first and use it at your own risk.

## Install with Terminus
1. Upload `DaDJDecks-1.0.0-mpc-armv7.zip` to `/tmp` on the MPC (SFTP).
2. Save your MPC project, then run:
   `cd /tmp && unzip -o DaDJDecks-1.0.0-mpc-armv7.zip && sh DaDJDecks-1.0.0/install.sh`
3. Answer `y`. MPC restarts. The installer also creates the folder **/sdcard/DJ**.
4. Copy your tracks as **WAV** files into `/sdcard/DJ`. Sub-folders work one level deep, for example `/sdcard/DJ/House/`.
5. Add **Da DJ Decks** as an insert on an audio track or the master, and press **RESCAN**. The line under deck A shows
   how many tracks it found.

The installer copies `dadjdecks.so` to `/sdcard/vst/` and the skin to
`/sdcard/Synths/RadioReady Audio - VST - Da DJ Decks/`. It then **stops MPC**, backs up `MPC.settings`, adds the
plugin to the plugin list and starts MPC again.

## Tracks
- **WAV only:** 16/24/32-bit or float, mono or stereo, any sample rate, up to 20 minutes. MP3 isn't supported, so
  convert MP3s to WAV first.
- **Memory:** each loaded track takes about 10 MB per minute (a 4-minute track is about 40 MB).
- **BPM and beat grid** are detected when a track loads, and work best on music with a steady beat. The display shows
  the BPM including your pitch.

## Using it
**Decks (A left, B right):**
- **Track box:** drag it, or turn Q-Link 12 (A) or 15 (B), to pick a track. Then press **LOAD**. The track loads
  stopped at its first beat.
- **▶ ❚❚** plays and pauses.
- **CUE:**
  - When stopped, it sets the cue point where the track is.
  - When playing, it jumps back to the cue point and stops.
- **PITCH fader:** changes speed, and key with it, like a turntable. **RANGE** sets it to ±8, ±16 or ±50%.
- **SYNC** matches this deck's tempo to the other deck, including double or half time if that's closer. It also
  lines up the beats: straight away if this deck is playing, otherwise the moment you press play.
- **NUDGE ◀ / ▶** moves the track 20 ms per press, to fix the beat alignment by ear.
- **LOOP:** 1, 2, 4, 8 or 16 beats, snapped to the beat grid. Set it to OFF to play on.
- **Display:** BPM, pitch %, elapsed and remaining time, a progress bar, a spinning platter, and 4 beat lights
  (the gold one is beat 1 of the bar).

**Mixer:**
- **GAIN** is the trim.
- **HIGH / MID / LOW** go from −24 dB (fully left) to +6 dB. Fully left **kills** the band: the crossover points are
  300 Hz and 4 kHz.
- **FILTER:** turn left for low-pass, right for high-pass. The centre is off.
- **Channel fader** and **meter** for each deck.
- **Crossfader**, with a **SMOOTH** or **CUT** curve.
- **MASTER** sets the decks' level; fully left is off.
- **MPC IN** is the level of whatever MPC audio feeds this insert (your sequence, pads, etc.). It passes through
  untouched at 100%, so you can mix decks over your own beats.

**Q-Links:**
| Q-Link | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| | A Pitch | A Volume | A Filter | A Low | B Pitch | B Volume | B Filter | B Low |

| Q-Link | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|
| | Crossfader | A Mid | A High | A Track | B Mid | B High | B Track | Master |

**Saving:** saving the MPC project (or a plugin preset) remembers every setting, and which track each deck had
loaded. That track is loaded again when you reopen the project, as long as `/sdcard/DJ` still has the same files.

## Things it can't do
- There's no headphone cue or pre-listen, because the plugin only has the one stereo output that MPC gives an insert.
- There's no keylock: pitch changes the key, like vinyl.
- There's no scrolling waveform and no MP3.

## Uninstall
`sh /tmp/DaDJDecks-1.0.0/uninstall.sh` (unzip the package to `/tmp` again first). Your tracks in `/sdcard/DJ` are left
alone.

## Troubleshooting
- **"No WAV files in /sdcard/DJ":** copy WAVs there (not MP3), then press RESCAN.
- **"Can't load: ...":** the file isn't a plain WAV (for example a compressed or unusual format). Export it again as
  16- or 24-bit WAV.
- **Wrong BPM** (for example half or double): SYNC still works, because it picks half or double time automatically.
  For tracks without a clear beat, set the pitch by ear.
- **Not in the plugin list:** it appears after MPC restarts. Check with
  `grep dadjdecks /media/az01-internal/Settings/*/MPC.settings`.
- **MPC crashes on load:** run `uninstall.sh`, or restore the `MPC.settings.bak-dadjdecks-*` backup and delete
  `/sdcard/vst/dadjdecks.so`.
