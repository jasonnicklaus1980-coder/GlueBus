#!/bin/sh
# Da Sample Lab installer for Gen1 MPC OS devices (MPC Live/Live II/One/X/Key 61, Force). Run ON the device as root:
#   sh install.sh [-y]
# Copies dasamplelab.so (the sampler, an instrument) and dasamplelab_capture.so (the recorder, an insert effect) to
# /sdcard/vst, their skins to /sdcard/Synths, a demo loop to /sdcard/SampleLab/Samples (never overwriting your files),
# backs up MPC.settings, registers both plugins, and restarts MPC (MPC is stopped for the edit - save your project
# first). Safe to re-run: it upgrades in place.
set -e
cd "$(dirname "$0")"
NAME='Da Sample Lab'; VERSION='1.1.0'; SO_DIR='/sdcard/vst'
# plugin file : plugin-list entry : skin folder
PLUGINS='dasamplelab.so:plugin.xml:RadioReady Audio - VST - Da Sample Lab
dasamplelab_capture.so:plugin-capture.xml:RadioReady Audio - VST - Da Sample Lab Capture'
YES=0; [ "$1" = "-y" ] && YES=1
die() { echo "error: $*" >&2; exit 1; }

# SAMPLELAB_TEST_ROOT: test hook that redirects all paths into a fake root and skips device checks.
PFX="${SAMPLELAB_TEST_ROOT:-}"
if [ -z "$PFX" ]; then
    [ "$(id -u)" = 0 ] || die "run as root"
    case "$(uname -m)" in armv7*) ;; *) die "this build is for 32-bit ARM MPC OS devices (Gen1); this one is $(uname -m)" ;; esac
    command -v systemctl >/dev/null || die "systemctl not found"
fi
SETTINGS=$(ls "$PFX"/media/az01-internal/Settings/*/MPC.settings 2>/dev/null | head -n 1)
[ -n "$SETTINGS" ] || die "MPC.settings not found (not an MPC OS device?)"
[ -d "$(dirname "$PFX$SO_DIR")" ] || die "$(dirname "$SO_DIR") not found"
echo "$PLUGINS" | while IFS=: read -r so xml skin; do
    [ -f "payload/vst/$so" ] && [ -f "$xml" ] && [ -d "payload/Synths/$skin" ] || die "$so, $xml or its skin is missing from the package"
done
grep -q '/sdcard/Synths' "$SETTINGS" || echo "warning: /sdcard/Synths isn't in MPC's SynthContentLocations; the skin may not show"
if [ -f SHA256SUMS ]; then sha256sum -c SHA256SUMS >/dev/null 2>&1 || die "files damaged (SHA256SUMS mismatch): copy the folder again"; fi

echo "Installing $NAME $VERSION:"
echo "  $SO_DIR/dasamplelab.so + dasamplelab_capture.so, their skins in /sdcard/Synths, /sdcard/SampleLab,"
echo "  and two entries in $SETTINGS"
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

mkdir -p "$PFX$SO_DIR" "$PFX/sdcard/Synths"
echo "$PLUGINS" | while IFS=: read -r so xml skin; do
    cp "payload/vst/$so" "$PFX$SO_DIR/$so.new" && mv "$PFX$SO_DIR/$so.new" "$PFX$SO_DIR/$so"
    rm -rf "$PFX/sdcard/Synths/$skin"; cp -a "payload/Synths/$skin" "$PFX/sdcard/Synths/$skin"
done
# samples folder + demo project (existing files are kept)
mkdir -p "$PFX/sdcard/SampleLab/Samples/Captures" "$PFX/sdcard/SampleLab/Exports"
for f in payload/SampleLab/Samples/*.wav; do [ -e "$PFX/sdcard/SampleLab/Samples/$(basename "$f")" ] || cp "$f" "$PFX/sdcard/SampleLab/Samples/"; done

BAK="$SETTINGS.bak-dasamplelab-$(date +%Y%m%d-%H%M%S)"
cp "$SETTINGS" "$BAK"
cp "$SETTINGS" "$SETTINGS.new"
for pair in dasamplelab.so:plugin.xml dasamplelab_capture.so:plugin-capture.xml; do
    so=${pair%%:*}; xml=${pair#*:}
    awk -v mode=add -v file="$SO_DIR/$so" -v entryfile="$xml" -f plugin_list.awk "$SETTINGS.new" > "$SETTINGS.tmp" && mv "$SETTINGS.tmp" "$SETTINGS.new"
    n=$(grep -c "file=\"$SO_DIR/$so\"" "$SETTINGS.new" || true)
    [ "$n" = 1 ] || { rm -f "$SETTINGS.new"; die "settings edit failed for $so (entry count $n); MPC.settings unchanged"; }
done
if command -v python3 >/dev/null; then
    python3 -c 'import sys, xml.etree.ElementTree as E; E.parse(sys.argv[1])' "$SETTINGS.new" 2>/dev/null ||
        { rm -f "$SETTINGS.new"; die "edited settings aren't valid XML; MPC.settings unchanged"; }
fi
mv "$SETTINGS.new" "$SETTINGS"
sync

echo "Done. Settings backup: $BAK"
echo "Starting MPC. Make a PLUGIN track and pick '$NAME' (RadioReady Audio) as its instrument."
echo "To sample inside MPC: insert 'Da Sample Lab Capture' (an effect) on any track or the master and tap RECORD."
echo "Put WAVs in /sdcard/SampleLab/Samples (or on a USB drive), choose one with the SAMPLE knob and tap LOAD."
echo "Pads A1-A16 play slices 1-16."
