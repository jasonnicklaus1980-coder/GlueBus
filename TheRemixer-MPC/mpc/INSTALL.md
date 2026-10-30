# The Remixer 0.1.0 (Phase 1: recording) - install on the MPC X

Status: **Phase 1, not yet verified on hardware.** It records and passes audio through. It has a simple RECORD screen
(record button, peak meters, clip count, recording time, dropouts and status). The screen has not been seen on the device yet.

## Install
1. Copy `TheRemixer-0.1.0-mpc-armv7.zip` to the MPC's `/tmp` (Termius SFTP or USB).
2. In the MPC's SSH shell, as root:
   ```
   cd /tmp && unzip -o TheRemixer-0.1.0-mpc-armv7.zip && sh TheRemixer-0.1.0/install.sh
   ```
3. Answer `y`. The MPC restarts.
4. Add "The Remixer" as an insert effect on a track.

## Recording
1. Put The Remixer on a track whose input is the physical input you want (set the track input in the MPC).
2. Press the **REC** button on the screen to start recording. Status shows `Recording`.
3. Press **REC** again to stop. Status shows `Saved`.
4. Recordings are 24-bit, stereo, at the project's sample rate, in `/media/az01-internal/TheRemixer/Recordings/rec_<unix time>.wav`.

The button works like a press, not a switch: each press toggles recording, and the plugin resets the button after
each press (the same as the Da Lufs Plug buttons).

The readouts show the input level (Peak L/R), clipping, recording length and any lost audio.
A Dropouts count above zero means the disk fell behind and some audio was lost. Tell us if you see that.

## Uninstall
```
sh /tmp/TheRemixer-0.1.0/uninstall.sh
```
This removes the plugin and keeps your recordings.
