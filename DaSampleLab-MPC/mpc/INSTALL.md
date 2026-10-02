# Da Sample Lab 1.1.0

A slicing sampler for the MPC, in the spirit of Serato Sample: load a WAV, see its BPM and key, chop it into slices,
play the slices from the pads, edit and rearrange them, pitch or time-stretch them, and export them as WAVs.
It is a native MPC instrument plugin: it loads on a PLUGIN track and saves with your project.
A second plugin, **Da Sample Lab Capture**, is an insert effect that records audio inside MPC for Da Sample Lab.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM): **MPC X** (not the X SE), Live / Live II, One, Key 61, Force.
- **Root shell access** (SSH). Installing plugins this way is unofficial: back up first, use at your own risk.

## Install
1. Copy the zip to the device (e.g. `scp DaSampleLab-1.1.0-mpc-armv7.zip root@<mpc-ip>:/tmp/`), then on the MPC:
   `cd /tmp && unzip -o DaSampleLab-1.1.0-mpc-armv7.zip && sh DaSampleLab-1.1.0/install.sh`
2. The installer:
   - copies `dasamplelab.so` and `dasamplelab_capture.so` to `/sdcard/vst/` and their screen skins to `/sdcard/Synths/`
   - creates `/sdcard/SampleLab/Samples` (with a demo loop), `Samples/Captures` and `/sdcard/SampleLab/Exports`
   - **stops MPC** (save your project first), backs up `MPC.settings`, registers Da Sample Lab as an instrument and
     Da Sample Lab Capture as an effect, and
     starts MPC again. Re-running upgrades in place.
3. In MPC: make a **PLUGIN track** and pick **Da Sample Lab** (RadioReady Audio) as its instrument.

## Use
**SAMPLE page**
- **BROWSE** picks where the list comes from:
  - *SampleLab*: `/sdcard/SampleLab/Samples` (including your Capture takes)
  - *Recent recordings*: the newest WAVs on MPC's internal drive (samples you recorded or resampled in MPC), newest first
  - *Internal drive*: every WAV on the internal drive; *USB drive*: every WAV on a USB stick
- Turn **SAMPLE** to choose a WAV, then tap **LOAD**. Internal-drive files are used where they are. A file loaded from
  USB is copied to `/sdcard/SampleLab/Samples` first, so your project still opens when the drive is unplugged.
- **LATEST** loads the newest Capture take straight away (see *Sampling inside MPC* below).
- Loading analyses the sample (**BPM** and **key**, shown next to the waveform) and chops it by transients into 16 slices.
- **START / END** set the part of the sample that is chopped and auditioned. You can also **drag on the waveform**:
  left / right on its top half moves START, on its bottom half moves END. **PLAY** auditions it (tap again to stop).
- **ZOOM**: *Whole sample*, *Start to end*, or *Selected slice* (the waveform shows just that slice, 128 bars wide).
- **PITCH** (semitones) and **FINE** (cents) tune everything. **TIME MODE**:
  - *Repitch*: speed and pitch change together, like a turntable.
  - *Stretch*: the speed changes and the pitch stays (high-quality WSOLA time-stretch).
- **SYNC** = *MPC tempo* plays the slices at the project tempo: speed = MPC tempo / sample BPM. If the detected BPM is
  wrong, tap **BPM ÷ 2** or **BPM × 2**, or set **BPM** by hand (for a sample without drums).
- **TARGET KEY** shifts everything to a key: the detected key moves by the shortest step (-5 to +6 semitones); a minor
  sample shifted to a major key goes to its relative minor (and vice versa), so it fits the key you chose. The shift
  is shown next to the key (`A minor → +3`). *Off* = no shift.
- **LEVEL** is the overall level.

**CHOP page**
- **CHOP BY**: *Transients* (hits, with **SENSITIVITY**), *Beats*, *Bars* (on the beat grid, from the first downbeat),
  *Sections* (musical changes, on bar lines), or *Equal*. **SLICES**: 4 to 64. Tap **CHOP**.
- **FIND** gives another chop of the same kind: other transients, or the beat / bar grid shifted by a step. Tap it
  again for more; **CHOP** goes back to the first chop. ZOOM works here too.
