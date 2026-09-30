# EQ7 build (same toolchain setup as GlueBus, SP1200 and EQ7).  Targets:
#   make arm      -> build/arm/eq7.so   (32-bit ARM hard-float, what the MPC needs)  needs arm-linux-gnueabihf-g++
#   make docker   -> same, built in a linux/arm/v7 container (no cross toolchain needed)
#   make test     -> builds a native x86 copy and runs test/host_test (no MPC needed)
CROSS   ?= arm-linux-gnueabihf-
COMMON  := -O2 -Wall -Wextra -std=c++14 -fno-exceptions -fno-rtti -fvisibility=hidden -fPIC -fno-stack-protector
ARMFLAGS:= -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard -mthumb
# Linked with the C driver (gcc, not g++): no libstdc++/libgcc_s, only libc + libm.
LDFLAGS := -shared -static-libgcc -Wl,-z,relro,-z,now -Wl,--no-undefined
SRC     := src/eq7.cpp src/vst2.h

all: arm

arm: build/arm/eq7.so
build/arm/eq7.so: $(SRC)
	@mkdir -p build/arm
	$(CROSS)gcc -x c++ $(COMMON) $(ARMFLAGS) src/eq7.cpp -x none $(LDFLAGS) -o $@ -lm
	$(CROSS)strip --strip-unneeded $@
	@file $@ || true

docker:
	docker run --rm --platform linux/arm/v7 -v "$(CURDIR)":/src -w /src ubuntu:22.04 bash -c \
	  "apt-get update -qq && apt-get install -y -qq g++ gcc make file binutils >/dev/null && \
	   make CROSS= ARMFLAGS= arm"

native: build/native/eq7.so
build/native/eq7.so: $(SRC)
	@mkdir -p build/native
	gcc -x c++ $(COMMON) src/eq7.cpp -x none $(LDFLAGS) -o $@ -lm

build/native/host_test: test/host_test.cpp src/vst2.h
	@mkdir -p build/native
	g++ -O1 -Wall -std=c++14 -Isrc test/host_test.cpp -o $@ -ldl -lm

test: native build/native/host_test
	./build/native/host_test build/native/eq7.so

clean:
	rm -rf build dist
.PHONY: all arm docker native test clean
