#!/bin/sh
# Da Sample Lab uninstaller. Run ON the device as root:  sh uninstall.sh [-y]
# Stops MPC, removes dasamplelab.so + dasamplelab_capture.so, their skins and MPC.settings entries (after a backup), starts MPC again.
# Your samples and exports in /sdcard/SampleLab are left alone.
set -e
cd "$(dirname "$0")"
NAME='Da Sample Lab'; SO_DIR='/sdcard/vst'
YES=0; [ "$1" = "-y" ] && YES=1
die() { echo "error: $*" >&2; exit 1; }

PFX="${SAMPLELAB_TEST_ROOT:-}"
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

BAK="$SETTINGS.bak-dasamplelab-$(date +%Y%m%d-%H%M%S)"
cp "$SETTINGS" "$BAK"
cp "$SETTINGS" "$SETTINGS.new"
for so in dasamplelab.so dasamplelab_capture.so; do
    awk -v mode=remove -v file="$SO_DIR/$so" -f plugin_list.awk "$SETTINGS.new" > "$SETTINGS.tmp" && mv "$SETTINGS.tmp" "$SETTINGS.new"
    n=$(grep -c "file=\"$SO_DIR/$so\"" "$SETTINGS.new" || true)
    [ "$n" = 0 ] || { rm -f "$SETTINGS.new"; die "settings edit failed; MPC.settings unchanged"; }
done
if command -v python3 >/dev/null; then
    python3 -c 'import sys, xml.etree.ElementTree as E; E.parse(sys.argv[1])' "$SETTINGS.new" 2>/dev/null ||
        { rm -f "$SETTINGS.new"; die "edited settings aren't valid XML; MPC.settings unchanged"; }
fi
mv "$SETTINGS.new" "$SETTINGS"
rm -f "$PFX$SO_DIR/dasamplelab.so" "$PFX$SO_DIR/dasamplelab_capture.so"
rm -rf "$PFX/sdcard/Synths/RadioReady Audio - VST - Da Sample Lab" "$PFX/sdcard/Synths/RadioReady Audio - VST - Da Sample Lab Capture"
sync
echo "Removed $NAME. Settings backup: $BAK. /sdcard/SampleLab was kept."
