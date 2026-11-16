CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic

.PHONY: all test

all: build/sun

SUN_SOURCES := src/main.cpp src/lexer.cpp src/parser.cpp src/constant_evaluator.cpp src/codegen.cpp src/modules.cpp src/stdlib_io.cpp src/stdlib_json.cpp
SUN_STDLIB_DIR := $(CURDIR)/stdlib

build/sun: $(SUN_SOURCES) src/compiler.hpp src/constant_evaluator.hpp src/lexer.hpp src/stdlib_io.hpp src/stdlib_json.hpp stdlib/sys.su
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DSUN_STDLIB_DIR=\"$(SUN_STDLIB_DIR)\" $(SUN_SOURCES) -o $@

test: build/sun
	python3 -m unittest discover -s tests -v
