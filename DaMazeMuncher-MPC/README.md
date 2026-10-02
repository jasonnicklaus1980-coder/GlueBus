# Da Maze Muncher (MPC)

An original maze-chase game as a **native MPC OS VST2 instrument** for the MPC X and other Gen1 devices, built on the
same dependency-free VST2 core, installer and skin pipeline as the GlueBus / RadioReady plugins.

- Eat all the dots while four glitch bugs hunt you: **Kick** chases you, **Snare** cuts in front of you, **Hat** roams
  and pounces when close, **Clap** guards the records. A **record** scares them for a few seconds: eat them for
  200 / 400 / 800 / 1600. Bonus note, extra life at 10,000, levels speed up, high score saved with the project.
- Played with the **pads** of a plugin track (pad 10 up, 5 left, 6 down, 7 right, 13 / 16 start / pause), or the
  on-screen D-pad and START key. Original synthesised sounds and a drum beat whose tempo rises with the level.
- How it draws: each of the 175 maze cells is a plugin parameter whose value picks a picture from a filmstrip; the
  plugin notifies MPC when a cell changes, so the game screen updates cell by cell.
- Original game design, maze, characters, sounds and art (no material from any existing game).

## Build + install
    make test                 # native build + test/host_test (maze checks, 40 bot-played games, plugin interface)
    ./scripts/package.sh      # ARM build, ELF checks (libc/libm only, glibc <= 2.34), dist/DaMazeMuncher-1.0.0-mpc-armv7.zip
    python3 tools/make_skin.py docs   # regenerate the skin and docs/skin-preview.png (needs Pillow and a native build)
Device steps: `mpc/INSTALL.md`.
