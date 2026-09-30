#!/usr/bin/env bash
# Build daclippads.so for the MPC (32-bit ARM hard-float), verify it, and assemble a release folder + zip
# in the same layout as the other GlueBus / RadioReady packages:
#   DaClipPads-1.0.0/{install.sh,uninstall.sh,plugin.xml,plugin_list.awk,INSTALL.md,MIDI.md,
#                     payload/vst/daclippads.so,payload/Synths/<skin>,payload/Clips/{Demo,Kits}}
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=1.0.0; NAME="DaClipPads-$VERSION"; SO=build/arm/daclippads.so

if command -v arm-linux-gnueabihf-g++ >/dev/null; then make arm
elif command -v docker >/dev/null; then make docker
else echo "Need arm-linux-gnueabihf-g++ (apt install g++-arm-linux-gnueabihf) or Docker."; exit 1; fi

# ---- verify the binary is what the MPC can load (32-bit ARM, hard-float, libc/libm only, glibc <= 2.34) ----
RE="${READELF:-readelf}"
if command -v "$RE" >/dev/null; then
  "$RE" -h "$SO" | grep -q 'Machine:.*ARM' || { echo "ERROR: not an ARM binary"; exit 1; }
  "$RE" -h "$SO" | grep -q 'Class:.*ELF32' || { echo "ERROR: not 32-bit"; exit 1; }
  "$RE" -A "$SO" | grep -q 'Tag_ABI_VFP_args: VFP registers' || echo "WARNING: not hard-float (MPC OS is armhf)"
  needed=$("$RE" -d "$SO" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')
  echo "NEEDED: $needed"
  for l in $needed; do case "$l" in libm.so.6|libc.so.6|libpthread.so.0) ;; *) echo "WARNING: unexpected dependency $l";; esac; done
  maxg=$("$RE" -V "$SO" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
  echo "Highest glibc symbol: $maxg  (MPC OS has GLIBC_2.34)"
  [ "$(printf '%s\n' "$maxg" GLIBC_2.34 | sort -V | tail -1)" = GLIBC_2.34 ] || { echo "ERROR: needs $maxg, newer than GLIBC_2.34"; exit 1; }
fi

SKIN="mpc/skin/RadioReady Audio - VST - Da Clip Pads"
[ -f "$SKIN/Plugin Skins/TUI.json" ] || { make native && python3 tools/make_skin.py; }
[ -f "test/demo/Clips/Kits/Demo Kit.dcpkit" ] || make demo
rm -rf "dist/$NAME"; mkdir -p "dist/$NAME/payload/vst" "dist/$NAME/payload/Synths" "dist/$NAME/payload/Clips"
cp -a "$SKIN" "dist/$NAME/payload/Synths/"
cp -a test/demo/Clips/Demo test/demo/Clips/Kits "dist/$NAME/payload/Clips/"
cp "$SO" "dist/$NAME/payload/vst/daclippads.so"
cp mpc/install.sh mpc/uninstall.sh mpc/plugin.xml mpc/plugin_list.awk "dist/$NAME/"
cp mpc/INSTALL.md docs/MIDI.md "dist/$NAME/"
chmod +x "dist/$NAME/"*.sh
( cd "dist/$NAME" && sha256sum INSTALL.md MIDI.md install.sh uninstall.sh plugin.xml plugin_list.awk payload/vst/daclippads.so > SHA256SUMS )
( cd dist && rm -f "$NAME-mpc-armv7.zip" && zip -qr "$NAME-mpc-armv7.zip" "$NAME" )
echo "Built dist/$NAME-mpc-armv7.zip"
