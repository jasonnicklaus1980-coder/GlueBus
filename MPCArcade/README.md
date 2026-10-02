# MPC Arcade: steps 1 and 2

Goal: classic arcade games running full-screen on the MPC X, with the MPC app stopped while you play and **always**
started again afterwards. Nothing in this package installs into the system or runs at boot: a power cycle always
starts the normal MPC.

## What's in the folder
| File | What it does | Changes the system? |
|---|---|---|
| `survey.sh` | collects CPU, memory, OS, storage, display, audio, input and service facts into one report | **No** (writes only the report file) |
| `arcade.sh` | launcher: stops the MPC app, runs a full-screen program, starts the MPC app again | **Temporarily**: stops/starts the `acvs` service |
| `hwtest` | 20-second hardware test: colour bars, a test tone, logs touches / buttons / pads / knobs | No (draws on the screen, plays sound) |

## Step 1: survey (read-only)
    mkdir -p /sdcard/mpcarcade && cd /sdcard/mpcarcade      # (copy the files here first)
    sh survey.sh
Send back `/sdcard/mpcx-survey.txt`.

## Step 2: hardware test (save your MPC project first)
    cd /sdcard/mpcarcade
    chmod +x hwtest
    sh arcade.sh run --max 60 ./hwtest
What happens:
1. The MPC app stops. The screen shows colour bars with an **orange square in the top-left corner** and a yellow
   square sliding across.
2. A one-second **beep** plays.
3. For 15 seconds, **touch the screen, press buttons, hit pads, turn the knobs and the jog wheel**. Touches draw white dots.
4. The screen goes black and the MPC app starts again by itself (about 30 s).

Then send back `/sdcard/mpcarcade/hwtest.log` and tell me: did the bars appear the right way up, did you hear the
beep, and did the dots land under your finger?

## If anything goes wrong
- The MPC app doesn't come back: `sh /sdcard/mpcarcade/arcade.sh restore` (or `systemctl start acvs`).
- No SSH and a black screen: wait 2 minutes (a watchdog starts the MPC app), or simply **power-cycle** the MPC.
  Nothing here runs at boot, so it always starts normally.
- Remove everything: `rm -r /sdcard/mpcarcade /sdcard/mpcx-survey.txt`.

## Next steps (after your results)
3. Pick and cross-compile the emulator for this exact display / audio / input setup (likely AdvanceMAME or a
   MAME 2003-era build for these classic games on a 32-bit ARM board).
4. Test a game ROM you own; map the pads, buttons and touchscreen as arcade controls; an exit combination that
   always returns to the MPC app.
5. A launcher menu. Only after all that is tested by hand: optional ways to start it (still never by default).
