# Da Sample Lab build (same toolchain setup as the other GlueBus / RadioReady MPC plugins).  Targets:
#   make arm      -> build/arm/dasamplelab.so + dasamplelab_capture.so (32-bit ARM hard-float, NEON: what the MPC X needs)
#   make native   -> build/native/dasamplelab.so (x86, for the tests and the skin generator)
#   make test     -> native build + test/host_test (66 checks, both plugins); make test-arm runs them on the ARM build under QEMU
#   make skin     -> regenerate the skin and docs/skin-preview-*.png
#   make package  -> dist/DaSampleLab-1.1.0-mpc-armv7.zip
CROSS    ?= arm-linux-gnueabihf-
QEMU     ?= qemu-arm-static
SRC      := src/dasamplelab.cpp
DEPS     := $(SRC) $(wildcard src/*.h src/core/*.h) src/compat.cpp src/exports.map
COMMON   := -std=c++17 -O2 -Wall -Wextra -Wno-format-truncation -Wno-psabi -fPIC -fvisibility=hidden -fvisibility-inlines-hidden
ARMFLAGS := -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -mthumb
# The C++ runtime is linked in statically and hidden (only VSTPluginMain + SL_* are exported), so it can never clash
# with MPC's own; compat.cpp maps the two newer glibc symbols it uses back to glibc 2.34.
ARMLD    := -shared -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,-Bsymbolic \
            -Wl,-z,relro,-z,now -Wl,--no-undefined -lpthread -lm
CAPSRC   := src/capture.cpp
CAPDEPS  := $(CAPSRC) src/vst2.h $(wildcard src/core/*.h) src/compat.cpp src/capture.map
SKIN     := mpc/skin/RadioReady Audio - VST - Da Sample Lab
WORK     ?= /tmp/dasamplelab-test

all: arm
arm: build/arm/dasamplelab.so build/arm/dasamplelab_capture.so
build/arm/dasamplelab.so: $(DEPS)
	@mkdir -p build/arm
	$(CROSS)g++ $(COMMON) $(ARMFLAGS) $(SRC) src/compat.cpp -o $@ -Wl,--version-script=src/exports.map $(ARMLD)
	$(CROSS)strip --strip-unneeded $@
build/arm/dasamplelab_capture.so: $(CAPDEPS)
	@mkdir -p build/arm
	$(CROSS)g++ $(COMMON) $(ARMFLAGS) $(CAPSRC) src/compat.cpp -o $@ -Wl,--version-script=src/capture.map $(ARMLD)
	$(CROSS)strip --strip-unneeded $@
native: build/native/dasamplelab.so build/native/dasamplelab_capture.so
build/native/dasamplelab.so: $(DEPS)
	@mkdir -p build/native
	g++ $(COMMON) -shared $(SRC) -o $@ -lpthread
build/native/dasamplelab_capture.so: $(CAPDEPS)
	@mkdir -p build/native
	g++ $(COMMON) -shared $(CAPSRC) -o $@ -lpthread
build/native/host_test: test/host_test.cpp src/vst2.h
	@mkdir -p build/native
	g++ -std=c++17 -O2 test/host_test.cpp -o $@ -ldl
build/arm/host_test: test/host_test.cpp src/vst2.h
	@mkdir -p build/arm
	$(CROSS)g++ -std=c++17 -O2 $(ARMFLAGS) test/host_test.cpp -o $@ -ldl
test: native build/native/host_test
	rm -rf $(WORK) $(WORK)-internal && ./build/native/host_test build/native/dasamplelab.so $(WORK) build/native/dasamplelab_capture.so
test-arm: arm build/arm/host_test
	rm -rf $(WORK)-arm $(WORK)-arm-internal && $(QEMU) -L /usr/arm-linux-gnueabihf build/arm/host_test build/arm/dasamplelab.so $(WORK)-arm build/arm/dasamplelab_capture.so
build/demo/Demo\ Loop\ 96\ Am.wav: tools/make_demo.py
	@mkdir -p build/demo
	python3 tools/make_demo.py "build/demo/Demo Loop 96 Am.wav"
skin: native build/demo/Demo\ Loop\ 96\ Am.wav
	rm -rf $(WORK)-skin && mkdir -p $(WORK)-skin/Samples && cp "build/demo/Demo Loop 96 Am.wav" $(WORK)-skin/Samples/
	python3 tools/make_skin.py docs $(WORK)-skin
package: arm skin
	./scripts/package.sh
clean:
	rm -rf build dist
.PHONY: all arm native test test-arm skin package clean
