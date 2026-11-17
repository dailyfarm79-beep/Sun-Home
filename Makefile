CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic

.PHONY: all test test-serial

all: build/sun

TEST_JOBS ?= auto
SUN_SOURCES := src/main.cpp src/lexer.cpp src/parser.cpp src/constant_evaluator.cpp src/expression_codegen.cpp src/codegen.cpp src/modules.cpp src/stdlib_io.cpp src/stdlib_json.cpp
SUN_STDLIB_DIR := $(CURDIR)/stdlib

build/sun: $(SUN_SOURCES) src/compiler.hpp src/constant_evaluator.hpp src/expression_codegen.hpp src/lexer.hpp src/stdlib_io.hpp src/stdlib_json.hpp stdlib/sys.su
	mkdir -p build
	$(CXX) $(CXXFLAGS) -DSUN_STDLIB_DIR=\"$(SUN_STDLIB_DIR)\" $(SUN_SOURCES) -o $@

test: build/sun
	python3 tests/run_tests.py --jobs $(TEST_JOBS)

test-serial: build/sun
	python3 -m unittest discover -s tests -v
