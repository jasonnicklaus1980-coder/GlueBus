# Operating the emulated MPC3000 (Python prototype)

    python3 drive.py "Disk" "Main Screen" "Song"

Boots OS 3.10 from the v3.10 ROM pair, presses SOFT KEY 4 to cancel the SCSI search, then
presses each named key and prints the 40x8 LCD after it. Key names are in KEYMAP.txt
(e.g. "Soft Key 1", "Enter", "Play", "Stop", "Up Arrow", "Program/Sounds").

Needs: /tmp/mpc3000_v310_x86.bin (interleaved v3.10 ROM pair) and the panel ROM
mp3000__op_v1_0_am27c256__id0110_ic602.bin in /mnt/user-data/uploads.
