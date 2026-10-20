# Quanta: DSP engine tests (x86) and the armv7 build of the same engine for the MPC.
CXX ?= g++
ARM_CXX ?= arm-linux-gnueabihf-g++
FLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -fno-math-errno

all: build/test_engine

build/test_engine: Tests/test_engine.cpp DSP/QuantaEngine.cpp DSP/QuantaEngine.h DSP/QuantaMath.h
	@mkdir -p build
	$(CXX) $(FLAGS) -o $@ Tests/test_engine.cpp DSP/QuantaEngine.cpp -lm

build/arm/quanta_engine.o: DSP/QuantaEngine.cpp DSP/QuantaEngine.h DSP/QuantaMath.h
	@mkdir -p build/arm
	$(ARM_CXX) $(FLAGS) -mfloat-abi=hard -march=armv7-a -mfpu=neon -c -o $@ DSP/QuantaEngine.cpp

test: build/test_engine build/test_wrapper
	./build/test_engine
	./build/test_wrapper

build/test_wrapper: Tests/test_wrapper.cpp MPC/src/quanta_mpc.cpp MPC/src/compat.cpp DSP/QuantaEngine.cpp DSP/QuantaEngine.h DSP/QuantaMath.h MPC/src/vst2.h
	@mkdir -p build
	$(CXX) $(FLAGS) -fno-exceptions -o $@ Tests/test_wrapper.cpp MPC/src/quanta_mpc.cpp DSP/QuantaEngine.cpp -lm

arm: build/arm/quanta_engine.o

clean:
	rm -rf build

.PHONY: all test arm clean
