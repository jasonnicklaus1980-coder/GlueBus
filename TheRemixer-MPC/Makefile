# The Remixer build (same toolchain setup as Da Lufs Plug, GlueBus and SP1200). Targets:
#   make arm      -> build/arm/theremixer.so   (32-bit ARM hard-float, what the MPC needs)  needs arm-linux-gnueabihf-gcc
#   make docker   -> same, built in a linux/arm/v7 container (no cross toolchain needed)
#   make test     -> builds a native x86 copy and runs test/host_test (no MPC needed)
CROSS   ?= arm-linux-gnueabihf-
COMMON  := -O2 -Wall -Wextra -std=c++17 -fno-exceptions -fno-rtti -fvisibility=hidden -fPIC -fno-stack-protector
ARMFLAGS:= -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard -mthumb
# Linked with the C driver (gcc, not g++): no libstdc++/libgcc_s, only libc, libm and pthread.
LDFLAGS := -shared -static-libgcc -pthread -Wl,-z,relro,-z,now -Wl,--no-undefined
CSRC    := src/remixer.cpp
SRC     := $(CSRC) src/vst2.h

all: arm

arm: build/arm/theremixer.so
build/arm/theremixer.so: $(SRC)
	@mkdir -p build/arm
	$(CROSS)gcc -x c++ $(COMMON) $(ARMFLAGS) $(CSRC) -x none $(LDFLAGS) -o $@ -lm
	$(CROSS)strip --strip-unneeded $@
	@file $@ || true

docker:
	docker run --rm --platform linux/arm/v7 -v "$(CURDIR)":/src -w /src ubuntu:22.04 bash -c \
	  "apt-get update -qq && apt-get install -y -qq g++ gcc make file binutils >/dev/null && \
	   make CROSS= ARMFLAGS= arm"

native: build/native/theremixer.so
build/native/theremixer.so: $(SRC)
	@mkdir -p build/native
	gcc -x c++ $(COMMON) $(CSRC) -x none $(LDFLAGS) -o $@ -lm

build/native/host_test: test/host_test.cpp src/vst2.h
	@mkdir -p build/native
	g++ -O2 -Wall -std=c++17 -Isrc test/host_test.cpp -o $@ -ldl -lm

test: native build/native/host_test analysis-test build/native/remixer_analyze
	./build/native/host_test build/native/theremixer.so

# Analysis (chords, key, notes, tempo, WAV reading). Plain C, host-side for now; the plugin build is unchanged.
ANALYSIS_SRC := src/analysis/chordcore.c src/analysis/keys.c src/analysis/notes.c src/analysis/poly.c \
                src/analysis/hpss.c src/analysis/musicout.c src/analysis/padsend.c \
                src/tempo.c src/wav_read.c src/analyze.c src/midi_build.c
build/native/test_analysis: test/test_analysis.c $(ANALYSIS_SRC)
	@mkdir -p build/native
	gcc -std=gnu99 -O2 -Wall -Wextra -Isrc test/test_analysis.c $(ANALYSIS_SRC) -o $@ -lm

build/native/test_midi: test/test_midi.c $(ANALYSIS_SRC)
	@mkdir -p build/native
	gcc -std=gnu99 -O2 -Wall -Wextra -Isrc test/test_midi.c $(ANALYSIS_SRC) -o $@ -lm

analysis-test: build/native/test_analysis build/native/test_midi
	cd $(CURDIR) && ./build/native/test_analysis && ./build/native/test_midi
.PHONY: analysis-test

build/native/remixer_analyze: tools/remixer_analyze.c $(ANALYSIS_SRC)
	@mkdir -p build/native
	gcc -std=gnu99 -O2 -Wall -Wextra -Isrc tools/remixer_analyze.c $(ANALYSIS_SRC) -o $@ -lm

clean:
	rm -rf build dist
.PHONY: all arm docker native test clean
