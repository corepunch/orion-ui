# Orion Framework Makefile
# Builds Orion library, examples, and tests for Linux, macOS, and Windows

# Compiler and flags
CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -std=c11 -I. -DGL_SILENCE_DEPRECATION
# silence unused parameter warnings
CFLAGS += -Wno-unused-parameter
LDFLAGS = 
LIBS = -lm

# Platform detection
# Detect Windows first (uname may not exist or may return different values on Windows)
ifeq ($(OS),Windows_NT)
    # Windows specific flags (MinGW/MSYS2)
    LIBS += -lglew32 -lopengl32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -lsetupapi
    # Lua library (MSYS2 provides -llua, not -llua5.4 like Unix platforms)
    CFLAGS += -DHAVE_LUA
    LIBS += -llua
    # For examples: use -mwindows to create Windows GUI application (no console)
    # For tests: use -mconsole to create console application (allows printf output and standard main())
    LDFLAGS_EXAMPLE = -mwindows
    LDFLAGS_TEST = -mconsole
    LIB_EXT = .dll
    PLATFORM_LIB_EXT = dll
    LIB_FLAGS = -shared
    EXE_EXT = .exe
else
    UNAME_S := $(shell uname -s)
    EXE_EXT =
    # On Unix-like platforms, no special flags needed for examples vs tests
    LDFLAGS_EXAMPLE =
    LDFLAGS_TEST =
    ifeq ($(UNAME_S),Darwin)
        # macOS specific flags
        CFLAGS += -I/opt/homebrew/include -I/usr/local/include
        LDFLAGS += -L/opt/homebrew/lib -L/usr/local/lib
        LIBS += -framework OpenGL
        LIB_EXT = .dylib
        PLATFORM_LIB_EXT = dylib
        LIB_FLAGS = -dynamiclib
        # Lua on macOS may be keg-only (headers not symlinked into /opt/homebrew/include).
        LUA_CFLAGS := $(shell pkg-config --cflags lua5.4 2>/dev/null || pkg-config --cflags lua 2>/dev/null)
        LUA_LIBS   := $(shell pkg-config --libs   lua5.4 2>/dev/null || pkg-config --libs   lua 2>/dev/null)
        ifeq ($(LUA_CFLAGS),)
            LUA_PREFIX := $(shell brew --prefix lua@5.4 2>/dev/null || \
                                   brew --prefix lua    2>/dev/null || echo "")
            ifneq ($(LUA_PREFIX),)
                LUA_CFLAGS := -I$(LUA_PREFIX)/include
                LUA_LIBS   := -L$(LUA_PREFIX)/lib -llua5.4
            endif
        endif
        ifneq ($(LUA_CFLAGS),)
            CFLAGS  += -DHAVE_LUA $(LUA_CFLAGS)
            LDFLAGS += $(filter -L%,$(LUA_LIBS))
            LIBS    += $(filter-out -L%,$(LUA_LIBS))
        else
            $(info NOTE: Lua not found; building without Lua scripting. Install Lua via Homebrew to enable.)
        endif
    else ifeq ($(UNAME_S),Linux)
        # Linux specific flags
        LIBS += -lGL
        LIB_EXT = .so
        PLATFORM_LIB_EXT = so
        LIB_FLAGS = -shared -fPIC
        CFLAGS += -fPIC
        # Lua detection on Linux via pkg-config (lua5.4-dev / liblua5.4-dev)
        LUA_CFLAGS := $(shell pkg-config --cflags lua5.4 2>/dev/null || pkg-config --cflags lua 2>/dev/null)
        LUA_LIBS   := $(shell pkg-config --libs   lua5.4 2>/dev/null || pkg-config --libs   lua 2>/dev/null)
        ifneq ($(LUA_CFLAGS),)
            CFLAGS += -DHAVE_LUA $(LUA_CFLAGS)
            LIBS   += $(LUA_LIBS)
        else
            $(info NOTE: Lua not found; building without Lua scripting. Install lua5.4-dev to enable.)
        endif
    endif
endif

# Build directories
BUILD_DIR = build
LIB_DIR = $(BUILD_DIR)/lib
BIN_DIR = $(BUILD_DIR)/bin
SHARE_DIR = $(BUILD_DIR)/share
TEST_DIR = tests

# Platform submodule library
PLATFORM_DIR = platform
PLATFORM_LIB = $(LIB_DIR)/libplatform.$(PLATFORM_LIB_EXT)

# Source files
USER_SRCS = $(wildcard user/*.c)
KERNEL_SRCS = $(wildcard kernel/*.c)
COMMCTL_SRCS = $(wildcard commctl/*.c)

# Library targets
STATIC_LIB = $(LIB_DIR)/liborion.a
SHARED_LIB = $(LIB_DIR)/liborion$(LIB_EXT)

# Link flags for platform library
PLATFORM_LDFLAGS = -L$(LIB_DIR) -lplatform -Wl,-rpath,$(abspath $(LIB_DIR))

# Example sources – each example lives in its own subdirectory with a main.c
# Compile directly to binary (no intermediate .o files)
EXAMPLE_DIRS = $(wildcard examples/*/main.c)
EXAMPLE_BINS = $(patsubst examples/%/main.c,$(BIN_DIR)/%$(EXE_EXT),$(EXAMPLE_DIRS))

