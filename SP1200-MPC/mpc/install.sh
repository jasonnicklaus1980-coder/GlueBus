#!/bin/sh
# SP1200 installer for Gen1 MPC OS devices (MPC Live/Live II/One/X/Key 61, Force). Run ON the device as root:
#   sh install.sh [-y]
# Copies sp1200.so to /sdcard/vst, backs up MPC.settings, registers the plugin, and restarts MPC
# (MPC is stopped for the edit - save your project first). Safe to re-run: it upgrades in place.
set -e
cd "$(dirname "$0")"
NAME='SP1200'; VERSION='2.0.0'; SO_DIR='/sdcard/vst'; SO='sp1200.so'; SKIN='GlueBus - VST - SP1200'
YES=0; [ "$1" = "-y" ] && YES=1
die() { echo "error: $*" >&2; exit 1; }

# SP1200_TEST_ROOT: test hook that redirects all paths into a fake root and skips device checks.
PFX="${SP1200_TEST_ROOT:-}"
if [ -z "$PFX" ]; then
    [ "$(id -u)" = 0 ] || die "run as root"
    case "$(uname -m)" in armv7*) ;; *) die "this build is for 32-bit ARM MPC OS devices (Gen1); this one is $(uname -m)" ;; esac
    command -v systemctl >/dev/null || die "systemctl not found"
fi
SETTINGS=$(ls "$PFX"/media/az01-internal/Settings/*/MPC.settings 2>/dev/null | head -n 1)
[ -n "$SETTINGS" ] || die "MPC.settings not found (not an MPC OS device?)"
[ -d "$(dirname "$PFX$SO_DIR")" ] || die "$(dirname "$SO_DIR") not found"
[ -d "payload/Synths/$SKIN" ] || die "skin folder missing from the package"
grep -q '/sdcard/Synths' "$SETTINGS" || echo "warning: /sdcard/Synths isn't in MPC's SynthContentLocations; the skin may not show"
if [ -f SHA256SUMS ]; then sha256sum -c SHA256SUMS >/dev/null 2>&1 || die "files damaged (SHA256SUMS mismatch): copy the folder again"; fi

echo "Installing $NAME $VERSION:"
echo "  $SO_DIR/$SO, /sdcard/Synths/$SKIN, and an entry in $SETTINGS"
if [ $YES = 0 ]; then
    printf "MPC will be stopped and restarted. Save your project first. Continue? [y/N] "
    read -r ok; case "$ok" in y|Y|yes) ;; *) echo "cancelled"; exit 1 ;; esac
fi

if [ -z "$PFX" ]; then
    systemctl stop acvs
    trap 'systemctl start acvs' EXIT
    i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
    pidof MPC >/dev/null && die "MPC did not stop"
fi

mkdir -p "$PFX$SO_DIR"
cp payload/vst/"$SO" "$PFX$SO_DIR/$SO.new" && mv "$PFX$SO_DIR/$SO.new" "$PFX$SO_DIR/$SO"
mkdir -p "$PFX/sdcard/Synths"
rm -rf "$PFX/sdcard/Synths/$SKIN"; cp -a "payload/Synths/$SKIN" "$PFX/sdcard/Synths/$SKIN"
# 1.0.0 put the skin under the wrong name (MPC looks for "<manufacturer> - VST - <plugin>"); remove it
rm -rf "$PFX/sdcard/Synths/SP1200 - VST - SP1200"

BAK="$SETTINGS.bak-sp1200-$(date +%Y%m%d-%H%M%S)"
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
echo "Starting MPC. Add '$NAME' as an insert effect from the plugin browser."
