# Da Clip Pads 1.0.0

A clip launcher and sample-performance instrument for the MPC: 16 clips on a 4×4 grid, 4 scenes, quantised
launching locked to the MPC's tempo, pitch and time-stretch, CHOP, FLIP, an optional vintage SAMPLER stage and
master effects. It's a native MPC OS VST2 **instrument** (plugin track). In the plugin browser it's **Da Clip Pads** by
**RadioReady Audio**.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live / Live II, One, Key 61, Force).
- **Root shell access** (SSH). Installing plugins this way is unofficial, so back up first and use it at your own risk.

## Install with Terminus
1. Upload `DaClipPads-1.0.0-mpc-armv7.zip` to `/tmp` on the MPC (SFTP).
2. Save your MPC project, then run:
   `cd /tmp && unzip -o DaClipPads-1.0.0-mpc-armv7.zip && sh DaClipPads-1.0.0/install.sh`
3. Answer `y`. MPC restarts.
4. Make a **plugin track** and choose **Da Clip Pads** (RadioReady Audio) as its instrument.
5. Try the demo: **SETTINGS** page > **KIT** "1. Demo Kit" > **LOAD KIT**, then tap **SCENE 1** on the CLIPS page.

The installer copies `daclippads.so` to `/sdcard/vst/` and the skin to
`/sdcard/Synths/RadioReady Audio - VST - Da Clip Pads/`. It creates `/sdcard/Clips` with the demo clips
(`/sdcard/Clips/Demo`) and the demo kit (`/sdcard/Clips/Kits`) without overwriting anything already there. It then
**stops MPC**, backs up `MPC.settings`, adds the plugin to the plugin list and starts MPC again.

## Samples
- Put **WAV** files in **/sdcard/Clips** (sub-folders two levels deep work). 8/16/24/32-bit or float, mono or stereo,
  any sample rate, up to 6 minutes each. Files at another rate than the MPC are converted once, when they load.
- Memory: about 10 MB per stereo minute; all loaded samples together can use up to 320 MB.
- Nothing is ever written into your WAV files. START / END / fades / REVERSE / NORMALIZE / slices are all settings.

## The pages
**CLIPS** (the main page)
- **16 clip pads**, laid out like the MPC pads: clip 1 bottom-left, clip 16 top-right. Each shows the number, name,
  a waveform thumbnail, a state ring (grey = stopped, amber dashes = waiting for the beat, green ▶ = playing,
  blue ↻ = looping, orange = stopping at the next beat, red M = muted) with a progress sweep, the clip's BPM and
  length, and its mode / pitch / volume.
- **PAD MODE**: what tapping a pad does. PLAY launches it (and selects it for editing), STOP stops it on the next
  beat, SELECT only selects it, MUTE toggles its mute.
- **SCENES 1-4**: launch a set of clips together (and stop the others) on the quantise grid. To make a scene, play
  the clips you want, press **STORE**, then press the scene. Or set it per clip on EDIT (SCENE 1-4 buttons).
  The light on a scene: blue = has clips, amber = starting, green = playing. **SCENE Q** (its own quantise),
  **FOLLOW** (Next / Previous / Random / Repeat) and **EVERY** (1-16 bars) chain scenes automatically.
- **QUANTIZE** (None, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1, 2, 4 bars), **SWING** (on 1/8 and 1/16), **TEMPO**, **SYNC TO HOST**.
- **RETRIG ALL** restarts everything playing on the next quantise point; **RESTART** does it on the next bar;
  **MUTE ALL**; **FLIP** (the selected clip); **STOP ALL**.
- Q-Links 1-16 = the 16 clip volumes.

**EDIT** (the selected clip)
- **CLIP**, **SAMPLE** (drag to browse /sdcard/Clips) + **LOAD**, **CLEAR**, **RESCAN**, **▶/■** (play the clip now, no
  quantise), **ZOOM** (1-32×, centred on the marker you last moved).
- **Waveform**: drag left/right on the **top half to move START**, on the **bottom half to move END**. The strip under
  it shows the markers: white START, red END, yellow/orange LOOP, green = the looping part, purple slices, gold =
  selected slice, pink = where it's playing.
- **START, END, LOOP START, LOOP END, FADE IN, FADE OUT, LOOP XFADE, FINE TUNE** (−100…+100 cents).
- **TRIGGER**: One Shot (plays once), Loop (loops; a tap relaunches it), Gate (plays while the pad or key is held;
  on screen a tap starts and a tap stops), Toggle (tap to start, tap to stop).
- **LAUNCH Q**: Global or its own quantise. **PITCH** −12…+12 semitones. **PITCH MODE**: Resample (speed and pitch
  change together, like classic samplers) or Stretch (pitch without changing speed).
- **STRETCH TO**: Off, Auto, 1/4, 1/2, 1, 2, 4, 8 bars. The clip (its loop) is fitted to that length at the current
  tempo, so it stays in time when the tempo changes. Auto picks the length closest to the tempo. In Resample mode the
  pitch follows the speed; in Stretch mode it doesn't. **STRETCH MODE**: Drums, Instruments, Vocals or Loops
  (grain size).
