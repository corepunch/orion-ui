# Orion Framework Makefile
# Builds Orion library, examples, and tests for Linux, macOS, and Windows

# Compiler and flags
CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -std=c11 -I. -DGL_SILENCE_DEPRECATION -D_DEFAULT_SOURCE
# silence unused parameter warnings
CFLAGS += -Wno-unused-parameter
LDFLAGS = 
LIBS = -lm

# Platform detection
# Detect Windows first (uname may not exist or may return different values on Windows)
ifeq ($(OS),Windows_NT)
    # Windows specific flags (MinGW/MSYS2)
    LIBS += -lglew32 -lopengl32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -lsetupapi
    # Lua library (MSYS2 provides -llua, not -llua5.4 like Unix platforms) – optional for terminal
    ifeq ($(shell pkg-config --exists lua 2>/dev/null && echo yes || echo no),yes)
        CFLAGS += -DHAVE_LUA
        LIBS += $(shell pkg-config --libs lua)
    else
        $(info NOTE: Lua not found on Windows; terminal will run in command-only mode.)
    endif
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
            LIBS    += $(filter-out -L% -lm,$(LUA_LIBS))
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
        # cglm detection — link against libcglm when installed as a shared library
        # (renderer.c uses cglm for matrix math; falls back to cglm_compat.h when absent).
        CGLM_CFLAGS := $(shell pkg-config --cflags cglm 2>/dev/null)
        CGLM_LIBS   := $(shell pkg-config --libs   cglm 2>/dev/null)
        ifneq ($(CGLM_LIBS),)
            CFLAGS += $(CGLM_CFLAGS)
            LIBS   += $(CGLM_LIBS)
        endif
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

# .gem shared-library build flags (platform-specific)
# Gems are built against liborion.so so that they share the same window
# manager instance as the shell (shared event loop).
ifeq ($(OS),Windows_NT)
    GEM_LFLAGS = $(LIB_FLAGS)
    SHELL_EXTRA_LDFLAGS =
else ifeq ($(UNAME_S),Darwin)
    # -undefined dynamic_lookup lets the gem resolve remaining symbols
    # from the host process at axDynlibOpen() time.
    GEM_LFLAGS = $(LIB_FLAGS) -undefined dynamic_lookup
    SHELL_EXTRA_LDFLAGS =
else
    # Linux: the shell must export its symbols so gem code compiled with
    # -fPIC can resolve any remaining references at load time.
    GEM_LFLAGS = $(LIB_FLAGS)
    SHELL_EXTRA_LDFLAGS = -Wl,--export-dynamic -ldl
endif

# Compile flags for .gem shared libraries
GEM_CFLAGS = $(CFLAGS) -DBUILD_AS_GEM

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

# Shared rpath used exactly once per link command to avoid duplicate-rpath warnings.
RPATH_FLAGS = -Wl,-rpath,$(abspath $(LIB_DIR))

# Link flags for platform library
PLATFORM_LDFLAGS = -L$(LIB_DIR) -lplatform

# Link flags for the Orion shared library.
# All programs (examples, tests, gems, shell) link dynamically against
# liborion.so so they all share the same window manager instance.
ORION_LDFLAGS = -L$(LIB_DIR) -lorion

# Tools directory
TOOLS_SRCS = $(wildcard tools/*.c)
TOOLS_BINS = $(patsubst tools/%.c,$(BIN_DIR)/%$(EXE_EXT),$(TOOLS_SRCS))

# .gem output directory and target list
GEM_DIR  = $(BUILD_DIR)/gem
GEM_BINS = $(GEM_DIR)/imageeditor.gem \
           $(GEM_DIR)/filemanager.gem \
           $(GEM_DIR)/helloworld.gem \
           $(GEM_DIR)/terminal.gem \
           $(GEM_DIR)/formeditor.gem

# Shell binary
SHELL_BIN  = $(BIN_DIR)/orion-shell$(EXE_EXT)
SHELL_SRCS = $(wildcard shell/*.c)

# Example sources - each example lives in its own subdirectory with a main.c.
# Browser is built with a dedicated rule because it needs libxml2 flags.
EXAMPLE_DIRS = $(filter-out examples/browser/main.c,$(wildcard examples/*/main.c))
EXAMPLE_BINS = $(patsubst examples/%/main.c,$(BIN_DIR)/%$(EXE_EXT),$(EXAMPLE_DIRS))

