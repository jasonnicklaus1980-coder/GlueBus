#!/bin/sh
# SP1200 uninstaller. Run ON the device as root:  sh uninstall.sh [-y]
# Stops MPC, removes sp1200.so and its MPC.settings entry (after a backup), starts MPC again.
set -e
cd "$(dirname "$0")"
NAME='SP1200'; SO_DIR='/sdcard/vst'; SO='sp1200.so'; SKIN='GlueBus - VST - SP1200'
YES=0; [ "$1" = "-y" ] && YES=1
die() { echo "error: $*" >&2; exit 1; }

PFX="${SP1200_TEST_ROOT:-}"
[ -n "$PFX" ] || [ "$(id -u)" = 0 ] || die "run as root"
SETTINGS=$(ls "$PFX"/media/az01-internal/Settings/*/MPC.settings 2>/dev/null | head -n 1)
[ -n "$SETTINGS" ] || die "MPC.settings not found"
if [ $YES = 0 ]; then
    printf "Remove %s? MPC will be stopped and restarted. Save your project first. [y/N] " "$NAME"
    read -r ok; case "$ok" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi

if [ -z "$PFX" ]; then
    systemctl stop acvs
    trap 'systemctl start acvs' EXIT
    i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
    pidof MPC >/dev/null && die "MPC did not stop"
fi

BAK="$SETTINGS.bak-sp1200-$(date +%Y%m%d-%H%M%S)"
cp "$SETTINGS" "$BAK"
awk -v mode=remove -v file="$SO_DIR/$SO" -f plugin_list.awk "$SETTINGS" > "$SETTINGS.new"
n=$(grep -c "file=\"$SO_DIR/$SO\"" "$SETTINGS.new" || true)
[ "$n" = 0 ] || { rm -f "$SETTINGS.new"; die "settings edit failed; MPC.settings unchanged"; }
if command -v python3 >/dev/null; then
    python3 -c 'import sys, xml.etree.ElementTree as E; E.parse(sys.argv[1])' "$SETTINGS.new" 2>/dev/null ||
        { rm -f "$SETTINGS.new"; die "edited settings aren't valid XML; MPC.settings unchanged"; }
fi
mv "$SETTINGS.new" "$SETTINGS"
rm -f "$PFX$SO_DIR/$SO"
rm -rf "$PFX/sdcard/Synths/$SKIN" "$PFX/sdcard/Synths/SP1200 - VST - SP1200"
sync
echo "Removed $NAME. Settings backup: $BAK"
