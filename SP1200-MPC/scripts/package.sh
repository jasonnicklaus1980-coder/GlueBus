#!/usr/bin/env bash
# Build sp1200.so for the MPC (32-bit ARM hard-float), verify it, and assemble a release folder + zip
# in the same layout as the JV-880 package:  SP1200-2.0.0/{install.sh,uninstall.sh,plugin.xml,plugin_list.awk,payload/vst/sp1200.so,...}
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=2.0.0; NAME="SP1200-$VERSION"; SO=build/arm/sp1200.so

if command -v arm-linux-gnueabihf-g++ >/dev/null; then make arm
elif command -v docker >/dev/null; then make docker
# "make docker" builds natively inside an armv7 container
else echo "Need arm-linux-gnueabihf-g++ (apt install g++-arm-linux-gnueabihf) or Docker."; exit 1; fi

# ---- verify the binary is what the MPC can load (compare: JV-880's jv880.so is ARM EABI5, armv7-a, VFPv3-D16, hard-float) ----
RE="${READELF:-readelf}"
if command -v "$RE" >/dev/null; then
  "$RE" -h "$SO" | grep -q 'Machine:.*ARM' || { echo "ERROR: not an ARM binary"; exit 1; }
  "$RE" -h "$SO" | grep -q 'Class:.*ELF32' || { echo "ERROR: not 32-bit"; exit 1; }
  "$RE" -A "$SO" | grep -q 'Tag_ABI_VFP_args: VFP registers' || echo "WARNING: not hard-float (MPC OS is armhf)"
  needed=$("$RE" -d "$SO" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')
  echo "NEEDED: $needed"
  for l in $needed; do case "$l" in libm.so.6|libc.so.6) ;; *) echo "WARNING: unexpected dependency $l (JV-880 only needs libstdc++/libm/libgcc_s/libc)";; esac; done
  maxg=$("$RE" -V "$SO" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
  echo "Highest glibc symbol: $maxg  (JV-880 needs up to GLIBC_2.34, so the device has at least that)"
  [ "$(printf '%s\n' "$maxg" GLIBC_2.34 | sort -V | tail -1)" = GLIBC_2.34 ] || { echo "ERROR: needs $maxg, newer than the JV-880 (GLIBC_2.34): build on an older distro"; exit 1; }
fi

SKIN="mpc/skin/GlueBus - VST - SP1200"
[ -f "$SKIN/Plugin Skins/TUI.json" ] || python3 tools/make_skin.py
rm -rf "dist/$NAME"; mkdir -p "dist/$NAME/payload/vst" "dist/$NAME/payload/Synths"
cp -a "$SKIN" "dist/$NAME/payload/Synths/"
cp "$SO" "dist/$NAME/payload/vst/sp1200.so"
cp mpc/install.sh mpc/uninstall.sh mpc/plugin.xml mpc/plugin_list.awk "dist/$NAME/"
cp mpc/INSTALL.md "dist/$NAME/INSTALL.md"
chmod +x "dist/$NAME/"*.sh
( cd "dist/$NAME" && sha256sum INSTALL.md install.sh uninstall.sh plugin.xml plugin_list.awk payload/vst/sp1200.so > SHA256SUMS )
( cd dist && rm -f "$NAME-mpc-armv7.zip" && zip -qr "$NAME-mpc-armv7.zip" "$NAME" )
echo "Built dist/$NAME-mpc-armv7.zip"
