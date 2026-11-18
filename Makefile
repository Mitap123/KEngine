# K Engine — bootstrap build (GNU make + a C++20 compiler).
#
# This is the build route VERIFIED in the current development environment,
# which has no CMake/Ninja (see BUILDING.md, DEVELOPMENT_STATUS.md). CMake
# (CMakeLists.txt) is the intended primary build once it can be run. Both
# build the same sources by glob, so they cannot drift on file lists.
#
#   make                      build k and k_unit_tests (Debug)
#   make check                build, then run unit tests + CLI tests
#   make BUILD_TYPE=Release   optimized build in a separate directory
#   make SANITIZE=1 check     AddressSanitizer + UBSan build and test run
#   make STRICT=1             treat warnings as errors
#   make clean                remove Build/

CXX        ?= g++
BUILD_TYPE ?= Debug
SANITIZE   ?= 0
STRICT     ?= 0

CXXSTD   := -std=c++20
WARN     := -Wall -Wextra -Wpedantic -Wshadow -Wconversion
ifeq ($(STRICT),1)
WARN += -Werror
endif

ifeq ($(BUILD_TYPE),Release)
OPT := -O2 -DNDEBUG
else ifeq ($(BUILD_TYPE),Debug)
OPT := -O0 -g
else
$(error BUILD_TYPE must be Debug or Release)
endif

SAN :=
CONFIG := $(shell echo $(BUILD_TYPE) | tr 'A-Z' 'a-z')
ifeq ($(SANITIZE),1)
SAN := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
CONFIG := $(CONFIG)-asan
endif

BUILD_DIR := Build/make-$(CONFIG)
CXXFLAGS_ALL := $(CXXSTD) $(WARN) $(OPT) $(SAN) -I. -MMD -MP $(CXXFLAGS)
LDFLAGS_ALL  := $(SAN) $(LDFLAGS)

ENGINE_SRC := $(wildcard Engine/*/*.cpp)
CLI_SRC    := Tools/CLI/main.cpp
TEST_SRC   := $(wildcard Tests/Unit/*.cpp)

ENGINE_OBJ := $(ENGINE_SRC:%.cpp=$(BUILD_DIR)/%.o)
CLI_OBJ    := $(CLI_SRC:%.cpp=$(BUILD_DIR)/%.o)
TEST_OBJ   := $(TEST_SRC:%.cpp=$(BUILD_DIR)/%.o)

ENGINE_LIB := $(BUILD_DIR)/libkengine_core.a
CLI_BIN    := $(BUILD_DIR)/k
TEST_BIN   := $(BUILD_DIR)/k_unit_tests

.PHONY: all check test cli-test clean help
all: $(CLI_BIN) $(TEST_BIN)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_ALL) -c $< -o $@

$(ENGINE_LIB): $(ENGINE_OBJ)
	@rm -f $@
	ar rcs $@ $^

$(CLI_BIN): $(CLI_OBJ) $(ENGINE_LIB)
	$(CXX) $(CLI_OBJ) $(ENGINE_LIB) $(LDFLAGS_ALL) -o $@

$(TEST_BIN): $(TEST_OBJ) $(ENGINE_LIB)
	$(CXX) $(TEST_OBJ) $(ENGINE_LIB) $(LDFLAGS_ALL) -o $@

test: $(TEST_BIN)
	$(TEST_BIN)

cli-test: $(CLI_BIN)
	KENGINE_CLI=$(abspath $(CLI_BIN)) python3 -m unittest discover -s Tests/CLI -v

check: test cli-test

clean:
	rm -rf Build

help:
	@sed -n '2,16p' Makefile

-include $(ENGINE_OBJ:.o=.d) $(CLI_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
