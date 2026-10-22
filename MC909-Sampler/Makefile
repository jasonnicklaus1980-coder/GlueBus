# MC-909 import engine: file checks for WAV and MIDI, with tests. Standard C++17, no dependencies.
CXX ?= g++
FLAGS := -std=c++17 -O2 -Wall -Wextra
SRC := src/import_check.cpp src/sampler.cpp src/project.cpp src/padmap.cpp src/waveedit.cpp

all: build/mc909_import_check build/mc909_pad_view

build:
	mkdir -p build

build/mc909_import_check: $(SRC) src/main.cpp src/import_check.h | build
	$(CXX) $(FLAGS) $(SRC) src/main.cpp -o $@

build/test_import: $(SRC) tests/test_import.cpp src/import_check.h | build
	$(CXX) $(FLAGS) $(SRC) tests/test_import.cpp -o $@

build/test_sampler: $(SRC) tests/test_sampler.cpp src/import_check.h src/sampler.h | build
	$(CXX) $(FLAGS) $(SRC) tests/test_sampler.cpp -o $@

build/test_project: $(SRC) tests/test_project.cpp src/project.h src/import_check.h src/sampler.h | build
	$(CXX) $(FLAGS) $(SRC) tests/test_project.cpp -o $@

build/test_extras: $(SRC) tests/test_extras.cpp src/padmap.h src/waveedit.h src/sampler.h | build
	$(CXX) $(FLAGS) $(SRC) tests/test_extras.cpp -o $@

test: build/test_import build/test_sampler build/test_project build/test_extras
	./build/test_import
	./build/test_sampler
	./build/test_project
	./build/test_extras

clean:
	rm -rf build

.PHONY: all test clean

build/mc909_pad_view: $(SRC) src/pad_view.cpp src/project.h | build
	$(CXX) $(FLAGS) $(SRC) src/pad_view.cpp -o $@
