# MC-909 import engine: file checks for WAV and MIDI, with tests. Standard C++17, no dependencies.
CXX ?= g++
FLAGS := -std=c++17 -O2 -Wall -Wextra
SRC := src/import_check.cpp src/sampler.cpp

all: build/mc909_import_check

build:
	mkdir -p build

build/mc909_import_check: $(SRC) src/main.cpp src/import_check.h | build
	$(CXX) $(FLAGS) $(SRC) src/main.cpp -o $@

build/test_import: $(SRC) tests/test_import.cpp src/import_check.h | build
	$(CXX) $(FLAGS) $(SRC) tests/test_import.cpp -o $@

build/test_sampler: $(SRC) tests/test_sampler.cpp src/import_check.h src/sampler.h | build
	$(CXX) $(FLAGS) $(SRC) tests/test_sampler.cpp -o $@

test: build/test_import build/test_sampler
	./build/test_import
	./build/test_sampler

clean:
	rm -rf build

.PHONY: all test clean
