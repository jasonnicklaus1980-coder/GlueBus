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

## Install (copies files only)
    mkdir -p /sdcard/mpcarcade/roms
    # copy everything from this folder to /sdcard/mpcarcade, then:
    chmod +x /sdcard/mpcarcade/mame /sdcard/mpcarcade/hwtest
Put **ROM sets you legally own** in `/sdcard/mpcarcade/roms` as zip files named like the set: `pacman.zip`,
`mspacman.zip`, `galaga.zip`, `digdug.zip`, `dkong.zip`, `invaders.zip`, `frogger.zip`, `centiped.zip`. They must
match MAME 0.242's sets (newer MAME sets usually work for these old games). No ROMs are included.

## Play (save your MPC project first)
    cd /sdcard/mpcarcade
    sh arcade.sh run ./mame.sh pacman          # one game
    sh arcade.sh run ./mame.sh                 # MAME's game list: tap a game, "Exit" at the bottom quits
When MAME exits, the MPC app starts again by itself.

### Controls
- **USB keyboard (recommended for the first test):** arrows = joystick, Left Ctrl = fire, `5` = coin, `1` = start,
  `Tab` = MAME menu, `P` = pause, **`Esc` = quit back to the MPC**.
- **Touchscreen:** acts as a mouse, so you can tap entries in MAME's menus.
- **MPC pads, buttons and knobs:** not mapped yet. Run `hwtest` (below) and send me the log so I can map them.
- **Exit without a keyboard:** from SSH run `sh /sdcard/mpcarcade/arcade.sh stop`. Every session also ends by itself
  after 30 minutes (`--max SECONDS` changes that).

### If the picture is sideways or upside down
Set `SDL_FBROTATE` to `90`, `180` or `270`, e.g. `SDL_FBROTATE=90 sh arcade.sh run ./mame.sh pacman`.

## Recovery
- The MPC app doesn't come back: `sh /sdcard/mpcarcade/arcade.sh restore` (or `systemctl start acvs`).
- No SSH and a black screen: wait about 2 minutes past the session limit (a watchdog starts the MPC app), or
  **power-cycle**. Nothing here runs at boot.
- Remove everything: `rm -r /sdcard/mpcarcade /sdcard/mpcx-survey.txt`.

## Recommended first runs
1. `sh survey.sh` (read-only) and `sh arcade.sh run --max 60 ./hwtest`: confirms screen, sound and inputs.
2. `sh arcade.sh run --max 120 ./mame.sh` with a USB keyboard: the game list should appear full-screen.
3. Then a game. Send me `/sdcard/mpcarcade/*.log` if anything looks wrong.

## What's verified so far (off-device)
- The binary is ARM EABI5 hard-float and needs only libc/libm. Its highest glibc symbol is 2.34, the same level
  the MPC already runs for the plugins.
- Run under QEMU (ARM emulation), it lists all 771 sets and renders MAME's full game list through the patched
  SDL video path (the same code that writes to `/dev/fb0` on the device).
- **Not yet verified on the MPC itself:** framebuffer format and orientation, ALSA device access, speed. The first
  runs above check exactly these.

## How it's built (`buildsys/`)
- **SDL 2.30.0, static:** its "dummy + evdev" video driver is patched to scale each frame onto the Linux
  framebuffer (`patches/SDL_null*.c`); ALSA loaded at run time; evdev input.
- **MAME 0.242 cross-compiled** (`build-mame.sh`) with only the six drivers, `-static-libstdc++`. Patches in
  `patches/mame/`:
  - no fontconfig, SDL2_ttf, EGL or X11
  - the SDL renderers don't require OpenGL
  - GCC 13 `<cstdint>` fixes
- `mpc_stubs.c` maps glibc 2.35–2.38 symbols back to the MPC's 2.34 and stubs the unused EGL/fontconfig calls.
