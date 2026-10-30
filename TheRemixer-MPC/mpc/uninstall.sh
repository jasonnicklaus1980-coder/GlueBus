#!/bin/sh
# Removes The Remixer: deletes /sdcard/vst/theremixer.so and its MPC.settings entry. Recordings are kept.
# Run on the device as root:  sh uninstall.sh [-y]
set -e
cd "$(dirname "$0")"
NAME='The Remixer'; SO_DIR='/sdcard/vst'; SO='theremixer.so'
YES=0; [ "$1" = "-y" ] && YES=1
die() { echo "error: $*" >&2; exit 1; }

PFX="${TR_TEST_ROOT:-}"
[ -n "$PFX" ] || [ "$(id -u)" = 0 ] || die "run as root"
SETTINGS=$(ls "$PFX"/media/az01-internal/Settings/*/MPC.settings 2>/dev/null | head -n 1)
[ -n "$SETTINGS" ] || die "MPC.settings not found"
if [ $YES = 0 ]; then
    printf "Remove %s? Recordings in /media/az01-internal/TheRemixer are kept. MPC will be stopped and restarted. [y/N] " "$NAME"
    read -r ok; case "$ok" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi

if [ -z "$PFX" ]; then
    systemctl stop acvs
    trap 'systemctl start acvs' EXIT
    i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
    pidof MPC >/dev/null && die "MPC did not stop"
fi

BAK="$SETTINGS.bak-theremixer-$(date +%Y%m%d-%H%M%S)"
cp "$SETTINGS" "$BAK"
awk -v mode=remove -v file="$SO_DIR/$SO" -f plugin_list.awk "$SETTINGS" > "$SETTINGS.new"
n=$(grep -c "file=\"$SO_DIR/$SO\"" "$SETTINGS.new" || true)
[ "$n" = 0 ] || { rm -f "$SETTINGS.new"; die "settings edit failed; MPC.settings unchanged"; }
mv "$SETTINGS.new" "$SETTINGS"
rm -f "$PFX$SO_DIR/$SO"
sync

echo "Removed $NAME. Settings backup: $BAK"
