# MPC Arcade build.  Targets:
#   make arm      -> build/arm/{arcade-ui,padbridge,romdrop,mpcarcade.so}   (32-bit ARM hard-float, for the MPC X)
#   make native   -> build/native/... (x86 copies for the off-device tests)
#   make test     -> native build + tests/run_tests.sh
#   make package  -> dist/MPCArcade-<version>.zip (needs the MAME binary from buildsys/build-mame.sh)
# SDL: the patched static SDL2 from buildsys (framebuffer video, evdev input). Paths can be overridden.
VERSION   := 1.0.0
CROSS     ?= arm-linux-gnueabihf-
SDL_ARM   ?= /home/user/build-mame/sdl-arm
SDL_NAT   ?= /home/user/build-mame/sdl-native
MAME_ARM  ?= /home/user/build-mame/mame-arm-full
LISTXML   ?= /home/user/build-mame/listxml-0242.xml
ARMFLAGS  := -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard
CFLAGS    := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -Wno-misleading-indentation -std=gnu11
UI_SRC    := src/frontend/main.c src/frontend/gfx.c src/frontend/ui.c src/frontend/data.c
UI_DEP    := $(UI_SRC) $(wildcard src/frontend/*.h) src/common/actions.h
STUBS     := buildsys/mpc_stubs.c

all: arm

# ---- ARM (the MPC)
arm: build/arm/arcade-ui build/arm/padbridge build/arm/romdrop build/arm/mpcarcade.so
build/arm/arcade-ui: $(UI_DEP)
	@mkdir -p build/arm
	$(CROSS)gcc $(CFLAGS) $(ARMFLAGS) -I$(SDL_ARM)/include/SDL2 -D_REENTRANT $(UI_SRC) buildsys/libc_compat.c -o $@ \
	  $(SDL_ARM)/lib/libSDL2.a -lm -ldl -lpthread -static-libgcc
	$(CROSS)strip $@
build/arm/padbridge: src/padbridge.c src/common/actions.h
	@mkdir -p build/arm
	$(CROSS)gcc $(CFLAGS) $(ARMFLAGS) src/padbridge.c buildsys/libc_compat.c -o $@ -lm
	$(CROSS)strip $@
build/arm/romdrop: src/romdrop.c
	@mkdir -p build/arm
	$(CROSS)gcc $(CFLAGS) $(ARMFLAGS) src/romdrop.c buildsys/libc_compat.c -o $@ -lm
	$(CROSS)strip $@
build/arm/mpcarcade.so: src/plugin/mpcarcade.cpp src/plugin/vst2.h
	@mkdir -p build/arm
	$(CROSS)gcc -x c++ -O2 -Wall -Wextra -std=c++17 -fno-exceptions -fno-rtti -fvisibility=hidden -fPIC -fno-stack-protector \
	  -fno-threadsafe-statics $(ARMFLAGS) -mthumb src/plugin/mpcarcade.cpp -x none -shared -static-libgcc -Wl,-z,relro,-z,now \
	  -Wl,--no-undefined -o $@ -lm
	$(CROSS)strip --strip-unneeded $@

# ---- native (tests)
native: build/native/arcade-ui build/native/padbridge build/native/romdrop build/native/mpcarcade.so
build/native/arcade-ui: $(UI_DEP)
	@mkdir -p build/native
	gcc $(CFLAGS) -g -I$(SDL_NAT)/include/SDL2 -D_REENTRANT $(UI_SRC) -o $@ $(SDL_NAT)/lib/libSDL2.a -lm -ldl -lpthread
build/native/padbridge: src/padbridge.c src/common/actions.h
	@mkdir -p build/native
	gcc $(CFLAGS) -g src/padbridge.c -o $@ -lm
build/native/romdrop: src/romdrop.c
	@mkdir -p build/native
	gcc $(CFLAGS) -g src/romdrop.c -o $@
build/native/mpcarcade.so: src/plugin/mpcarcade.cpp src/plugin/vst2.h
	@mkdir -p build/native
	gcc -x c++ -O2 -Wall -Wextra -std=c++17 -fno-exceptions -fno-rtti -fvisibility=hidden -fPIC -fno-threadsafe-statics \
	  src/plugin/mpcarcade.cpp -x none -shared -Wl,--no-undefined -o $@ -lm

# ---- data files generated from this MAME build
build/gamedb.tsv: $(LISTXML) tools/make_gamedb.py data/genres.txt
	python3 tools/make_gamedb.py $(LISTXML) $@
src/frontend/font_data.h: tools/make_font.py
	python3 tools/make_font.py

test: native build/gamedb.tsv
	sh tests/run_tests.sh

package: arm build/gamedb.tsv
	sh scripts/package.sh $(VERSION) $(MAME_ARM)

clean:
	rm -rf build dist
.PHONY: all arm native test package clean
