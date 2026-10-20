#!/bin/sh
# Builds the ARM plugin, checks it (libc and loader only, glibc up to 2.34, one export), and packages the installer:
# dist/Quanta-1.0.0-mpc-armv7.zip with SHA256SUMS. Run from the MPC folder.
set -e
cd "$(dirname "$0")/.."
VER=1.0.0
make -s build/quanta.so
needed=$(arm-linux-gnueabihf-readelf -d build/quanta.so | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/')
for l in $needed; do case "$l" in libc.so.6|ld-linux-armhf.so.3) ;; *) echo "ERROR: unexpected dependency $l"; exit 1;; esac; done
hi=$(arm-linux-gnueabihf-readelf -sW build/quanta.so | grep -o 'GLIBC_2\.[0-9]*' | sort -t. -k2 -n -u | tail -1)
case "$hi" in GLIBC_2.[0-9]|GLIBC_2.[12][0-9]|GLIBC_2.3[0-4]) ;; *) echo "ERROR: needs $hi (MPC OS has glibc 2.34)"; exit 1;; esac
ex=$(arm-linux-gnueabihf-readelf -sW build/quanta.so | grep ' GLOBAL DEFAULT ' | grep -v ' UND ' | awk '{print $8}' | sort | tr '\n' ' ')
[ "$ex" = "VSTPluginMain " ] || { echo "ERROR: unexpected exports: $ex"; exit 1; }
arm-linux-gnueabihf-readelf -h build/quanta.so | grep -q 'ARM' || { echo "ERROR: not an ARM binary"; exit 1; }
D="dist/Quanta-$VER-mpc-armv7"
rm -rf "$D" && mkdir -p "$D/payload/vst" "$D/payload/Synths"
cp build/quanta.so "$D/payload/vst/quanta.so"
cp -r "mpc/skin/Quanta Audio - VST - Quanta" "$D/payload/Synths/"
cp mpc/install.sh mpc/uninstall.sh mpc/plugin.xml mpc/plugin_list.awk "$D/"
(cd "$D" && find payload plugin.xml plugin_list.awk -type f -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
(cd dist && rm -f "Quanta-$VER-mpc-armv7.zip" && zip -qr "Quanta-$VER-mpc-armv7.zip" "Quanta-$VER-mpc-armv7")
echo "dist/Quanta-$VER-mpc-armv7.zip"
