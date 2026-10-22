#!/bin/sh
# Legacy Sampler uninstaller. Run ON the device as root:  sh uninstall.sh [-y]
# Stops MPC, removes mc909_sampler.so and its MPC.settings entry (after a backup), starts MPC again.
# Your project file in /sdcard/LegacySampler is kept.
set -e
cd "$(dirname "$0")"
NAME='Legacy Sampler'; SO_DIR='/sdcard/vst'; SO='mc909_sampler.so'
YES=0; [ "$1" = "-y" ] && YES=1
die() { echo "error: $*" >&2; exit 1; }

PFX="${MC909_TEST_ROOT:-}"
[ -n "$PFX" ] || [ "$(id -u)" = 0 ] || die "run as root"
SETTINGS=$(ls "$PFX"/media/az01-internal/Settings/*/MPC.settings 2>/dev/null | head -n 1)
[ -n "$SETTINGS" ] && [ -f "$SETTINGS" ] || die "MPC.settings not found"
if [ $YES = 0 ]; then
    printf "Remove %s? MPC will be stopped and restarted. Save your project first. [y/N] " "$NAME"
    read -r ok; case "$ok" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi

if [ -z "$PFX" ]; then
    if systemctl cat acvs >/dev/null 2>&1; then SVC=acvs; else SVC=inmusic-mpc; fi
    systemctl stop "$SVC"
    trap 'systemctl start "$SVC"' EXIT
    i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
    pidof MPC >/dev/null && die "MPC did not stop"
fi

BAK="$SETTINGS.bak-mc909-$(date +%Y%m%d-%H%M%S)"
cp "$SETTINGS" "$BAK"
awk -v mode=remove -v file="$SO_DIR/$SO" -v uid="4c534d50" -f plugin_list.awk "$SETTINGS" > "$SETTINGS.new"
n=$(grep -c "file=\"$SO_DIR/$SO\"" "$SETTINGS.new" || true)
[ "$n" = 0 ] || { rm -f "$SETTINGS.new"; die "settings edit failed; MPC.settings unchanged"; }
mv "$SETTINGS.new" "$SETTINGS"
rm -f "$PFX$SO_DIR/$SO"
sync
echo "Removed $NAME. Settings backup: $BAK"
