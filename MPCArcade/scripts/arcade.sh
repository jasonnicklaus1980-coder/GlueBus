#!/bin/sh
# MPC Arcade launcher: runs a full-screen program (the test, later the emulator) with the MPC app stopped, and
# ALWAYS brings the MPC app back.  Nothing here runs at boot and nothing is installed into the system: a power
# cycle always starts the normal MPC.
#
#   sh arcade.sh run [--max SECONDS] PROGRAM [ARGS...]   stop MPC app -> run PROGRAM -> start MPC app again
#   sh arcade.sh restore                                 start the MPC app (manual recovery)
#   sh arcade.sh status                                  show whether the MPC app is running
#   sh arcade.sh stop                                    end the running arcade program (the MPC app then comes back)
#
# CHANGES SYSTEM STATE (temporarily): `run` stops the MPC service (systemctl stop acvs) and starts it again when the
# program ends, crashes, is killed, times out, or the SSH session drops. A separate watchdog process also starts
# it after --max + 60 seconds in case this script itself is killed. Save your MPC project before using it.
set -u
SVC=acvs
LOG_DIR=/sdcard/mpcarcade
die() { echo "arcade: $*" >&2; exit 1; }
restore() { systemctl start "$SVC" && echo "arcade: MPC app started again"; }

case "${1:-}" in
    restore) restore; exit $? ;;
    status) printf "MPC app (%s): " "$SVC"; systemctl is-active "$SVC"; exit 0 ;;
    stop) pkill -TERM -x mame 2>/dev/null; pkill -TERM -x hwtest 2>/dev/null; sleep 3; systemctl is-active "$SVC" >/dev/null || restore; exit 0 ;;
    run) shift ;;
    *) sed -n '2,13p' "$0"; exit 1 ;;
esac
MAX=1800
if [ "${1:-}" = "--max" ]; then MAX="$2"; shift 2; fi
[ $# -ge 1 ] || die "no program given"
[ "$(id -u)" = 0 ] || die "run as root"
command -v systemctl >/dev/null || die "systemctl not found"
[ -x "$1" ] || die "$1 is not an executable file"
mkdir -p "$LOG_DIR"
LOG="$LOG_DIR/$(basename "$1").log"

# safety net that outlives this script and the SSH session
setsid sh -c "sleep $((MAX + 60)); systemctl is-active $SVC >/dev/null || systemctl start $SVC" >/dev/null 2>&1 < /dev/null &

trap 'restore; trap - EXIT; exit' EXIT INT TERM HUP
echo "arcade: stopping the MPC app (it comes back when the program ends, max ${MAX}s)"
systemctl stop "$SVC"
i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
pidof MPC >/dev/null && die "MPC did not stop; not starting the program"

echo "arcade: running $* (log: $LOG)"
if command -v timeout >/dev/null; then timeout -s TERM "$MAX" "$@" > "$LOG" 2>&1
else "$@" > "$LOG" 2>&1; fi
echo "arcade: program ended (exit $?)"
