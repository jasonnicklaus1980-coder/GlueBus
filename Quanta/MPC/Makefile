# Quanta for MPC OS: ARM (32-bit hard-float) VST2 plugin, linked against libc only.
ARM_CXX ?= arm-linux-gnueabihf-g++
ARM_STRIP ?= arm-linux-gnueabihf-strip
ENGINE := ../DSP/QuantaEngine.cpp ../DSP/QuantaPresets.cpp
FLAGS := -std=c++17 -O2 -fPIC -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -fno-math-errno -fno-exceptions -fno-rtti \
         -mfloat-abi=hard -march=armv7-a -mfpu=neon

build/quanta.so: src/quanta_mpc.cpp $(ENGINE) ../DSP/QuantaMath.h ../DSP/QuantaPresets.h src/vst2.h src/exports.map src/compat.cpp
	@mkdir -p build
	$(ARM_CXX) $(FLAGS) -shared -o $@ src/quanta_mpc.cpp $(ENGINE) src/compat.cpp \
	  -Wl,--version-script=src/exports.map -Wl,--no-undefined -Wl,-z,relro,-z,now -static-libgcc -static-libstdc++ -fno-stack-protector
	$(ARM_STRIP) --strip-unneeded $@

check: build/quanta.so
	@arm-linux-gnueabihf-readelf -d $< | grep NEEDED
	@arm-linux-gnueabihf-readelf -sW $< | grep " UND " | awk '{print $$8}' | sort -u
	@arm-linux-gnueabihf-readelf -sW $< | grep " GLOBAL DEFAULT .* VSTPluginMain" | wc -l
	@echo "highest glibc symbol: $$(arm-linux-gnueabihf-readelf -sW $< | grep -o 'GLIBC_2\.[0-9]*' | sort -t. -k2 -n -u | tail -1)"

clean:
	rm -rf build

.PHONY: check clean