- **FOLLOW** (Stop, Repeat, Next Clip, Prev Clip, Random, Random in Scene, Continue = next clip from the same spot,
  Launch Scene) **AFTER** (At End, 1-2 beats, 1-16 bars) and **LAUNCH SCENE** (for Launch Scene).
- **SCENE 1-4** (is this clip in the scene), **MIDI NOTE** + **LEARN**, **SAMPLER** (send this clip through the
  sampler stage).
- **REVERSE, NORMALIZE, TRIM** (cut silence off the ends), **FADE** (5 ms in / 30 ms out), **CROSSFADE** (25 ms loop
  crossfade), **REARRANGED** (play the slices in the FLIP order), **FLIP**, **UNDO**.

**CHOP**
- **CHOPS** 4 / 8 / 16 / 32, **DETECT** Transient (cuts on the hits; the largest gaps are split if there are fewer
  hits than slices) or Equal, then **CHOP**.
- Waveform: drag the **top half to move the selected slice**, the **bottom half to pick a slice**. **SLICE**,
  **SLICE START**, **SLICE PITCH**, **REVERSE** (slice) edit it.
- **16 slice pads** play slices 1-16 (or 17-32 with **PAD BANK**). MIDI notes from **SLICE NOTE** (default C3 = 60)
  up play them too.
- **PAD ASSIGN** copies each slice into the next empty clip (sharing the same sample in memory), as a one-shot.
- **FLIP** makes a random variation of the selected clip from: reverse, pitch ±12, start / end shift, stutter,
  repeat, half speed, double speed, filter, random chop, rearranged slices. **LOCK** keeps pitch, reverse,
  start/end, filter or slices as they are. **UNDO** takes it back (press again to redo).

**MIX**: for each clip, its name, **pan**, a **level ladder** (drag up/down), **M**ute and its state.
Q-Links 1-16 = the 16 pans.

**FX**
- **Clip FX** (selected clip): FILTER Low Pass / High Pass / Band Pass with CUTOFF and RESONANCE, DRIVE, REVERB SEND,
  DELAY SEND, SAMPLER (stage on/off for this clip).
- **SAMPLER** stage: ON/OFF, preset (Clean, 12-Bit, SP Style, MPC Style, Vinyl, Dusty, Crushed), BIT DEPTH, SAMPLE RATE,
  ANTI-ALIAS, QUANTIZE, SATURATION, NOISE, CRACKLE, OUTPUT. It models the sound of early samplers with standard DSP
  (rate reduction, bit reduction, zero-order-hold DAC, saturation); it is not a copy of any machine's circuits.
- **MASTER**: saturation, compressor (threshold, ratio, attack, release, makeup), 3-band EQ (low shelf 100 Hz,
  mid with frequency, high shelf 8 kHz), reverb (size, damping, return), tempo-synced ping-pong delay (time,
  feedback, tone, return), MASTER volume and a LIMITER that keeps the output below 0 dBFS.

**SETTINGS**: tempo, swing, quantise, scene quantise, **WHEN MPC PLAYS / STOPS** (Off / Restart / Restart+Stop),
**QUALITY** (Eco / Normal / High), MIDI channel, velocity sensitivity, slice note, selected clip note + LEARN,
**FACTORY PRESET** + LOAD (54 presets in 9 styles; they change the setup, never your samples), **KIT** + LOAD KIT,
**SAVE KIT**, RESCAN.

## Timing
- With **SYNC TO HOST** on, Da Clip Pads follows the MPC's tempo, bars and time signature. When the sequencer plays,
  launches land on the MPC's own bar lines. When it's stopped, the plugin keeps its own clock at that tempo, so pads
  still launch in time.
- Pressing a pad up to 35 ms **after** a beat still counts for that beat: the clip starts at once, already in step.
- **WHEN MPC PLAYS / STOPS**: Restart = pressing play restarts playing clips from the top in step; Restart+Stop also
  stops them when the sequencer stops.

## Saving
Saving the MPC project saves everything: which files each clip uses, all points and settings, slices, scenes,
MIDI notes and global settings. Reopening the project reloads the samples from /sdcard/Clips (keep the files where
they were). **SAVE KIT** also writes the whole setup to `/sdcard/Clips/Kits/Kit NNN.dcpkit`.

## Uninstall
`sh /tmp/DaClipPads-1.0.0/uninstall.sh` (unzip the package to `/tmp` again first). `/sdcard/Clips` is left alone.

## Troubleshooting
- **Not in the plugin list:** it appears after MPC restarts, under instruments (plugin track). Check with
  `grep daclippads /media/az01-internal/Settings/*/MPC.settings`.
- **Pads don't play clips:** check SETTINGS > MIDI CHANNEL (Omni) and that the clip's MIDI NOTE matches the pad's note
  (MPC pad bank A sends 36-51 by default). LEARN sets it from the pad.
- **"Missing file":** the project or kit points to a WAV that isn't in /sdcard/Clips any more.
- **"Can't load":** the file isn't a plain WAV. Export it again as 16- or 24-bit WAV.
- **Crackles when many clips stretch:** set QUALITY to Eco, or use Resample instead of Stretch on some clips.
- **MPC crashes on load:** run `uninstall.sh`, or restore the `MPC.settings.bak-daclippads-*` backup and delete
  `/sdcard/vst/daclippads.so`.
