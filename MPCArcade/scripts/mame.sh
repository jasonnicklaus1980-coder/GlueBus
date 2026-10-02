#!/bin/sh
# Run MAME full-screen on the MPC X framebuffer. Start it THROUGH the launcher, which stops the MPC app first and
# always restarts it:   sh /sdcard/mpcarcade/arcade.sh run /sdcard/mpcarcade/mame.sh [game]
#   no game: MAME's game list (tap a game on the touchscreen to start it; "Exit" at the bottom quits)
#   game:    e.g. pacman, mspacman, galaga, digdug, dkong, invaders, frogger, centiped
# Everything lives in /sdcard/mpcarcade: roms/, cfg/, nvram/, hi/, snap/. Nothing is written elsewhere.
D=/sdcard/mpcarcade
mkdir -p "$D/roms" "$D/cfg" "$D/nvram" "$D/snap" "$D/ini"
export SDL_VIDEODRIVER=evdev          # SDL "dummy + evdev input" driver, patched to draw on the framebuffer
export SDL_FBDEV=/dev/fb0
export SDL_FBROTATE="${SDL_FBROTATE:-0}"
export SDL_AUDIODRIVER=alsa
export HOME="$D"
[ -f /sys/class/graphics/fbcon/cursor_blink ] && echo 0 > /sys/class/graphics/fbcon/cursor_blink 2>/dev/null
exec "$D/mame" -rompath "$D/roms" -cfg_directory "$D/cfg" -nvram_directory "$D/nvram" -snapshot_directory "$D/snap" \
     -inipath "$D/ini" -homepath "$D" -video soft -sound sdl -samplerate 48000 -audio_latency 2 \
     -skip_gameinfo -keepaspect -nofilter -mouse -nowindow "$@"
