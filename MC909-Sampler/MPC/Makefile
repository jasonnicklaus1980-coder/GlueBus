# MC-909 Sampler for MPC OS: ARM (32-bit hard-float) VST2 instrument, linked against libc only.
ARM_CXX ?= arm-linux-gnueabihf-g++
ARM_STRIP ?= arm-linux-gnueabihf-strip
ENGINE := ../src/import_check.cpp ../src/sampler.cpp ../src/project.cpp ../src/padmap.cpp ../src/waveedit.cpp
FLAGS := -std=c++17 -O2 -fPIC -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -fno-math-errno -fno-exceptions -fno-rtti -Wno-psabi \
         -mfloat-abi=hard -march=armv7-a -mfpu=neon

build/mc909_sampler.so: src/sampler_mpc.cpp $(ENGINE) ../src/import_check.h ../src/sampler.h ../src/project.h ../src/padmap.h ../src/waveedit.h src/vst2.h src/exports.map src/compat.cpp
	@mkdir -p build
	$(ARM_CXX) $(FLAGS) -shared -o $@ src/sampler_mpc.cpp $(ENGINE) src/compat.cpp \
	  -Wl,--version-script=src/exports.map -Wl,--no-undefined -Wl,-z,relro,-z,now -static-libgcc -static-libstdc++ -fno-stack-protector \
	  -Wl,--as-needed
	$(ARM_STRIP) --strip-unneeded $@

check: build/mc909_sampler.so
	@arm-linux-gnueabihf-readelf -d $< | grep NEEDED
	@arm-linux-gnueabihf-readelf -sW $< | grep " UND " | awk '{print $$8}' | sort -u
	@arm-linux-gnueabihf-readelf -sW $< | grep " GLOBAL DEFAULT .* VSTPluginMain" | wc -l
	@echo "highest glibc symbol: $$(arm-linux-gnueabihf-readelf -sW $< | grep -o 'GLIBC_2\.[0-9]*' | sort -t. -k2 -n -u | tail -1)"

package: build/mc909_sampler.so
	sh scripts/package.sh

clean:
	rm -rf build dist

.PHONY: check package clean
