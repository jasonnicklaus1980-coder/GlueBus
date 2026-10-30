# The Remixer 0.1.0 (Phase 1: recording) - install on the MPC X

Status: **Phase 1, not yet verified on hardware.** It records and passes audio through. It has no custom screen yet:
the MPC shows its generic parameter view.

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
2. Turn **Record** on (`Record` parameter). The Status parameter shows `Recording`.
3. Turn **Record** off. Status shows `Saved`.
4. Recordings are 24-bit, stereo, at the project's sample rate, in `/media/az01-internal/TheRemixer/Recordings/rec_<unix time>.wav`.

The Peak L/R, Clips, Rec Time, and Dropouts parameters show the input level, clipping, recording length and any lost audio.
A Dropouts count above zero means the disk fell behind and some audio was lost. Tell us if you see that.

## Uninstall
```
sh /tmp/TheRemixer-0.1.0/uninstall.sh
```
This removes the plugin and keeps your recordings.