# Test sources
TEST_SRCS = $(filter-out $(TEST_DIR)/test_env.c,$(wildcard $(TEST_DIR)/*.c))
TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/test_%$(EXE_EXT),$(TEST_SRCS))
TEST_ENV_SRCS = $(filter-out $(TEST_DIR)/test_env.c,$(shell grep -l '"test_env.h"' $(TEST_DIR)/*.c 2>/dev/null))
TEST_ENV_BINS = $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/test_%$(EXE_EXT),$(TEST_ENV_SRCS))

# Default target
.PHONY: all
all: library examples

# Build the platform submodule shared library
.PHONY: platform
platform: $(PLATFORM_LIB)

$(PLATFORM_LIB): | $(LIB_DIR)
	@echo "Building platform library..."
	$(MAKE) -C $(PLATFORM_DIR) OUTDIR=$(abspath $(LIB_DIR)) LIB_EXT=$(PLATFORM_LIB_EXT)

# Shared data assets
.PHONY: share
share: | $(SHARE_DIR)
	@echo "Copying shared data assets..."
	cp examples/imageeditor/tools.png $(SHARE_DIR)/tools.png
	cp examples/imageeditor/Inconsolata-Regular.ttf $(SHARE_DIR)/Inconsolata-Regular.ttf

# Library targets
.PHONY: library
library: $(STATIC_LIB) $(SHARED_LIB)

$(STATIC_LIB): $(USER_SRCS) $(KERNEL_SRCS) $(COMMCTL_SRCS) $(PLATFORM_LIB) | $(LIB_DIR)
	@echo "Creating static library: $@"
	tmpobj=$$(mktemp /tmp/liborion_XXXXXX.o) && \
	find user kernel commctl -name "*.c" | sort | sed 's/.*/#include "&"/' | \
		$(CC) $(CFLAGS) -x c -c -o $$tmpobj - && \
	$(AR) rcs $@ $$tmpobj && \
	rm $$tmpobj

$(SHARED_LIB): $(USER_SRCS) $(KERNEL_SRCS) $(COMMCTL_SRCS) $(PLATFORM_LIB) | $(LIB_DIR)
	@echo "Creating shared library: $@"
	find user kernel commctl -name "*.c" | sort | sed 's/.*/#include "&"/' | \
		$(CC) $(CFLAGS) $(LIB_FLAGS) -x c -o $@ - $(LDFLAGS) $(PLATFORM_LDFLAGS) $(LIBS)

# Examples
.PHONY: examples
examples: share $(EXAMPLE_BINS)

# Image editor links against the Orion library (PNG I/O via stb_image).
# main.c is appended last so that all sub-module symbols (e.g. kMenus, win procs)
# are defined before main.c's application code references them.
$(BIN_DIR)/imageeditor$(EXE_EXT): $(wildcard examples/imageeditor/*.c) $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building example: $@"
	(find examples/imageeditor -name "*.c" ! -name "main.c" | sort | sed 's/.*/#include "&"/'; \
	 echo '#include "examples/imageeditor/main.c"') | \
		$(CC) $(CFLAGS) -Iexamples/imageeditor -x c -o $@ - -x none $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_EXAMPLE) $(PLATFORM_LDFLAGS) $(LIBS)

# Generic rule: compile each example's main.c as a single file directly to binary
$(BIN_DIR)/%$(EXE_EXT): examples/%/main.c $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building example: $@"
	$(CC) $(CFLAGS) -o $@ $< $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_EXAMPLE) $(PLATFORM_LDFLAGS) $(LIBS)

# Tests
.PHONY: test
test: $(TEST_BINS)
	@echo "Running tests..."
	@for test in $(TEST_BINS); do \
		echo "Running $$test..."; \
		$$test || exit 1; \
	done
	@echo "All tests passed!"

# Build tests that need test_env (auto-detected by include)
$(TEST_ENV_BINS): $(BIN_DIR)/test_%$(EXE_EXT): $(TEST_DIR)/%.c $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building test with environment: $@"
	$(CC) $(CFLAGS) -o $@ $< $(TEST_DIR)/test_env.c $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_TEST) $(PLATFORM_LDFLAGS) $(LIBS)

# Image API test – self-contained, pulls in user/image.c directly (no platform/GL needed)
$(BIN_DIR)/test_image_test$(EXE_EXT): $(TEST_DIR)/image_test.c | $(BIN_DIR)
	@echo "Building test: $@"
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_TEST) -lm

# Generic test build rule (tests without test_env)
$(BIN_DIR)/test_%$(EXE_EXT): $(TEST_DIR)/%.c $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building test: $@"
	$(CC) $(CFLAGS) -o $@ $< $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_TEST) $(PLATFORM_LDFLAGS) $(LIBS)

# Directory creation
BUILD_DIRS = $(BUILD_DIR) $(LIB_DIR) $(BIN_DIR) $(SHARE_DIR)

$(BUILD_DIRS):
	mkdir -p $@

# Clean
.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)
	$(MAKE) -C $(PLATFORM_DIR) OUTDIR=$(abspath $(LIB_DIR)) LIB_EXT=$(PLATFORM_LIB_EXT) clean 2>/dev/null || true

# Help
.PHONY: help
help:
	@echo "Goldie UI Framework - Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all       - Build library and examples (default)"
	@echo "  library   - Build static and shared libraries"
	@echo "  examples  - Build example applications"
	@echo "  test      - Build and run tests"
	@echo "  clean     - Remove all build artifacts"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Output directories:"
	@echo "  $(LIB_DIR)  - Libraries"
	@echo "  $(SHARE_DIR) - Shared data assets (icons, etc.)"
