# Orion Framework Makefile
# Builds Orion library, examples, and tests for Linux, macOS, and Windows

# Compiler and flags
CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -std=c11 -I. -DGL_SILENCE_DEPRECATION
# silence unused parameter warnings
CFLAGS += -Wno-unused-parameter
LDFLAGS = 
LIBS = -lSDL2 -lm

# Platform detection
# Detect Windows first (uname may not exist or may return different values on Windows)
ifeq ($(OS),Windows_NT)
    # Windows specific flags (MinGW/MSYS2)
    # SDL2 on Windows requires specific library order: -lmingw32 -lSDL2main -lSDL2
    LIBS = -lmingw32 -lSDL2main -lSDL2
    LIBS += -lopengl32 -lglew32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -lsetupapi
    # Lua library (MSYS2 provides -llua, not -llua5.4 like Unix platforms)
    LIBS += -llua
    # For examples: use -mwindows to create Windows GUI application (no console)
    # For tests: use -mconsole to create console application (allows printf output and standard main())
    LDFLAGS_EXAMPLE = -mwindows
    LDFLAGS_TEST = -mconsole
    LIB_EXT = .dll
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
        LIB_FLAGS = -dynamiclib
    else ifeq ($(UNAME_S),Linux)
        # Linux specific flags
        LIBS += -lGL
        LIB_EXT = .so
        LIB_FLAGS = -shared -fPIC
        CFLAGS += -fPIC
    endif
    # Use lua5.4 on Unix-like platforms
    LIBS += -llua5.4
endif

# Build directories
BUILD_DIR = build
LIB_DIR = $(BUILD_DIR)/lib
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
TEST_DIR = tests

# Source files
USER_SRCS = $(wildcard user/*.c)
KERNEL_SRCS = $(wildcard kernel/*.c)
COMMCTL_SRCS = $(wildcard commctl/*.c)
LIB_SRCS = $(USER_SRCS) $(KERNEL_SRCS) $(COMMCTL_SRCS)

# Object files
USER_OBJS = $(patsubst user/%.c,$(OBJ_DIR)/user/%.o,$(USER_SRCS))
KERNEL_OBJS = $(patsubst kernel/%.c,$(OBJ_DIR)/kernel/%.o,$(KERNEL_SRCS))
COMMCTL_OBJS = $(patsubst commctl/%.c,$(OBJ_DIR)/commctl/%.o,$(COMMCTL_SRCS))
LIB_OBJS = $(USER_OBJS) $(KERNEL_OBJS) $(COMMCTL_OBJS)

# Library targets
STATIC_LIB = $(LIB_DIR)/liborion.a
SHARED_LIB = $(LIB_DIR)/liborion$(LIB_EXT)

# Example sources – each example lives in its own subdirectory with a main.c
# Compile directly to binary (no intermediate .o files)
EXAMPLE_DIRS = $(wildcard examples/*/main.c)
EXAMPLE_BINS = $(patsubst examples/%/main.c,$(BIN_DIR)/%$(EXE_EXT),$(EXAMPLE_DIRS))

# Test sources
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/test_%$(EXE_EXT),$(filter-out $(TEST_DIR)/test_env.c,$(TEST_SRCS)))
TEST_ENV_OBJ = $(OBJ_DIR)/test_env.o

# Default target
.PHONY: all
all: library examples

# Library targets
.PHONY: library
library: $(STATIC_LIB) $(SHARED_LIB)

$(STATIC_LIB): $(LIB_OBJS) | $(LIB_DIR)
	@echo "Creating static library: $@"
	$(AR) rcs $@ $^

$(SHARED_LIB): $(LIB_OBJS) | $(LIB_DIR)
	@echo "Creating shared library: $@"
	$(CC) $(LIB_FLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

# Object file compilation rules
$(OBJ_DIR)/user/%.o: user/%.c | $(OBJ_DIR)/user
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/%.o: kernel/%.c | $(OBJ_DIR)/kernel
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/commctl/%.o: commctl/%.c | $(OBJ_DIR)/commctl
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Examples
.PHONY: examples
examples: $(EXAMPLE_BINS)

# Image editor links with libpng for PNG open/save support
$(BIN_DIR)/imageeditor$(EXE_EXT): examples/imageeditor/main.c $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building example: $@"
	$(CC) $(CFLAGS) -Iexamples/imageeditor -o $@ $< $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_EXAMPLE) $(LIBS) -lpng

# Generic rule: compile each example's main.c as a single file directly to binary
$(BIN_DIR)/%$(EXE_EXT): examples/%/main.c $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building example: $@"
	$(CC) $(CFLAGS) -o $@ $< $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_EXAMPLE) $(LIBS)

