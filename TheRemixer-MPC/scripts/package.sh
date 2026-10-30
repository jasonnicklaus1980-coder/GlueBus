#!/usr/bin/env bash
# Build theremixer.so for the MPC (32-bit ARM hard-float), verify it, and assemble a release folder + zip:
#   TheRemixer-0.1.0/{install.sh,uninstall.sh,plugin.xml,plugin_list.awk,INSTALL.md,SHA256SUMS,payload/vst/theremixer.so,payload/Synths/<skin>}
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=0.1.0; NAME="TheRemixer-$VERSION"; SO=build/arm/theremixer.so

if command -v arm-linux-gnueabihf-gcc >/dev/null; then make arm
elif command -v docker >/dev/null; then make docker
else echo "Need arm-linux-gnueabihf-gcc (apt install gcc-arm-linux-gnueabihf) or Docker."; exit 1; fi

# ---- verify the binary is what the MPC can load ----
RE="${READELF:-readelf}"
"$RE" -h "$SO" | grep -q 'Machine:.*ARM' || { echo "ERROR: not an ARM binary"; exit 1; }
"$RE" -h "$SO" | grep -q 'Class:.*ELF32' || { echo "ERROR: not 32-bit"; exit 1; }
"$RE" -A "$SO" | grep -q 'Tag_ABI_VFP_args: VFP registers' || { echo "ERROR: not hard-float (MPC OS is armhf)"; exit 1; }
needed=$("$RE" -d "$SO" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')
echo "NEEDED: $needed"
for l in $needed; do case "$l" in libm.so.6|libc.so.6|libpthread.so.0|ld-linux-armhf.so.3) ;; *) echo "ERROR: unexpected dependency $l"; exit 1;; esac; done
maxg=$("$RE" -V "$SO" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
echo "Highest glibc symbol: $maxg  (the MPC has at least GLIBC_2.34, as the JV-880 package needs)"
[ "$(printf '%s\n' "$maxg" GLIBC_2.34 | sort -V | tail -1)" = GLIBC_2.34 ] || { echo "ERROR: needs $maxg, newer than GLIBC_2.34"; exit 1; }

SKIN="mpc/skin/GlueBus - VST - The Remixer"
[ -f "$SKIN/Plugin Skins/TUI.json" ] || { echo "ERROR: skin missing: $SKIN (run skin_tools/make_skin.py)"; exit 1; }
rm -rf "dist/$NAME"; mkdir -p "dist/$NAME/payload/vst" "dist/$NAME/payload/Synths"
cp -a "$SKIN" "dist/$NAME/payload/Synths/"
cp "$SO" "dist/$NAME/payload/vst/theremixer.so"
cp mpc/install.sh mpc/uninstall.sh mpc/plugin.xml mpc/plugin_list.awk "dist/$NAME/"
cp mpc/INSTALL.md "dist/$NAME/INSTALL.md"
chmod +x "dist/$NAME/"*.sh
( cd "dist/$NAME" && find INSTALL.md install.sh uninstall.sh plugin.xml plugin_list.awk payload -type f -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS )
( cd dist && rm -f "$NAME-mpc-armv7.zip" && zip -qr "$NAME-mpc-armv7.zip" "$NAME" )
echo "Built dist/$NAME-mpc-armv7.zip"