# Detect libxml2 before deciding whether to include the browser target.
LIBXML2_CFLAGS = $(shell pkg-config --cflags libxml-2.0 2>/dev/null)
LIBXML2_LIBS = $(shell pkg-config --libs libxml-2.0 2>/dev/null)
LIBXML2_PREFIX = $(shell brew --prefix libxml2 2>/dev/null)
ifeq ($(strip $(LIBXML2_CFLAGS)),)
ifneq ($(strip $(LIBXML2_PREFIX)),)
LIBXML2_CFLAGS = -I$(LIBXML2_PREFIX)/include/libxml2
endif
endif
ifeq ($(strip $(LIBXML2_LIBS)),)
ifneq ($(strip $(LIBXML2_PREFIX)),)
LIBXML2_LIBS = -L$(LIBXML2_PREFIX)/lib -lxml2
endif
endif

# Include the browser example only when its source exists AND libxml2 is available.
BROWSER_MAIN = examples/browser/main.c
BROWSER_BIN = $(BIN_DIR)/browser$(EXE_EXT)
ifneq ($(wildcard $(BROWSER_MAIN)),)
ifneq ($(strip $(LIBXML2_LIBS)),)
EXTRA_EXAMPLE_BINS = $(BROWSER_BIN)
else
$(info NOTE: libxml2 not found; skipping browser example. Install libxml2 + pkg-config to enable.)
endif
endif

# Gitclient tests require custom build rules because they compile gitclient
# source files alongside the test.  The UI test also needs test_env.c; the
# backend test uses only test_framework.h (header-only).  Both are excluded
# from the generic TEST_SRCS/TEST_BINS so that the explicit rules below are
# used without interference from the pattern rules.
GITCLIENT_TEST_SRCS = $(TEST_DIR)/gitclient_backend_test.c \
                      $(TEST_DIR)/gitclient_ui_test.c
