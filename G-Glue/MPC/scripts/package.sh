#!/usr/bin/env bash
# Verify the ARM plugin (32-bit ARM, hard-float, libc/libm only, glibc <= 2.34, exactly 3 exported symbols) and build
#   dist/G-Glue-MPC-1.0.0-armv7/{install.sh,uninstall.sh,plugin.xml,plugin_list.awk,INSTALL.md,SHA256SUMS,
#                                payload/vst/gglue.so,payload/Synths/<skin>}  + the zip
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=1.0.0; NAME="G-Glue-MPC-$VERSION-armv7"; SO=build/arm/gglue.so
RE="${READELF:-arm-linux-gnueabihf-readelf}"
"$RE" -h "$SO" | grep -q 'Machine:.*ARM' || { echo "ERROR: not an ARM binary"; exit 1; }
"$RE" -h "$SO" | grep -q 'Class:.*ELF32' || { echo "ERROR: not 32-bit"; exit 1; }
"$RE" -A "$SO" | grep -q 'Tag_ABI_VFP_args: VFP registers' || { echo "ERROR: not hard-float"; exit 1; }
needed=$("$RE" -d "$SO" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')
echo "NEEDED: $needed"
for l in $needed; do case "$l" in libm.so.6|libc.so.6|ld-linux-armhf.so.3) ;; *) echo "ERROR: unexpected dependency $l"; exit 1;; esac; done
maxg=$("$RE" -V "$SO" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
echo "Highest glibc symbol: $maxg  (MPC OS has GLIBC_2.34)"
[ "$(printf '%s\n' "$maxg" GLIBC_2.34 | sort -V | tail -1)" = GLIBC_2.34 ] || { echo "ERROR: needs $maxg"; exit 1; }
exports=$("$RE" -W --dyn-syms "$SO" | awk '$7!="UND" && ($5=="GLOBAL"||$5=="WEAK"){print $8}' | sort | tr '\n' ' ')
echo "Exports: $exports"
[ "$exports" = "GG_ParamCount GG_ParamKey VSTPluginMain " ] || { echo "ERROR: unexpected exported symbols"; exit 1; }
SKIN="mpc/skin/G-Glue Audio - VST - G-Glue Bus Compressor"
[ -f "$SKIN/Plugin Skins/TUI.json" ] || { echo "ERROR: build the skin first (make skin)"; exit 1; }
rm -rf "dist/$NAME"; mkdir -p "dist/$NAME/payload/vst" "dist/$NAME/payload/Synths"
cp -a "$SKIN" "dist/$NAME/payload/Synths/"
cp "$SO" "dist/$NAME/payload/vst/gglue.so"
cp mpc/install.sh mpc/uninstall.sh mpc/plugin.xml mpc/plugin_list.awk mpc/INSTALL.md "dist/$NAME/"
chmod +x "dist/$NAME/"*.sh
( cd "dist/$NAME" && sha256sum INSTALL.md install.sh uninstall.sh plugin.xml plugin_list.awk payload/vst/gglue.so > SHA256SUMS )
( cd dist && rm -f "$NAME.zip" && zip -qr "$NAME.zip" "$NAME" )
echo "Built dist/$NAME.zip"