# Tests
.PHONY: test
test: $(TEST_BINS)
	@echo "Running tests..."
	@for test in $(TEST_BINS); do \
		echo "Running $$test..."; \
		$$test || exit 1; \
	done
	@echo "All tests passed!"

# Build test environment object file
$(TEST_ENV_OBJ): $(TEST_DIR)/test_env.c | $(OBJ_DIR)
	@echo "Compiling test environment: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Build basic tests (without test_env)
$(BIN_DIR)/test_basic_test$(EXE_EXT): $(TEST_DIR)/basic_test.c $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building test: $@"
	$(CC) $(CFLAGS) -o $@ $< $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_TEST) $(LIBS)

# Image editor test (pure-C logic, no test_env, no extra libs needed)
$(BIN_DIR)/test_imageeditor_test$(EXE_EXT): $(TEST_DIR)/imageeditor_test.c | $(BIN_DIR)
	@echo "Building test: $@"
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDFLAGS_TEST)

# Build tests that need test_env
$(BIN_DIR)/test_window_msg_test$(EXE_EXT): $(TEST_DIR)/window_msg_test.c $(TEST_ENV_OBJ) $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building test with environment: $@"
	$(CC) $(CFLAGS) -o $@ $< $(TEST_ENV_OBJ) $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_TEST) $(LIBS)

$(BIN_DIR)/test_button_click_test$(EXE_EXT): $(TEST_DIR)/button_click_test.c $(TEST_ENV_OBJ) $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building test with environment: $@"
	$(CC) $(CFLAGS) -o $@ $< $(TEST_ENV_OBJ) $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_TEST) $(LIBS)

$(BIN_DIR)/test_helloworld_test$(EXE_EXT): $(TEST_DIR)/helloworld_test.c $(TEST_ENV_OBJ) $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building test with environment: $@"
	$(CC) $(CFLAGS) -o $@ $< $(TEST_ENV_OBJ) $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_TEST) $(LIBS)

$(BIN_DIR)/test_terminal_test$(EXE_EXT): $(TEST_DIR)/terminal_test.c $(TEST_ENV_OBJ) $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building test with environment: $@"
	$(CC) $(CFLAGS) -o $@ $< $(TEST_ENV_OBJ) $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_TEST) $(LIBS)

$(BIN_DIR)/test_terminal_test: $(TEST_DIR)/terminal_test.c $(TEST_ENV_OBJ) $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building test with environment: $@"
	$(CC) $(CFLAGS) -o $@ $< $(TEST_ENV_OBJ) $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_TEST) $(LIBS)

$(BIN_DIR)/test_terminal_scrolling_test$(EXE_EXT): $(TEST_DIR)/terminal_scrolling_test.c $(TEST_ENV_OBJ) $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building test with environment: $@"
	$(CC) $(CFLAGS) -o $@ $< $(TEST_ENV_OBJ) $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_TEST) $(LIBS)

$(BIN_DIR)/test_stdout_test$(EXE_EXT): $(TEST_DIR)/stdout_test.c $(TEST_ENV_OBJ) $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building test with environment: $@"
	$(CC) $(CFLAGS) -o $@ $< $(TEST_ENV_OBJ) $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_TEST) $(LIBS)

$(BIN_DIR)/test_comprehensive_stdout_test$(EXE_EXT): $(TEST_DIR)/comprehensive_stdout_test.c $(TEST_ENV_OBJ) $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building test with environment: $@"
	$(CC) $(CFLAGS) -o $@ $< $(TEST_ENV_OBJ) $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_TEST) $(LIBS)

# Generic test build rule (fallback)
$(BIN_DIR)/test_%$(EXE_EXT): $(TEST_DIR)/%.c $(STATIC_LIB) | $(BIN_DIR)
	@echo "Building test: $@"
	$(CC) $(CFLAGS) -o $@ $< $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_TEST) $(LIBS)

# Directory creation
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(LIB_DIR): | $(BUILD_DIR)
	mkdir -p $(LIB_DIR)

$(BIN_DIR): | $(BUILD_DIR)
	mkdir -p $(BIN_DIR)

$(OBJ_DIR): | $(BUILD_DIR)
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/user: | $(BUILD_DIR)
	mkdir -p $(OBJ_DIR)/user

$(OBJ_DIR)/kernel: | $(BUILD_DIR)
	mkdir -p $(OBJ_DIR)/kernel

$(OBJ_DIR)/commctl: | $(BUILD_DIR)
	mkdir -p $(OBJ_DIR)/commctl

# Clean
.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)

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
	@echo "  $(BIN_DIR)  - Executables (examples and tests)"
	@echo "  $(OBJ_DIR)  - Object files"