GITCLIENT_TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/test_%$(EXE_EXT),$(GITCLIENT_TEST_SRCS))
GITCLIENT_SRCS_NO_MAIN = $(filter-out examples/gitclient/main.c,$(wildcard examples/gitclient/*.c))

# Test sources (gitclient tests excluded — they use their own build rules)
TEST_SRCS = $(filter-out $(TEST_DIR)/test_env.c $(GITCLIENT_TEST_SRCS),$(wildcard $(TEST_DIR)/*.c))
TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/test_%$(EXE_EXT),$(TEST_SRCS))
TEST_ENV_SRCS = $(filter-out $(TEST_DIR)/test_env.c $(GITCLIENT_TEST_SRCS),$(shell grep -l '"test_env.h"' $(TEST_DIR)/*.c 2>/dev/null))
TEST_ENV_BINS = $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/test_%$(EXE_EXT),$(TEST_ENV_SRCS))

# Default target
.PHONY: all
ifeq ($(OS),Windows_NT)
all: library examples tools
else
all: library examples gems shell tools
endif

.PHONY: tools
tools: $(TOOLS_BINS)
	@echo "All tools built"

fonts: tools
	$(BIN_DIR)/font_atlas fonts/ChiKareGo2.ttf share/ChiKareGo2.png -pixelsize=16 -em -sharp -cellw=16 -cellh=16 -v
	$(BIN_DIR)/font_atlas fonts/FindersKeepers.ttf share/FindersKeepers.png -pixelsize=16 -em -sharp -cellw=8 -cellh=9 -v

# Geneva9.png — standalone target; does not depend on the full Orion shared lib.
# font_atlas.c only needs libc + stb_truetype; no OpenGL required.
# Requires: Python 3 + Pillow (pip install Pillow)
.PHONY: Geneva9
FONT_ATLAS_STANDALONE = /tmp/orion_font_atlas
Geneva9:
	@echo "Building standalone font_atlas..."
	$(CC) $(CFLAGS) -I. -Itools tools/font_atlas.c -lm -o $(FONT_ATLAS_STANDALONE)
	@echo "Generating share/Geneva9.png (Silkscreen text + SmallFont icons)..."
	python3 tools/gen_small_font.py \
		fonts/Silkscreen-Regular.ttf \
		share/SmallFont.png \
		share/Geneva9.png \
		$(FONT_ATLAS_STANDALONE)


$(BIN_DIR)/%$(EXE_EXT): tools/%.c $(SHARED_LIB) | $(BIN_DIR)
	@echo "Building tool: $@"
	$(CC) $(CFLAGS) -I. -Itools -o $@ $< \
		$(LDFLAGS) $(ORION_LDFLAGS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS)
ifeq ($(OS),Windows_NT)
	@cp -f $(LIB_DIR)/libplatform.dll $(BIN_DIR)/
	@cp -f $(LIB_DIR)/liborion.dll $(BIN_DIR)/
endif

# Build the platform submodule shared library
.PHONY: platform
platform: $(PLATFORM_LIB)

$(PLATFORM_LIB): | $(LIB_DIR)
	@echo "Building platform library..."
	$(MAKE) -C $(PLATFORM_DIR) OUTDIR=$(abspath $(LIB_DIR))

# VGA font sheet — copied from the source tree into build/share/orion.
# Place your custom font at share/vga-rom-font-8x16.png and it will be used
# by gitclient at runtime.
VGA_FONT_PNG = $(SHARE_DIR)/orion/vga-rom-font-8x16.png
VGA_FONT_SRC = share/vga-rom-font-8x16.png

$(VGA_FONT_PNG): $(VGA_FONT_SRC) | $(SHARE_DIR)
	@echo "Copying VGA font sheet: $@"
	@mkdir -p $(SHARE_DIR)/orion
	cp $(VGA_FONT_SRC) $@

# Shared data assets — copy per-example resources into build/share/<example>/
# and copy the framework's own icon sheet into build/share/orion/.
.PHONY: share
share: $(VGA_FONT_PNG) | $(SHARE_DIR)
	@mkdir -p $(SHARE_DIR)/orion
	@cp share/icon_sheet_16x16.png $(SHARE_DIR)/orion/
	@cp share/SmallFont.png $(SHARE_DIR)/orion/
	@cp share/ChiKareGo2.png $(SHARE_DIR)/orion/
	@[ ! -f share/Geneva9.png ] || cp share/Geneva9.png $(SHARE_DIR)/orion/
	@for dir in examples/*/; do \
	  name=$$(basename "$$dir"); \
	  assets=$$(find "$$dir" -maxdepth 1 \( -name "*.png" -o -name "*.ttf" -o -name "*.jpg" -o -name "*.jpeg" \) 2>/dev/null); \
	  if [ -n "$$assets" ]; then \
	    mkdir -p $(SHARE_DIR)/$$name; \
	    echo "$$assets" | tr '\n' '\0' | xargs -0 -I{} cp {} $(SHARE_DIR)/$$name/; \
	  fi; \
	  if [ -d "$${dir}share" ]; then \
	    mkdir -p $(SHARE_DIR)/$$name; \
	    find "$${dir}share" -maxdepth 1 \( -name "*.png" -o -name "*.ttf" -o -name "*.jpg" -o -name "*.jpeg" \) 2>/dev/null | \
	      tr '\n' '\0' | xargs -0 -I{} cp {} $(SHARE_DIR)/$$name/ 2>/dev/null || true; \
	  fi; \
	done

# Library targets
.PHONY: library
library: $(STATIC_LIB) $(SHARED_LIB)

$(STATIC_LIB): $(USER_SRCS) $(KERNEL_SRCS) $(COMMCTL_SRCS) $(PLATFORM_LIB) | $(LIB_DIR)
	@echo "Creating static library: $@"
	find user kernel commctl -name "*.c" | sort | sed 's/.*/#include "&"/' | \
		$(CC) $(CFLAGS) -x c -c -o $(BUILD_DIR)/liborion_unity.o - && \
	$(AR) rcs $@ $(BUILD_DIR)/liborion_unity.o && \
	rm -f $(BUILD_DIR)/liborion_unity.o

$(SHARED_LIB): $(USER_SRCS) $(KERNEL_SRCS) $(COMMCTL_SRCS) $(PLATFORM_LIB) | $(LIB_DIR)
	@echo "Creating shared library: $@"
	find user kernel commctl -name "*.c" | sort | sed 's/.*/#include "&"/' | \
		$(CC) $(CFLAGS) $(LIB_FLAGS) -x c -o $@ - $(LDFLAGS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS)

# Examples
.PHONY: examples
examples: share $(EXAMPLE_BINS) $(EXTRA_EXAMPLE_BINS)

# Static unity-build rule for all examples.
# The target list is scoped to $(EXAMPLE_BINS) so this rule never fires for
# test binaries (which share the same $(BIN_DIR)/%$(EXE_EXT) pattern).
# SECONDEXPANSION lets $$(wildcard ...) expand after % is substituted, so
# any *.c change in the example directory triggers a rebuild.
.SECONDEXPANSION:
$(EXAMPLE_BINS): $(BIN_DIR)/%$(EXE_EXT): $$(wildcard examples/%/*.c) $(SHARED_LIB) | $(BIN_DIR)
	@echo "Building example: $@"
	@(find examples/$* -name "*.c" ! -name "main.c" | sort | sed 's/.*/#include "&"/'; \
	 echo '#include "examples/$*/main.c"') | \
		$(CC) $(CFLAGS) -I. -Iexamples/$* -DSHAREDIR='"../share/$*"' -x c -o $@ - \
		$(LDFLAGS) $(LDFLAGS_EXAMPLE) $(ORION_LDFLAGS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS)

# Browser example (MVP) - requires libxml2.
$(BROWSER_BIN): $(wildcard examples/browser/*.c) $(SHARED_LIB) | $(BIN_DIR)
	@echo "Building example: $@"
	@(find examples/browser -name "*.c" ! -name "main.c" | sort | sed 's/.*/#include "&"/'; \
	 echo '#include "examples/browser/main.c"') | \
		$(CC) $(CFLAGS) $(LIBXML2_CFLAGS) -I. -Iexamples/browser -DSHAREDIR='"../share/browser"' -x c -o $@ - \
		$(LDFLAGS) $(LDFLAGS_EXAMPLE) $(ORION_LDFLAGS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBXML2_LIBS) $(LIBS)

# === .gem shared libraries ===
#
# Each .gem is built against liborion.so (the Orion shared library) so that
# it shares the same window manager, window list, and event infrastructure as
# the shell — enabling the single shared event loop described in gem_magic.h.
#
# gem_magic.h is force-included at the top of every gem's unity build so that
# BUILD_AS_GEM macros (running stub, ui_init/shutdown no-ops, etc.) apply to
# every source file in the gem without requiring manual edits to each file.

.PHONY: gems
gems: $(GEM_BINS)
	@echo "OK All .gems built and validated"

# Generic .gem unity-build rule — handles both single and multi-file examples.
# gem_magic.h first; non-main files sorted; main.c last.
$(GEM_DIR)/%.gem: $$(wildcard examples/%/*.c) $(SHARED_LIB) | $(GEM_DIR)
	@echo "Building .gem: $@"
	@(echo '#include "gem_magic.h"'; \
	 find examples/$* -name "*.c" ! -name "main.c" | sort | sed 's/.*/#include "&"/'; \
	 echo '#include "examples/$*/main.c"') | \
		$(CC) $(GEM_CFLAGS) $(GEM_LFLAGS) -I. -Iexamples/$* -DSHAREDIR='"../share/$*"' -x c -o $@ - \
		$(LDFLAGS) $(ORION_LDFLAGS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS)
	@$(MAKE) --no-print-directory validate-gem GEM=$@

# Validate that a .gem exports the required gem_get_interface symbol.
.PHONY: validate-gem
validate-gem:
ifeq ($(OS),Windows_NT)
	@dumpbin //EXPORTS $(GEM) 2>/dev/null | grep -q "gem_get_interface" \
		|| (echo "FAIL missing gem_get_interface" && exit 1)
else ifeq ($(UNAME_S),Darwin)
	@nm -g $(GEM) 2>/dev/null | grep -q "T _gem_get_interface" \
		|| (echo "FAIL missing gem_get_interface" && exit 1)
else
	@nm -D $(GEM) 2>/dev/null | grep -q "T gem_get_interface" \
		|| (echo "FAIL missing gem_get_interface" && exit 1)
endif

$(GEM_DIR):
	mkdir -p $@

# === Orion Shell ===
#
# The shell links against liborion.so (not .a) and exports its symbols with
# -Wl,--export-dynamic (Linux) so that gems whose unresolved references were
# not satisfied by liborion.so can still resolve them from the shell.

.PHONY: shell
shell: $(SHELL_BIN)

$(SHELL_BIN): $(SHELL_SRCS) $(SHARED_LIB) | $(BIN_DIR)
	@echo "Building Orion Shell: $@"
	@(find shell -name "*.c" | sort | sed 's/.*/#include "&"/') | \
		$(CC) $(CFLAGS) -I. -Ishell -x c -o $@ - \
		$(LDFLAGS) $(ORION_LDFLAGS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LDFLAGS_EXAMPLE) $(LIBS) $(SHELL_EXTRA_LDFLAGS)

# Tests
.PHONY: test
test: $(TEST_BINS) $(GITCLIENT_TEST_BINS)
	@echo "Running tests..."
ifeq ($(OS),Windows_NT)
	@cp -f $(LIB_DIR)/libplatform.dll $(BIN_DIR)/
	@cp -f $(LIB_DIR)/liborion.dll $(BIN_DIR)/
endif
	@for test in $(TEST_BINS) $(GITCLIENT_TEST_BINS); do \
		echo "Running $$test..."; \
		$$test || exit 1; \
	done
	@echo "All tests passed!"

# Gitclient backend test — only needs git_backend.c (no UI procs).
$(BIN_DIR)/test_gitclient_backend_test$(EXE_EXT): $(TEST_DIR)/gitclient_backend_test.c examples/gitclient/git_backend.c $(SHARED_LIB) | $(BIN_DIR)
	@echo "Building gitclient backend test: $@"
	$(CC) $(CFLAGS) -I. -Iexamples/gitclient -o $@ \
		$(TEST_DIR)/gitclient_backend_test.c \
		examples/gitclient/git_backend.c \
		$(LDFLAGS) $(LDFLAGS_TEST) $(ORION_LDFLAGS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS)

# Gitclient UI test — needs all gitclient sources except main.c + test_env.c.
$(BIN_DIR)/test_gitclient_ui_test$(EXE_EXT): $(TEST_DIR)/gitclient_ui_test.c $(TEST_DIR)/test_env.c $(GITCLIENT_SRCS_NO_MAIN) $(SHARED_LIB) | $(BIN_DIR)
	@echo "Building gitclient UI test: $@"
	$(CC) $(CFLAGS) -I. -Iexamples/gitclient -o $@ \
		$(TEST_DIR)/gitclient_ui_test.c $(TEST_DIR)/test_env.c \
		$(GITCLIENT_SRCS_NO_MAIN) \
		$(LDFLAGS) $(LDFLAGS_TEST) $(ORION_LDFLAGS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS)

# Build tests that need test_env (auto-detected by include)
$(TEST_ENV_BINS): $(BIN_DIR)/test_%$(EXE_EXT): $(TEST_DIR)/%.c $(SHARED_LIB) | $(BIN_DIR)
	@echo "Building test with environment: $@"
	$(CC) $(CFLAGS) -o $@ $< $(TEST_DIR)/test_env.c $(LDFLAGS) $(LDFLAGS_TEST) $(ORION_LDFLAGS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS)

# Image API test – self-contained, pulls in user/image.c directly (no platform/GL needed)
$(BIN_DIR)/test_image_test$(EXE_EXT): $(TEST_DIR)/image_test.c | $(BIN_DIR)
	@echo "Building test: $@"
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_TEST) -lm

# Generic test build rule (tests without test_env)
$(BIN_DIR)/test_%$(EXE_EXT): $(TEST_DIR)/%.c $(SHARED_LIB) | $(BIN_DIR)
	@echo "Building test: $@"
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDFLAGS_TEST) $(ORION_LDFLAGS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS)

# Directory creation
BUILD_DIRS = $(BUILD_DIR) $(LIB_DIR) $(BIN_DIR) $(SHARE_DIR)

$(BUILD_DIRS):
	mkdir -p $@

# Clean
.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)
	$(MAKE) -C $(PLATFORM_DIR) OUTDIR=$(abspath $(LIB_DIR)) clean 2>/dev/null || true

# Help
.PHONY: help
help:
	@echo "Orion UI Framework - Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all       - Build library, examples, gems, and shell"
	@echo "  library   - Build static and shared libraries"
	@echo "  examples  - Build example applications"
	@echo "  gems      - Build all .gem shared libraries"
	@echo "  shell     - Build the Orion shell"
	@echo "  test      - Build and run tests"
	@echo "  clean     - Remove all build artifacts"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Output directories:"
	@echo "  $(LIB_DIR)  - Libraries"
	@echo "  $(SHARE_DIR) - Shared data assets (icons, etc.)"
