#!/bin/sh
# Legacy Sampler installer for Gen1 MPC OS devices (MPC X, Live / Live II, One, Key 61, Force).
# UNOFFICIAL: Akai has no third-party plugin SDK for MPC Standalone; this registers a VST2 instrument in MPC.settings,
# the route the other GlueBus MPC plugins use. Needs root (SSH). Back up first; use at your own risk.
# Run ON the device as root:  sh install.sh [-y]
# Copies mc909_sampler.so to /sdcard/vst, backs up MPC.settings, registers the instrument and restarts MPC
# (save your project first). Re-running upgrades. The sampler reads /sdcard/LegacySampler/project.mc909 (see INSTALL.md).
set -e
cd "$(dirname "$0")"
NAME='Legacy Sampler'; VERSION='0.1.0'; SO_DIR='/sdcard/vst'; SO='mc909_sampler.so'
YES=0; [ "$1" = "-y" ] && YES=1
die() { echo "error: $*" >&2; exit 1; }

# MC909_TEST_ROOT: test hook that redirects all paths into a fake root and skips the device checks.
PFX="${MC909_TEST_ROOT:-}"
if [ -z "$PFX" ]; then
    [ "$(id -u)" = 0 ] || die "run as root"
    case "$(uname -m)" in armv7*) ;; *) die "this build is for 32-bit ARM MPC OS devices (Gen1); this one is $(uname -m)" ;; esac
    command -v systemctl >/dev/null || die "systemctl not found"
fi
SETTINGS=$(ls "$PFX"/media/az01-internal/Settings/*/MPC.settings 2>/dev/null | head -n 1)
[ -n "$SETTINGS" ] || die "MPC.settings not found (not an MPC OS device?)"
[ -f "payload/vst/$SO" ] || die "plugin missing from the package"
if [ -f SHA256SUMS ]; then sha256sum -c SHA256SUMS >/dev/null 2>&1 || die "files damaged (SHA256SUMS mismatch): copy the folder again"; fi

echo "Installing $NAME $VERSION (unofficial MPC build):"
echo "  $SO_DIR/$SO and an entry in $SETTINGS"
if [ $YES = 0 ]; then
    printf "MPC will be stopped and restarted. Save your project first. Continue? [y/N] "
    read -r ok; case "$ok" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi

if [ -z "$PFX" ]; then
    # MPC's service is acvs on stock firmware, inmusic-mpc on Hakai-enabled systems
    if systemctl cat acvs >/dev/null 2>&1; then SVC=acvs; else SVC=inmusic-mpc; fi
    systemctl stop "$SVC"
    trap 'systemctl start "$SVC"' EXIT
    i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
    pidof MPC >/dev/null && die "MPC did not stop"
fi

mkdir -p "$PFX$SO_DIR"
cp "payload/vst/$SO" "$PFX$SO_DIR/$SO.new" && mv "$PFX$SO_DIR/$SO.new" "$PFX$SO_DIR/$SO"

BAK="$SETTINGS.bak-mc909-$(date +%Y%m%d-%H%M%S)"
cp "$SETTINGS" "$BAK"
awk -v mode=add -v file="$SO_DIR/$SO" -v entryfile=plugin.xml -f plugin_list.awk "$SETTINGS" > "$SETTINGS.new"
n=$(grep -c "file=\"$SO_DIR/$SO\"" "$SETTINGS.new" || true)
[ "$n" = 1 ] || { rm -f "$SETTINGS.new"; die "settings edit failed (entry count $n); MPC.settings unchanged"; }
if command -v python3 >/dev/null; then
    python3 -c 'import sys, xml.etree.ElementTree as E; E.parse(sys.argv[1])' "$SETTINGS.new" 2>/dev/null ||
        { rm -f "$SETTINGS.new"; die "edited settings aren't valid XML; MPC.settings unchanged"; }
fi
mv "$SETTINGS.new" "$SETTINGS"
sync

echo "Done. Settings backup: $BAK"
echo "Starting MPC. Load '$NAME' as an instrument on a program or track."
