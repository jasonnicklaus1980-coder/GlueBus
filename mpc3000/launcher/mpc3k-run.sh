#!/bin/sh
# mpc3k-run.sh - switch the MPC X into MPC3000 mode (run over SSH).
#
# Stops the MPC app, runs the MPC3000 emulator, and ALWAYS starts the MPC app again when the
# emulator exits (EXIT touch button, Ctrl+C, crash, or SSH disconnect). Nothing on the system
# partition is changed. Power-cycling the MPC X returns it to normal.
#
# Files in /data/mpc3k:
#   mpc3000_ls_ic13.bin, mpc3000_ms_ic14.bin   your v3.10 EPROM images (256 KB each)
#   mpc3k_mpcx_armhf                           the built front end
# Buttons: your MPC4000 map (/data/mpc4k/mpcx_buttons.map) is used automatically and translated.
#
# Usage: sh /data/mpc3k/mpc3k-run.sh [options, e.g. --rotate 270, --display drm, --learn]
#        MPC3K_SHORT_SEARCH=1 sh /data/mpc3k/mpc3k-run.sh   (2 s SCSI search instead of ~60 s)
#        MPC3K_FLOPPY=/data/mpc3k/os339.img sh /data/mpc3k/mpc3k-run.sh   (insert a floppy image)
#        or copy an image to /data/mpc3k/floppy.img to insert it every time
# Hard disk: /data/mpc3k/hd.img (created blank on first run; format it from the MPC3000 once).

DIR="${MPC3K_DIR:-/data/mpc3k}"
BIN="$DIR/mpc3k_mpcx_armhf"
LS="$DIR/mpc3000_ls_ic13.bin"; MS="$DIR/mpc3000_ms_ic14.bin"
[ -f "$LS" ] && [ -f "$MS" ] || { echo "Missing $LS or $MS - copy your MPC3000 v3.10 EPROM files there."; exit 1; }
[ -f "$BIN" ] || { echo "Missing $BIN - copy the built emulator there."; exit 1; }
chmod +x "$BIN" 2>/dev/null

MAP=""
if grep -q '^[0-9]' /data/mpc4k/mpcx_buttons.map 2>/dev/null; then
    MAP="--mpc4k-map /data/mpc4k/mpcx_buttons.map"
    echo "Using your MPC4000 button map (translated to MPC3000 buttons)"
fi
[ -f "$DIR/mpc3k_buttons.map" ] && MAP="$MAP --buttons $DIR/mpc3k_buttons.map"
SHORT=""; [ "$MPC3K_SHORT_SEARCH" = 1 ] && SHORT="--short-search"
FLOPPY=""                                   # a 1.44 MB or 720 KB raw image is inserted at power-on
[ -n "$MPC3K_FLOPPY" ] && FLOPPY="--floppy $MPC3K_FLOPPY"
[ -z "$FLOPPY" ] && [ -f "$DIR/floppy.img" ] && FLOPPY="--floppy $DIR/floppy.img" && echo "Inserting $DIR/floppy.img"

restore() {
    trap - EXIT INT TERM HUP
    echo "Returning to MPC X mode..."
    systemctl start acvs.service
}
trap restore EXIT INT TERM HUP
echo "Stopping the MPC app..."
systemctl stop acvs.service
sleep 1
HD="$DIR/hd.img"                            # SCSI hard disk, ID 0 (sparse file; format it once from the MPC3000)
if [ ! -f "$HD" ]; then
    dd if=/dev/zero of="$HD" bs=1M count=0 seek="${MPC3K_HD_MB:-256}" 2>/dev/null &&
    echo "Created blank $HD (${MPC3K_HD_MB:-256} MB). Format it once: DISK, 9, 3, <Next screen>, <Format now>, <Do it>."
fi
"$BIN" --rom "$LS,$MS" $MAP $SHORT $FLOPPY --hd "$HD" "$@"
