# G-Glue for MPC OS (unofficial VST2 route, Gen1 devices). Same toolchain setup as the other GlueBus MPC plugins.
#   make arm      -> build/arm/gglue.so        (32-bit ARM hard-float, NEON: MPC X / Live / One / Key 61 / Force)
#   make native   -> build/native/gglue.so     (x86, for the tests and the skin preview)
#   make test     -> native build + test/host_test;  make test-arm runs the same test on the ARM build under QEMU
#   make skin     -> mpc/skin + docs/skin-preview.png (needs Pillow)
#   make package  -> dist/G-Glue-MPC-1.0.0-armv7.zip
CROSS    ?= arm-linux-gnueabihf-
QEMU     ?= qemu-arm-static
CORE     := ../DSP/GGlueEngine.cpp ../Parameters/GGlueParameters.cpp ../Presets/GGluePresets.cpp ../Presets/FactoryPresets.cpp
SRC      := src/gglue_mpc.cpp $(CORE)
DEPS     := $(SRC) $(wildcard src/*.h ../DSP/*.h ../Parameters/*.h ../Presets/*.h ../GUI/NeedleBallistics.h) src/compat.cpp src/exports.map
COMMON   := -std=c++17 -O2 -Wall -Wextra -Wno-format-truncation -Wno-psabi -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -I..
ARMFLAGS := -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -mthumb
# C++ runtime linked in and hidden (only VSTPluginMain + GG_* exported), so it never clashes with MPC's own;
# compat.cpp maps the newer glibc symbols the runtime asks for back to what MPC OS (glibc 2.34) has.
ARMLD    := -shared -static-libstdc++ -static-libgcc -Wl,--version-script=src/exports.map -Wl,--exclude-libs,ALL -Wl,-Bsymbolic \
            -Wl,-z,relro,-z,now -Wl,--no-undefined -lpthread -lm
WORK     ?= /tmp/gglue-mpc-test

all: arm
arm: build/arm/gglue.so
build/arm/gglue.so: $(DEPS)
	@mkdir -p build/arm
	$(CROSS)g++ $(COMMON) $(ARMFLAGS) $(SRC) src/compat.cpp -o $@ $(ARMLD)
	$(CROSS)strip --strip-unneeded $@
native: build/native/gglue.so
build/native/gglue.so: $(DEPS)
	@mkdir -p build/native
	g++ $(COMMON) -shared $(SRC) -o $@ -lpthread
build/native/host_test: test/host_test.cpp src/vst2.h
	@mkdir -p build/native
	g++ -std=c++17 -O2 test/host_test.cpp -o $@ -ldl
build/arm/host_test: test/host_test.cpp src/vst2.h
	@mkdir -p build/arm
	$(CROSS)g++ -std=c++17 -O2 $(ARMFLAGS) test/host_test.cpp -o $@ -ldl
test: native build/native/host_test
	rm -rf $(WORK) && ./build/native/host_test build/native/gglue.so $(WORK)
test-arm: arm build/arm/host_test
	rm -rf $(WORK)-arm && GGLUE_EMULATED=1 $(QEMU) -L /usr/arm-linux-gnueabihf build/arm/host_test build/arm/gglue.so $(WORK)-arm
skin: native
	python3 tools/make_skin.py docs
package: arm skin
	./scripts/package.sh
clean:
	rm -rf build dist
.PHONY: all arm native test test-arm skin package clean
