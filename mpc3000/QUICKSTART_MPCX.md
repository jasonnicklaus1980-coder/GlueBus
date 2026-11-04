# MPC3000 on the MPC X - quick start (not yet tried on the hardware)

1. Build: push this folder to GitHub. The "build-mpc3k" workflow produces `mpc3k_mpcx_armhf`
   (static ARMv7) and copies the launcher scripts into the artifact.
2. Copy to the MPC X (Termius / SFTP) into `/data/mpc3k/`:
   - `mpc3k_mpcx_armhf`, `mpc3k-run.sh`, `mpc3k-make-kit.sh`
   - your v3.10 EPROM images, renamed `mpc3000_ls_ic13.bin` and `mpc3000_ms_ic14.bin` (256 KB each)
3. First run creates a blank 256 MB SCSI disk, `/data/mpc3k/hd.img`. Format it once on the MPC3000:
   DISK, 9, 3, <Next screen>, <Format now>, then <Do it> (it proposes the partitions). Exit afterwards.
   Kits: `sh /data/mpc3k/mpc3k-make-kit.sh --find` lists your MPC X kits;
   `sh /data/mpc3k/mpc3k-make-kit.sh "/media/SSD/.../Kit-Something.xpm"` adds one to SCSI partition A
   (MPC3K_PART=B for partition B, and so on). Without a formatted disk it makes a floppy image instead.
4. Run: `sh /data/mpc3k/mpc3k-run.sh` (stops the MPC app, runs the MPC3000, restarts the app on exit).
   `MPC3K_SHORT_SEARCH=1` shortens the 60 s SCSI search at power-on to 2 s.
5. Load the kit: DISK, 7, choose the .PGM (Disk: SCSI DISK PART A), soft key 1 <Load>, then <Do it>.
   Pads 1-16 = A01-A16.
6. Exit: the EXIT touch button (or Ctrl+C in Termius).

Buttons come from your MPC4000 button map (`/data/mpc4k/mpcx_buttons.map`), translated by name.
Touch buttons: SOFT 1-4, MAIN, ENTER, -, +, EXIT; tapping the LCD's bottom row presses that soft key.
The data wheel sends +/- presses. Audio: outputs 1-2. `--audio-test` plays a test tone first.

What to report back: does the screen appear, do pads make sound, any "underruns" printed on exit,
and how the speed feels (sequencer tempo against a click, for example).
