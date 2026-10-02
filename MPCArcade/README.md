# MPC Arcade: MAME on the MPC X

**MAME 0.242** built for the MPC's 32-bit ARM chip. It runs full-screen on the MPC's framebuffer with ALSA sound,
while the MPC app is stopped, and the launcher always starts the MPC app again. Nothing installs into the system
and nothing runs at boot: a power cycle always starts the normal MPC.

Included drivers: Pac-Man, Ms. Pac-Man, Galaga, Dig Dug, Donkey Kong, Space Invaders, Frogger, Centipede
(771 sets in total from those six driver files, e.g. Galaxian, Scramble, Amidar, Gun Fight).

## What's in the folder
| File | What it does | Changes the system? |
|---|---|---|
| `mame` | MAME 0.242 for ARMv7 hard-float. Needs only libc/libm (glibc 2.34 or newer) | no |
| `mame.sh` | starts MAME on `/dev/fb0` (ALSA sound, touchscreen as mouse), files kept in `/sdcard/mpcarcade` | no |
| `arcade.sh` | launcher: stops the MPC app → runs a program → starts the MPC app again | **temporarily** (stops/starts `acvs`) |
| `survey.sh` | read-only system report (`/sdcard/mpcx-survey.txt`) | no |
| `hwtest` | 20 s screen / sound / input test | no |
| `padbridge` | MPC pads / buttons → virtual keyboard (probe, learn, run) | no (creates a virtual input device while running) |

## Install (copies files only)
    mkdir -p /sdcard/mpcarcade/roms
    # copy everything from this folder to /sdcard/mpcarcade, then:
    chmod +x /sdcard/mpcarcade/mame /sdcard/mpcarcade/hwtest /sdcard/mpcarcade/padbridge
Put **ROM sets you legally own** in `/sdcard/mpcarcade/roms` as zip files named like the set: `pacman.zip`,
`mspacman.zip`, `galaga.zip`, `digdug.zip`, `dkong.zip`, `invaders.zip`, `frogger.zip`, `centiped.zip`. They must
match MAME 0.242's sets (newer MAME sets usually work for these old games). No ROMs are included.

## Play (save your MPC project first)
    cd /sdcard/mpcarcade
    sh arcade.sh run ./mame.sh pacman          # one game
    sh arcade.sh run ./mame.sh                 # MAME's game list: tap a game, "Exit" at the bottom quits
When MAME exits, the MPC app starts again by itself.

### Controls: the MPC pads (no keyboard needed)
`padbridge` turns pads and buttons into a virtual keyboard for MAME. Set it up once:
1. **See what the pads send** (30 s, tap pads and press buttons while it runs):
   `sh arcade.sh run --max 60 --console ./padbridge probe 30`
2. **Learn your layout:** it asks for each action in turn. Tap the pad or button you want, or wait 15 s to skip one:
   `sh arcade.sh run --max 300 --console ./padbridge learn`
   The actions are: Up, Down, Left, Right, Fire, Button 2, Coin, Start, MAME menu, menu Select, and **Exit**.
   The mapping is saved to `/sdcard/mpcarcade/pads.conf`.
3. Play: `sh arcade.sh run ./mame.sh pacman`. `mame.sh` starts the bridge automatically when `pads.conf` exists.
   - **Exit** needs its pad **held for 1.5 s**, so a stray tap never quits. MAME then closes and the MPC app returns.
   - The **touchscreen** acts as a mouse: tap entries in MAME's game list and menus.
   - The bridge needs `/dev/uinput`; the probe output says whether your MPC has it.

A USB keyboard also works: arrows, Ctrl = fire, `5` = coin, `1` = start, `Tab` = menu, `Esc` = quit.
You can always end a session from SSH: `sh /sdcard/mpcarcade/arcade.sh stop`. Sessions also end by themselves after
30 minutes (`--max SECONDS` changes that).

### If the picture is sideways or upside down
Set `SDL_FBROTATE` to `90`, `180` or `270`, e.g. `SDL_FBROTATE=90 sh arcade.sh run ./mame.sh pacman`.

## Recovery
- The MPC app doesn't come back: `sh /sdcard/mpcarcade/arcade.sh restore` (or `systemctl start acvs`).
- No SSH and a black screen: wait about 2 minutes past the session limit (a watchdog starts the MPC app), or
  **power-cycle**. Nothing here runs at boot.
- Remove everything: `rm -r /sdcard/mpcarcade /sdcard/mpcx-survey.txt`.

## Recommended first runs
1. `sh survey.sh` (read-only) and `sh arcade.sh run --max 60 ./hwtest`: confirms screen, sound and inputs.
2. `sh arcade.sh run --max 120 ./mame.sh`: the game list should appear full-screen (tap the screen to scroll and choose).
3. `padbridge probe`, then `padbridge learn` (see Controls), then a game. Send me `/sdcard/mpcarcade/*.log` if anything looks wrong.

## What's verified so far (off-device)
- The binary is ARM EABI5 hard-float and needs only libc/libm. Its highest glibc symbol is 2.34, the same level
  the MPC already runs for the plugins.
- Run under QEMU (ARM emulation), it lists all 771 sets and renders MAME's full game list through the patched
  SDL video path (the same code that writes to `/dev/fb0` on the device).
- **Not yet verified on the MPC itself:** framebuffer format and orientation, ALSA device access, speed. The first
  runs above check exactly these.

## How it's built (`buildsys/`)
- **SDL 2.30.0, static:**
  - Its "dummy + evdev" video driver is patched to scale each frame onto the Linux framebuffer (`patches/SDL_null*.c`).
  - Its evdev input scans `/dev/input` itself, since there's no udev (`patches/SDL_evdev.c`).
  - ALSA is loaded at run time.
- **MAME 0.242 cross-compiled** (`build-mame.sh`) with only the six drivers, `-static-libstdc++`. Patches in
  `patches/mame/`:
  - no fontconfig, SDL2_ttf, EGL or X11
  - the SDL renderers don't require OpenGL
  - GCC 13 `<cstdint>` fixes
- `mpc_stubs.c` maps glibc 2.35–2.38 symbols back to the MPC's 2.34 and stubs the unused EGL/fontconfig calls.
