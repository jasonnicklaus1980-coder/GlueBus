#!/bin/sh
# Builds the ARM instrument, checks it (libc and loader only, glibc up to 2.34, one export), and packages the installer:
# dist/Legacy-Sampler-0.1.0-mpc-armv7.zip with SHA256SUMS. Run from the MPC folder.
set -e
cd "$(dirname "$0")/.."
VER=0.1.0
make -s build/mc909_sampler.so
needed=$(arm-linux-gnueabihf-readelf -d build/mc909_sampler.so | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/')
for l in $needed; do case "$l" in libc.so.6|ld-linux-armhf.so.3) ;; *) echo "ERROR: unexpected dependency $l"; exit 1;; esac; done
hi=$(arm-linux-gnueabihf-readelf -sW build/mc909_sampler.so | grep -o 'GLIBC_2\.[0-9]*' | sort -t. -k2 -n -u | tail -1)
case "$hi" in GLIBC_2.[0-9]|GLIBC_2.[12][0-9]|GLIBC_2.3[0-4]) ;; *) echo "ERROR: needs $hi (MPC OS has glibc 2.34)"; exit 1;; esac
ex=$(arm-linux-gnueabihf-readelf -sW build/mc909_sampler.so | grep ' GLOBAL DEFAULT ' | grep -v ' UND ' | awk '{print $8}' | sort | tr '\n' ' ')
[ "$ex" = "VSTPluginMain " ] || { echo "ERROR: unexpected exports: $ex"; exit 1; }
arm-linux-gnueabihf-readelf -h build/mc909_sampler.so | grep -q 'ARM' || { echo "ERROR: not an ARM binary"; exit 1; }
D="dist/Legacy-Sampler-$VER-mpc-armv7"
rm -rf "$D" && mkdir -p "$D/payload/vst"
cp build/mc909_sampler.so "$D/payload/vst/mc909_sampler.so"
cp mpc/install.sh mpc/uninstall.sh mpc/plugin.xml mpc/plugin_list.awk "$D/"
cp mpc/INSTALL.md "$D/"
(cd "$D" && find payload plugin.xml plugin_list.awk INSTALL.md -type f -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
(cd dist && rm -f "Legacy-Sampler-$VER-mpc-armv7.zip" && zip -qr "Legacy-Sampler-$VER-mpc-armv7.zip" "Legacy-Sampler-$VER-mpc-armv7")
echo "dist/Legacy-Sampler-$VER-mpc-armv7.zip"
