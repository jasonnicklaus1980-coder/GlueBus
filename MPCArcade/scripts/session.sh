#!/bin/sh
# MPC Arcade session: steps the MPC app aside, runs the library and the games, and ALWAYS starts the MPC app again
# (on exit, crash, kill, or the 6-second EXIT "panic" hold). Started by launch.sh; not meant to be run by hand.
#   session.sh library|play|resume [GAME]
# Settings that can be overridden in system/arcade.conf (sourced if present):
#   SVC=acvs            the MPC app's systemd service
#   FBDEV=/dev/fb0      the framebuffer
D=$(cd "$(dirname "$0")/.." && pwd)
SYS="$D/system"; RUN="$SYS/run"; LOGS="$D/logs"
SVC=acvs; FBDEV=/dev/fb0
[ -f "$SYS/arcade.conf" ] && . "$SYS/arcade.conf"
MODE=${1:-library}; GAME=$2
mkdir -p "$RUN" "$LOGS"
echo $$ > "$RUN/session.pid"
rm -f "$RUN/panic" "$RUN/capture" "$RUN/launch"
log() { echo "$(date '+%T') $*"; }

BRIDGE=
restore() {
    [ -n "$BRIDGE" ] && kill "$BRIDGE" 2>/dev/null
    pkill -TERM -x romdrop 2>/dev/null
    [ -n "$NO_MPC_RESTART" ] || systemctl start "$SVC"
    rm -f "$RUN/session.pid"
    log "session ended; MPC app started again"
}
trap 'restore; trap - EXIT; exit 0' EXIT INT TERM HUP

log "session start: $MODE $GAME"
if [ -z "$NO_MPC_RESTART" ]; then
    systemctl stop "$SVC"
    i=0; while pidof MPC >/dev/null 2>&1 && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
    if pidof MPC >/dev/null 2>&1; then log "the MPC app did not stop; giving up"; exit 1; fi
fi

export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-evdev}  # SDL's dummy video + evdev input, patched to draw on the framebuffer
export SDL_FBDEV=$FBDEV
export SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-alsa}
export HOME="$SYS"
[ -w /sys/class/graphics/fbcon/cursor_blink ] && echo 0 > /sys/class/graphics/fbcon/cursor_blink 2>/dev/null

"$SYS/padbridge" run --root "$D" > "$LOGS/padbridge.log" 2>&1 &
BRIDGE=$!
sleep 1                                           # the virtual keyboard must exist before SDL looks for devices

ARGS=
case "$MODE" in
    play|resume) [ -n "$GAME" ] && ARGS="--launch $GAME" ;;
esac
fails=0
while :; do
    # shellcheck disable=SC2086
    "$SYS/arcade-ui" --root "$D" $ARGS >> "$LOGS/arcade-ui.log" 2>&1
    rc=$?
    ARGS=
    [ -f "$RUN/panic" ] && { log "panic exit"; break; }
    case $rc in
        0) log "library closed"; break ;;
        10) ;;
        *) fails=$((fails + 1)); log "library exited with $rc ($fails)"; [ $fails -ge 3 ] && break; sleep 1; continue ;;
    esac
    [ -f "$RUN/launch" ] || { log "no launch file"; continue; }
    G=$(sed -n 's/^GAME://p' "$RUN/launch" | head -n 1)
    t0=$(date +%s)
    log "playing $G"
    (
        set --
        while IFS= read -r line; do
            case "$line" in
                ENV:*) export "${line#ENV:}" ;;
                ARG:*) set -- "$@" "${line#ARG:}" ;;
            esac
        done < "$RUN/launch"
        exec "$SYS/mame" "$@"
    ) > "$LOGS/mame.log" 2>&1
    mrc=$?
    t1=$(date +%s)
    echo "$G $t0 $t1 $mrc" >> "$RUN/plays.log"
    log "$G ended ($mrc) after $((t1 - t0)) s"
    rm -f "$RUN/launch"
    if [ $mrc -ne 0 ] && [ $((t1 - t0)) -lt 10 ] && [ ! -f "$RUN/panic" ]; then
        { echo "MAME exit code $mrc. End of MAME/logs/mame.log:"; tail -n 8 "$LOGS/mame.log"; } > "$RUN/lasterror"
    fi
    [ -f "$RUN/panic" ] && { log "panic exit"; break; }
    ARGS="--select $G"
    fails=0
done
exit 0
