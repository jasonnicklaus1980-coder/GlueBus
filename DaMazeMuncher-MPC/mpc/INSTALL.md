# Da Maze Muncher 1.0.0

An original maze-chase game as a native MPC OS instrument plugin. Eat every dot in the maze while four glitch bugs
(Kick, Snare, Hat and Clap) hunt you down. Grab a spinning record and the bugs get scared: eat them for big points.
Clear the maze to reach the next level, which is faster (and so is the drum beat).

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live / Live II, One, Key 61, Force).
- **Root shell access** (SSH). Installing plugins this way is unofficial: back up first, use at your own risk.

## Install
1. Copy the zip to the device (e.g. `/tmp`), then:
   `cd /tmp && unzip -o DaMazeMuncher-1.0.0-mpc-armv7.zip && sh DaMazeMuncher-1.0.0/install.sh`
2. The installer copies `mazemuncher.so` to `/sdcard/vst/` and the skin to
   `/sdcard/Synths/RadioReady Audio - VST - Da Maze Muncher/`, **stops MPC** (save your project first), backs up
   `MPC.settings`, registers the plugin as an instrument and starts MPC again. Re-running upgrades in place.
3. Make a **PLUGIN track** and pick **Da Maze Muncher** (RadioReady Audio) as its instrument. The pads now play the game.

## How to play
| Control | Pads (bank A, any bank works) | Screen |
|---|---|---|
| Up | pad 10 | ▲ |
| Left | pad 5 | ◀ |
| Down | pad 6 | ▼ |
| Right | pad 7 | ▶ |
| Start / pause / resume | pad 13 or 16 | START / PAUSE |

The Muncher keeps going in its direction and turns as soon as the way opens, so press the next direction a little
early. Q-Link 1 also steers, Q-Link 2 is START.

- Dot 10 points, record 50, bugs 200 / 400 / 800 / 1600 in a row, bonus note (appears twice per level) 100+.
- Extra life at 10,000. The high score is saved with your project.
- SPEED: Slow / Normal / Fast. DRUMS: the beat on or off. VOLUME: game sound level.

## Notes
- The screen moves in steps (one cell at a time): the game draws itself through MPC's plugin parameters.
- If you record automation on this track, the game's screen updates can be recorded too. Don't record-arm it.
- The game sounds come out of the plugin track like any instrument; mute the track to play silently.

## Uninstall
`sh DaMazeMuncher-1.0.0/uninstall.sh` removes the plugin, its skin and the plugin-list entry.
