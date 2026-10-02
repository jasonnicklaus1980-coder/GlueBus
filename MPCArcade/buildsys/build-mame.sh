#!/bin/sh
# Cross-build MAME 0.242 for 32-bit ARM hard-float (MPC X): 49 driver files, from the 1978 classics to CPS2 / Neo Geo,
# SDL OSD on our static, patched SDL2 (framebuffer video, evdev input, ALSA loaded at run time).
#   sh build-mame.sh          (re)generate the projects, compile, link
# MAME's generated makefile lists libSDL2.a before the objects (a static library there resolves nothing), so after
# genie regenerates the project this script appends libSDL2.a and the glibc-2.34 compatibility stubs to the link.
B=/home/user/build-mame
cd $B/mame-mame0242 || exit 1
DRV="pacman galaga dkong mw8080bw galaxian centiped asteroid missile williams btime mrdo ladybug timeplt gyruss pooyan
     zaxxon tempest polepos sprint2 firetrk segaorun 1942 1943 ddragon bublbobl mappy contra gauntlet rastan karnov snk
     segas16b sf tmnt simpsons cps1 m72 toaplan2 taito_f2 cps2 neogeo midyunit circus gridlee astrocde exidy carpolo
     polyplay supertnk"
SRC=$(for d in $DRV; do printf "src/mame/drivers/%s.cpp," "$d"; done); SRC="${SRC%,},src/mame/audio/llander.cpp"
arm-linux-gnueabihf-gcc -O2 -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard -c $B/mpc_stubs.c -o $B/mpc_stubs.o || exit 1

build() {
  make -j4 linux "$@" \
  SUBTARGET=mpcarcade SOURCES="$SRC" \
  TARGETOS=linux PLATFORM=arm ARCHITECTURE= PTR64=0 CROSS_BUILD=1 \
  OVERRIDE_CC=arm-linux-gnueabihf-gcc OVERRIDE_CXX=arm-linux-gnueabihf-g++ OVERRIDE_LD=arm-linux-gnueabihf-g++ OVERRIDE_AR=arm-linux-gnueabihf-ar \
  ARCHOPTS="-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard" ARCHOPTS_CXX="-include cstdint" \
  FORCE_DRC_C_BACKEND=1 NOASM=1 PRECOMPILE=0 NOWERROR=1 OPTIMIZE=2 SYMBOLS=0 \
  NO_X11=1 NO_USE_XINPUT=1 NO_OPENGL=1 USE_DISPATCH_GL=0 NO_USE_MIDI=1 NO_USE_PORTAUDIO=1 NO_USE_PULSEAUDIO=1 USE_QTDEBUG=0 \
  SDL_INSTALL_ROOT=$B/sdl-arm PYTHON_EXECUTABLE=python3 \
  LDOPTS="-static-libstdc++ -static-libgcc"
}
fixlink() {
  for f in build/projects/sdl/mamempcarcade/gmake-linux/mpcarcade.make; do
    [ -f "$f" ] || continue
    grep -q "mpc_stubs.o" "$f" || sed -i "s#^\(  LIBS  *+= \$(LDDEPS)\)#\1 $B/sdl-arm/lib/libSDL2.a $B/mpc_stubs.o -ldl#" "$f"
  done
}
build REGENIE=1 || true          # regenerates the projects and compiles; the first link may fail
fixlink
rm -f mpcarcade
build
