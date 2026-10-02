# Da Sample Lab 1.0.0

A slicing sampler for the MPC, in the spirit of Serato Sample: load a WAV, see its BPM and key, chop it into slices,
play the slices from the pads, edit and rearrange them, pitch or time-stretch them, and export them as WAVs.
It is a native MPC instrument plugin: it loads on a PLUGIN track and saves with your project.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM): **MPC X** (not the X SE), Live / Live II, One, Key 61, Force.
- **Root shell access** (SSH). Installing plugins this way is unofficial: back up first, use at your own risk.

## Install
1. Copy the zip to the device (e.g. `scp DaSampleLab-1.0.0-mpc-armv7.zip root@<mpc-ip>:/tmp/`), then on the MPC:
   `cd /tmp && unzip -o DaSampleLab-1.0.0-mpc-armv7.zip && sh DaSampleLab-1.0.0/install.sh`
2. The installer:
   - copies `dasamplelab.so` to `/sdcard/vst/` and the screen skin to `/sdcard/Synths/RadioReady Audio - VST - Da Sample Lab/`
   - creates `/sdcard/SampleLab/Samples` (with a demo loop) and `/sdcard/SampleLab/Exports`
   - **stops MPC** (save your project first), backs up `MPC.settings`, registers the plugin as an instrument, and
     starts MPC again. Re-running upgrades in place.
3. In MPC: make a **PLUGIN track** and pick **Da Sample Lab** (RadioReady Audio) as its instrument.

## Use
**SAMPLE page**
- Turn **SAMPLE** (Q-Link 1) to choose a WAV, then tap **LOAD**. The list shows `/sdcard/SampleLab/Samples` and any
  USB drive. A file loaded from USB is copied to `/sdcard/SampleLab/Samples` first, so your project still opens
  when the drive is unplugged.
- Loading analyses the sample (**BPM** and **key**, shown next to the waveform) and chops it by transients into 16 slices.
- **START / END** set the part of the sample that is chopped and auditioned. **PLAY** auditions it (tap again to stop).
- **PITCH** (semitones) and **FINE** (cents) tune everything. **TIME MODE**:
  - *Repitch*: speed and pitch change together, like a turntable.
  - *Stretch*: the speed changes and the pitch stays (high-quality WSOLA time-stretch).
- **SYNC** = *MPC tempo* plays the slices at the project tempo: speed = MPC tempo / sample BPM. If the detected BPM is
  wrong (half or double, or a sample without drums), set **BPM** by hand.
- **LEVEL** is the overall level.

**CHOP page**
- **CHOP BY**: *Transients* (hits, with **SENSITIVITY**), *Beats*, *Bars* (on the beat grid, from the first downbeat),
  *Sections* (musical changes, on bar lines), or *Equal*. **SLICES**: 4 to 64. Tap **CHOP**.
- **SLICE** selects a slice (it turns red on the waveform; gold lines mark where slices start).
  For the selected slice: **SLICE START / END**, **SLICE LEVEL**, **SLICE PITCH**, **REVERSE**, **LOOP**, **▶ SLICE** to
  hear it.
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

## Uninstall
`sh DaSampleLab-1.0.0/uninstall.sh` removes the plugin, its skin and its plugin-list entry (after a backup of
`MPC.settings`). `/sdcard/SampleLab` (your samples and exports) is kept: delete it yourself if you want.

## Recovery
- MPC doesn't start after installing: the installer kept a backup next to MPC.settings
  (`MPC.settings.bak-dasamplelab-<date>`). Over SSH: `systemctl stop acvs`, copy the backup over `MPC.settings`,
  `systemctl start acvs`.
- The plugin never changes MPC's software or start-up; removing `/sdcard/vst/dasamplelab.so` and its plugin-list
  entry (the uninstaller) returns MPC to exactly how it was.

## Limits
- MPC lets plugins draw only through skins: the waveform is 64 bars of the whole sample, and markers are moved with
  the START / END controls (no free dragging on the waveform, no zoom).
- Plugins can't light the pads or read pad banks directly: banks work because each bank sends its own notes.
- BPM and key are estimates, like in any sampler. Samples without a clear beat get an arbitrary BPM: set it by hand.
- Longest sample: 10 minutes (about 100 MB of memory at 44.1 kHz).
