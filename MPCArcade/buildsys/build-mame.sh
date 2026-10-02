#!/bin/sh
# Cross-build MAME 0.242 (six classic drivers) for 32-bit ARM hard-float (MPC X), SDL OSD on our static SDL2.
cd /home/user/build-mame/mame-mame0242
exec make -j4 linux \
  SUBTARGET=mpcarcade \
  SOURCES=src/mame/drivers/pacman.cpp,src/mame/drivers/galaga.cpp,src/mame/drivers/dkong.cpp,src/mame/drivers/mw8080bw.cpp,src/mame/drivers/galaxian.cpp,src/mame/drivers/centiped.cpp \
  TARGETOS=linux PLATFORM=arm ARCHITECTURE= PTR64=0 CROSS_BUILD=1 \
  OVERRIDE_CC=arm-linux-gnueabihf-gcc OVERRIDE_CXX=arm-linux-gnueabihf-g++ OVERRIDE_LD=arm-linux-gnueabihf-g++ OVERRIDE_AR=arm-linux-gnueabihf-ar \
  ARCHOPTS="-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard" ARCHOPTS_CXX="-include cstdint" \
  FORCE_DRC_C_BACKEND=1 NOASM=1 NOWERROR=1 OPTIMIZE=2 SYMBOLS=0 \
  NO_X11=1 NO_USE_XINPUT=1 NO_OPENGL=1 USE_DISPATCH_GL=0 NO_USE_MIDI=1 NO_USE_PORTAUDIO=1 NO_USE_PULSEAUDIO=1 USE_QTDEBUG=0 \
  SDL_INSTALL_ROOT=/home/user/build-mame/sdl-arm PYTHON_EXECUTABLE=python3 \
  LDOPTS="-static-libstdc++ -static-libgcc"
