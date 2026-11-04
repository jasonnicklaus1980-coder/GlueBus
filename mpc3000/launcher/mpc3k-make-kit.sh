#!/bin/sh
# mpc3k-make-kit.sh - turn an MPC X kit (.xpm) or a folder of WAVs into an MPC3000 kit floppy.
# The floppy goes to /data/mpc3k/floppy.img, which mpc3k-run.sh inserts automatically.
#
#   sh /data/mpc3k/mpc3k-make-kit.sh --find [FOLDER]        list .xpm kits (default: /media/SSD)
#   sh /data/mpc3k/mpc3k-make-kit.sh "/path/to/Kit.xpm"     pads 1-16 of that kit -> A01-A16
#   sh /data/mpc3k/mpc3k-make-kit.sh "/path/to/wav folder"  first 16 WAVs (by name) -> A01-A16
#
# If /data/mpc3k/hd.img is formatted (by the MPC3000), the kit is added to partition A (MPC3K_PART=B..Z for
# others; each partition holds ~22-30 MB). Otherwise a floppy image is made: about 16 seconds in total,
# longer kits are trimmed to fit (with a fade).
# On the MPC3000: DISK, 7, pick the .PGM file, soft key 1 <Load>.
DIR="${MPC3K_DIR:-/data/mpc3k}"; BIN="$DIR/mpc3k_mpcx_armhf"; OUT="${MPC3K_FLOPPY_OUT:-$DIR/floppy.img}"
PART="${MPC3K_PART:-A}"
# If the hard disk has been formatted by the MPC3000, kits go there (no 16-second limit);
# otherwise they go to a floppy image.
if [ -z "$MPC3K_FLOPPY_OUT" ] && [ -f "$DIR/hd.img" ] && dd if="$DIR/hd.img" bs=1 skip=3 count=6 2>/dev/null | grep -q "MPC-60"; then
    OUT="$DIR/hd.img"
fi
[ -f "$BIN" ] || { echo "Missing $BIN"; exit 1; }
if [ "$1" = "--find" ]; then
    find "${2:-/media/SSD}" -iname '*.xpm' 2>/dev/null | head -200
    exit 0
fi
[ -n "$1" ] || { echo "usage: sh $0 --find [FOLDER] | KIT.xpm | WAV_FOLDER"; exit 1; }
if [ "$OUT" = "$DIR/hd.img" ]; then
    "$BIN" --make-kit "$1" "$OUT" --part "$PART" && echo "Added to SCSI DISK PART $PART (MPC3K_PART=B etc. for another partition)"
else
    "$BIN" --make-kit "$1" "$OUT" && echo "Ready: $OUT (inserted next time you run mpc3k-run.sh)"
fi
