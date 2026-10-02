#!/bin/sh
# MPC Arcade launcher. Called by the MPC Arcade plugin (or over SSH):
#   sh /media/az01-internal/MAME/system/launch.sh library|play|resume [GAME]
# It starts system/session.sh OUTSIDE the MPC app's service, so that stopping the MPC app (which the session does
# first) doesn't stop the arcade too. With systemd-run the session gets its own transient unit and a clean process
# (nothing inherited from MPC); otherwise it moves itself out of MPC's cgroup.
D=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$D/logs" "$D/system/run"
LOG="$D/logs/launch.log"
echo "$(date '+%F %T') launch $*" >> "$LOG"
if [ -f "$D/system/run/session.pid" ] && kill -0 "$(cat "$D/system/run/session.pid")" 2>/dev/null; then
    echo "  a session is already running" >> "$LOG"; exit 0
fi
if command -v systemd-run >/dev/null 2>&1; then
    U="mpcarcade-$(date +%s)"
    systemd-run --unit="$U" --collect --quiet /bin/sh "$D/system/session.sh" "${1:-library}" "$2" >> "$LOG" 2>&1 && exit 0
    systemd-run --unit="$U" --quiet /bin/sh "$D/system/session.sh" "${1:-library}" "$2" >> "$LOG" 2>&1 && exit 0
    echo "  systemd-run failed; using the fallback" >> "$LOG"
fi
for cg in /sys/fs/cgroup/systemd/cgroup.procs /sys/fs/cgroup/unified/cgroup.procs /sys/fs/cgroup/cgroup.procs; do
    [ -w "$cg" ] && echo $$ > "$cg" 2>/dev/null
done
setsid /bin/sh "$D/system/session.sh" "${1:-library}" "$2" >> "$LOG" 2>&1 < /dev/null &
exit 0
