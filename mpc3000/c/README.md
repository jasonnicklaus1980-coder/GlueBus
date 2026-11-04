# mpc3k - MPC3000 emulator core in C

Line-for-line port of the Python model (`../x86cpu.py`), checked in lockstep.

    make            # native build
    make mpcx-arm   # static ARMv7 build for the MPC X (needs arm-linux-gnueabihf-gcc; see the workflow)
    ./mpc3k ROM.bin --screen --time 20                 # boot (full 60 s SCSI search) and print the LCD
    ./mpc3k ROM.bin --screen --steps 20000000 --short-search   # 2 s search, main screen in ~0.6 s
    ./mpc3k ROM.bin --steps 30000000 --short-search --press 50@20000000 --screen   # press Mixer

ROM.bin is the 524,288-byte interleaved v3.10 image (ls + ms EPROMs, low byte from ic13).

Options: --steps N, --time S (wall seconds), --trace N (state every N instructions, same format as
../trace_py.py), --short-search (search-time setting 1 instead of 30), --press KK@STEP (button
message 90 KK 7F, released 60,000 instructions later; KK = 40h + button index, PANEL_PROTOCOL.md),
--screen (print the 40x8 LCD at the end).

Status: lockstep-identical to the Python model for 16 M instructions (registers, flags, ICU, timer,
every 50,000 steps through the SCSI search and OS start). 30-33 M instructions/s on the build PC,
7-8x real time (real time assumes 4 instructions per 1 MHz timer count). Not yet run on the MPC X;
no display, audio or input front end for it yet.