- **SLICE** selects a slice (it turns red on the waveform; gold lines mark where slices start).
  For the selected slice: **SLICE START / END** (or drag on the waveform: top half = start, bottom half = end),
  **SLICE LEVEL**, **SLICE PITCH**, **FILTER** (turn left: low-pass 20 kHz → 200 Hz, right: high-pass 20 Hz → 5 kHz,
  centre = off), **SLICE ATTACK / RELEASE** (*Global* = the PADS page values), **REVERSE**, **LOOP**, **MUTE**,
  **CHOKE GROUP** (slices in the same group cut each other, like an open / closed hi-hat), **▶ SLICE** to hear it.
- **◀ MOVE / MOVE ▶** rearrange: the slice swaps places with its neighbour, so it moves to another pad.
- **SPLIT** cuts the slice at its strongest hit (or the middle); **MERGE** joins it with the next slice.
- **EXPORT** writes the selected slice, **EXPORT ALL** every slice, to `/sdcard/SampleLab/Exports/<sample>/`.
  Slices are cut from the original file: same sample rate and bit depth (24-bit stays 24-bit), nothing re-encoded.

**PADS page**
- **PAD MODE** *Slices*: slice 1 on the first pad note (C1 = 36 by default, **FIRST PAD NOTE**), slice 2 on the next
  note, and so on. Pad banks A-D reach slices 1-16, 17-32, 33-48, 49-64. If your pads play other notes, set the first
  note to match pad A1, or use MPC's pad-to-note settings for the track.
- **PAD MODE** *Chromatic*: the selected slice on every pad and key, C3 (60) = original pitch. Play it from the pads
  in 16 Levels / Chromatic mode or a MIDI keyboard.
- **TRIGGER** *One-shot* (slices play to their end) or *Gate* (they stop when you let go; looping slices loop until then).
- **VOICES** *Poly* or *Mono (choke)*: a new hit cuts the previous one.
- **VELOCITY** sets how much pad velocity changes the level; **ATTACK** / **RELEASE** shape every hit.
- Note Repeat works: it re-triggers the pad's slice.

Everything (sample, slices, settings) is saved with the MPC project and restored when you load it.

**Sampling inside MPC (Da Sample Lab Capture)**
- Insert **Da Sample Lab Capture** as an effect on any track, a submix or the master (it passes audio through untouched).
- Tap **● REC / ■ STOP** to start and stop. Or turn on **ARM: START ON SIGNAL**, tap REC, and recording starts when
  the input goes above **THRESHOLD** (with a short pre-roll so the attack isn't cut). **MAX LENGTH** stops it
  automatically (5 s to 10 min).
- Takes are 24-bit WAVs at the project rate in `/sdcard/SampleLab/Samples/Captures/Capture <date time>.wav`.
- In Da Sample Lab, tap **LATEST** (the status line also tells you when a new take is ready).

## Uninstall
`sh DaSampleLab-1.1.0/uninstall.sh` removes both plugins, their skins and their plugin-list entries (after a backup of
`MPC.settings`). `/sdcard/SampleLab` (your samples and exports) is kept: delete it yourself if you want.

## Recovery
- MPC doesn't start after installing: the installer kept a backup next to MPC.settings
  (`MPC.settings.bak-dasamplelab-<date>`). Over SSH: `systemctl stop acvs`, copy the backup over `MPC.settings`,
  `systemctl start acvs`.
- The plugins never change MPC's software or start-up; removing `/sdcard/vst/dasamplelab*.so` and their plugin-list
  entries (the uninstaller) returns MPC to exactly how it was.

## Limits
- MPC lets plugins draw only through skins (no free drawing): the waveform is 128 bars built from the audio, with
  zoom. Dragging on the waveform moves a marker *relative* to where it is (like turning a knob), not to the exact spot
  you touch. If dragging doesn't respond on your MPC, the START / END and SLICE START / END controls do the same.
- Plugins can't light the pads or read pad banks directly: banks work because each bank sends its own notes.
- BPM and key are estimates, like in any sampler. Samples without a clear beat get an arbitrary BPM: set it by hand.
- Longest sample: 10 minutes (about 100 MB of memory at 44.1 kHz).
