# SP1200 build (same toolchain setup as GlueBus).  Targets:
#   make arm      -> build/arm/sp1200.so   (32-bit ARM hard-float, what the MPC needs)  needs arm-linux-gnueabihf-g++
#   make docker   -> same, built in a linux/arm/v7 container (no cross toolchain needed)
#   make test     -> builds a native x86 copy and runs test/host_test (no MPC needed)
CROSS   ?= arm-linux-gnueabihf-
COMMON  := -O2 -Wall -Wextra -std=c++17 -fno-exceptions -fno-rtti -fvisibility=hidden -fPIC -fno-stack-protector -fno-threadsafe-statics -Wno-format-truncation
ARMFLAGS:= -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard -mthumb
# Linked with the C driver (gcc, not g++): no libstdc++/libgcc_s, only libc + libm.
LDFLAGS := -shared -static-libgcc -Wl,-z,relro,-z,now -Wl,--no-undefined
SRC     := src/sp1200.cpp src/vst2.h $(wildcard src/sp/*.h)

all: arm

arm: build/arm/sp1200.so
build/arm/sp1200.so: $(SRC)
	@mkdir -p build/arm
	$(CROSS)gcc -x c++ $(COMMON) $(ARMFLAGS) src/sp1200.cpp -x none $(LDFLAGS) -o $@ -lm
	$(CROSS)strip --strip-unneeded $@
	@file $@ || true

docker:
	docker run --rm --platform linux/arm/v7 -v "$(CURDIR)":/src -w /src ubuntu:22.04 bash -c \
	  "apt-get update -qq && apt-get install -y -qq g++ gcc make file binutils >/dev/null && \
	   make CROSS= ARMFLAGS= arm"

native: build/native/sp1200.so
build/native/sp1200.so: $(SRC)
	@mkdir -p build/native
	gcc -x c++ $(COMMON) src/sp1200.cpp -x none $(LDFLAGS) -o $@ -lm

build/native/host_test: test/host_test.cpp src/vst2.h
	@mkdir -p build/native
	g++ -O1 -Wall -std=c++17 -Isrc test/host_test.cpp -o $@ -ldl -lm

build/native/stage_test: test/stage_test.cpp $(SRC)
	@mkdir -p build/native
	g++ -O2 -Wall -std=c++17 -Isrc test/stage_test.cpp -o $@ -lm

stagetest: build/native/stage_test
	./build/native/stage_test

test: native build/native/host_test build/native/stage_test
	./build/native/stage_test
	./build/native/host_test build/native/sp1200.so

clean:
	rm -rf build dist
.PHONY: all arm docker native test stagetest clean
