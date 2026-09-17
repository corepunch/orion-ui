# Orion Framework Makefile
# Builds Orion library, apps, and tests for Linux, macOS, and Windows

# Keep plain `make` anchored to the complete build even if generated rules
# appear before the all target below.
.DEFAULT_GOAL := all

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -I. -DGL_SILENCE_DEPRECATION -D_DEFAULT_SOURCE \
           -Wno-unused-parameter -Wno-missing-field-initializers
LDFLAGS  =
LIBS     =
UNAME_S ?= $(shell uname -s)
PREFIX  ?= /opt/orion
DESTDIR ?=
INSTALL ?= install
ALLOW_HIGHDPI ?= 1
CFLAGS += -DORION_ALLOW_HIGHDPI=$(ALLOW_HIGHDPI)

# ── Platform ─────────────────────────────────────────────────────────────
# Native Windows and MinGW/MSYS share one configuration; IS_WIN gates the
# remaining differences (gems skipped in all, DLLs copied next to binaries).
COPY_DLLS = :
ifneq (,$(filter Windows_NT,$(OS))$(findstring MINGW,$(UNAME_S))$(findstring MSYS,$(UNAME_S)))
IS_WIN   = 1
LIBS    += -lglew32 -lopengl32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -lsetupapi -lxmllite
LIB_EXT  = dll
LIB_FLAGS = -shared
EXE_EXT  = .exe
IMPLIB_FLAGS    = -Wl,--out-implib,$@.a
LDFLAGS_EXAMPLE = -mwindows
LDFLAGS_TEST    = -mconsole
GEM_NM   = dumpbin //EXPORTS
GEM_SYM  = gem_get_interface
COPY_DLLS = cp -f $(PLATFORM_LIB) $(CORE_LIBS) $(BIN_DIR)/
else ifeq ($(UNAME_S),Darwin)
ARCH     ?= arm64
CFLAGS   += -arch $(ARCH)
LDFLAGS  += -arch $(ARCH)
LIBS     += -framework OpenGL
LIB_EXT  = dylib
LIB_FLAGS = -dynamiclib
RPATH_FLAGS = -Wl,-rpath,@loader_path/../lib
GEM_INSTALL_RPATH_FLAGS = -Wl,-rpath,@loader_path/../..
lib_id_flags = -Wl,-install_name,@rpath/lib$(1).$(LIB_EXT)
GEM_NM   = nm -g
GEM_SYM  = T _gem_get_interface
else ifeq ($(UNAME_S),Linux)
LIBS     += -lGL -lutil
CFLAGS   += -fPIC
LIB_EXT  = so
LIB_FLAGS = -shared -fPIC
RPATH_FLAGS = -Wl,-rpath,'$$ORIGIN/../lib'
GEM_INSTALL_RPATH_FLAGS = -Wl,-rpath,'$$ORIGIN/../..'
GEM_NM   = nm -D
GEM_SYM  = T gem_get_interface
else
$(error Unsupported platform: OS=$(OS) UNAME_S=$(UNAME_S))
endif

# ── Dependencies ─────────────────────────────────────────────────────────
PKG_CONFIG ?= pkg-config
pc = $(shell $(PKG_CONFIG) --$(1) lua5.4 2>/dev/null || $(PKG_CONFIG) --$(1) lua 2>/dev/null)
LIBXML_CFLAGS := $(shell $(PKG_CONFIG) --cflags libxml-2.0 2>/dev/null)
LIBXML_LIBS   := $(shell $(PKG_CONFIG) --libs libxml-2.0 2>/dev/null)

CFLAGS += $(call pc,cflags) $(LIBXML_CFLAGS) -DHAVE_LUA
LIBS   += $(filter-out -lm,$(call pc,libs) $(LIBXML_LIBS)) -lm

# Compile flags for .gem shared libraries
GEM_CFLAGS = $(CFLAGS) -DBUILD_AS_GEM

APPS  = apps
COMPS = components

# Build directories
BUILD_DIR     = build
LIB_DIR       = $(BUILD_DIR)/lib
BIN_DIR       = $(BUILD_DIR)/bin
SHARE_DIR     = $(BUILD_DIR)/share
GEM_DIR       = $(BUILD_DIR)/gem
GENERATED_DIR = $(BUILD_DIR)/generated
TEST_DIR      = tests
BUILD_DIRS    = $(BUILD_DIR) $(LIB_DIR) $(BIN_DIR) $(SHARE_DIR) $(GEM_DIR) $(GENERATED_DIR)

# Platform submodule library
PLATFORM_DIR = platform
PLATFORM_LIB = $(LIB_DIR)/libplatform.$(LIB_EXT)
PLATFORM_SRCS = $(wildcard $(PLATFORM_DIR)/*.h) \
				$(wildcard $(PLATFORM_DIR)/*.c) \
				$(wildcard $(PLATFORM_DIR)/$(if $(IS_WIN),windows,unix)/*) \
				$(wildcard $(PLATFORM_DIR)/$(if $(IS_WIN),windows,macos)/*)

# Core Orion libraries (unity-built; see unity_lib below)
USER_LIB    = $(LIB_DIR)/libuser.$(LIB_EXT)
KERNEL_LIB  = $(LIB_DIR)/libkernel.$(LIB_EXT)
COMMCTL_LIB = $(LIB_DIR)/libcommctl.$(LIB_EXT)
COMMDLG_LIB = $(LIB_DIR)/libcommdlg.$(LIB_EXT)
CORE_LIBS   = $(USER_LIB) $(COMMCTL_LIB) $(COMMDLG_LIB) $(KERNEL_LIB)

# Shared relocatable rpath used exactly once per link command.
PLATFORM_LDFLAGS = -L$(LIB_DIR) -lplatform
CORE_LDLIBS      = -L$(LIB_DIR) -lkernel -lcommctl -lcommdlg -luser

# Tools
ORIONC_BIN   = $(BIN_DIR)/orionc$(EXE_EXT)
TOOLS_SRCS   = $(filter-out tools/orionc.c,$(wildcard tools/*.c))
TOOLS_BINS   = $(patsubst tools/%.c,$(BIN_DIR)/%$(EXE_EXT),$(TOOLS_SRCS)) $(ORIONC_BIN)
TOOLS_CFLAGS = $(CFLAGS) -Wno-unused-function

# Examples are directory names under $(APPS)/, independent of their contents.
EXAMPLES     = $(patsubst $(APPS)/%/,%,$(filter %/,$(wildcard $(APPS)/*/)))
EXAMPLE_BINS = $(patsubst %,$(BIN_DIR)/%$(EXE_EXT),$(EXAMPLES))
GEM_BINS     = $(patsubst %,$(GEM_DIR)/%.gem,$(filter-out shell,$(EXAMPLES)))
COMPONENT_PLUGIN_BINS = $(patsubst $(APPS)/%/$(COMPS),$(LIB_DIR)/%_components.$(LIB_EXT),$(wildcard $(APPS)/*/$(COMPS)))
app_plugin_file = $(if $(wildcard $(call appdir,$(1))/$(COMPS)),$(LIB_DIR)/$(notdir $(call appdir,$(1)))_components.$(LIB_EXT))
app_plugin = $(if $(call app_plugin_file,$(1)),-x none $(call app_plugin_file,$(1)))

# ── Phony apps ───────────────────────────────────────────────────────────
# Phony apps are alternative builds of existing apps with extra compiler
# flags.  Define PHONY_APPS_SRC_<name> + PHONY_APPS_CFLAGS_<name> and add
# <name> to PHONY_APP_NAMES; the source example must exist under $(APPS)/.
PHONY_APPS_SRC_penciltest    = imageeditor
PHONY_APPS_CFLAGS_penciltest = -DIMAGEEDITOR_BW=1 -DIMAGEEDITOR_BW_RETINA
PHONY_APP_NAMES = penciltest
PHONY_APP_BINS  = $(patsubst %,$(BIN_DIR)/%$(EXE_EXT),$(PHONY_APP_NAMES))
PHONY_APP_GEMS  = $(patsubst %,$(GEM_DIR)/%.gem,$(PHONY_APP_NAMES))

# Resolve an output stem (example or phony-app name) to its source dir.
appdir = $(APPS)/$(or $(PHONY_APPS_SRC_$(1)),$(1))

# Unity-built apps need their real sources as prerequisites: without them an
# edited example leaves the binary newer than every listed prereq and make
# silently runs the stale executable.
app_srcs = $(shell find $(call appdir,$(1)) \( -name '*.c' -o -name '*.h' \) ! -path '*/tests/*')
$(foreach n,$(EXAMPLES) $(PHONY_APP_NAMES),$(eval $(BIN_DIR)/$(n)$(EXE_EXT): $(call app_srcs,$(n))))
$(foreach n,$(EXAMPLES) $(PHONY_APP_NAMES),$(eval $(BIN_DIR)/$(n)$(EXE_EXT): $(call app_plugin_file,$(n))))
$(foreach n,$(filter-out shell,$(EXAMPLES)) $(PHONY_APP_NAMES),$(eval $(GEM_DIR)/$(n).gem: $(call app_srcs,$(n))))
$(foreach n,$(filter-out shell,$(EXAMPLES)) $(PHONY_APP_NAMES),$(eval $(GEM_DIR)/$(n).gem: $(call app_plugin_file,$(n))))

GENERATED_HEADERS = $(patsubst $(APPS)/%.orion,$(GENERATED_DIR)/$(APPS)/%.h,$(wildcard $(APPS)/*/*.orion))

.SECONDEXPANSION:

TEST_SRCS = $(sort $(filter-out $(TEST_DIR)/test_env.c,$(wildcard $(TEST_DIR)/*.c)) \
    $(wildcard $(APPS)/*/tests/*.c))
TEST_BINS = $(patsubst %,$(BIN_DIR)/test_%$(EXE_EXT),$(basename $(notdir $(TEST_SRCS))))

# Shell fragment emitting the unity translation unit for example dir $(1):
# every .c outside $(COMPS), main.c last.  ('#' is backslash-escaped for make.)
unity_tu = find $(1) -name '*.c' ! -name main.c ! -path '*/$(COMPS)/*' ! -path '*/tests/*' | sort | sed 's/.*/\#include "&"/'; echo '\#include "$(1)/main.c"'
app_inc  = -I. -I$(call appdir,$*) -I$(call appdir,$*)/$(COMPS) -DSHAREDIR='"../share/$(notdir $(call appdir,$*))"'
app_libs = $(LDFLAGS) $(CORE_LDLIBS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(call app_plugin,$*) $(LIBS)

.PHONY: all install tools platform share library apps plugins gems scener test clean help $(PHONY_APP_NAMES)

all: library apps tools $(if $(IS_WIN),,gems)

# ── Tools ────────────────────────────────────────────────────────────────
tools: $(TOOLS_BINS)
	@echo "All tools built"

$(BIN_DIR)/%$(EXE_EXT): tools/%.c $(CORE_LIBS) | $(BIN_DIR)
	@echo "TOOL    $@"
	@$(CC) $(TOOLS_CFLAGS) -I. -Itools -o $@ $< \
	    $(LDFLAGS) $(CORE_LDLIBS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS)
	@$(COPY_DLLS)

# Self-contained tools that don't need the core libraries.
$(ORIONC_BIN):                          TOOL_LINK = $(LDFLAGS) $(LIBS)
$(ORIONC_BIN): $(BIN_DIR)/%$(EXE_EXT): tools/%.c | $(BIN_DIR)
	@echo "TOOL    $@"
	@$(CC) $(TOOLS_CFLAGS) -I. -Itools -o $@ $< $(TOOL_LINK)

$(GENERATED_DIR)/$(APPS)/%.h: $(APPS)/%.orion $(ORIONC_BIN) | $(GENERATED_DIR)
	@mkdir -p $(dir $@)
	@echo "GEN     $@"
	@$(ORIONC_BIN) --input $< --output $@ --prefix $(notdir $(basename $<))

# ── Platform submodule ───────────────────────────────────────────────────
platform: $(PLATFORM_LIB)

$(PLATFORM_LIB): $(PLATFORM_SRCS) | $(LIB_DIR)
	@echo "PLATFORM"
	@$(MAKE) -B -s -C $(PLATFORM_DIR) OUTDIR=$(abspath $(LIB_DIR)) ARCH="$(ARCH)"

# ── Shared assets ────────────────────────────────────────────────────────
share: | $(SHARE_DIR)
	@mkdir -p $(SHARE_DIR)/orion
	@cp -R share/. $(SHARE_DIR)/orion/
	@for dir in $(APPS)/*/share; do \
	  [ -d "$$dir" ] || continue; \
	  name=$$(basename $$(dirname "$$dir")); \
	  mkdir -p $(SHARE_DIR)/$$name; \
	  cp -R $$dir/. $(SHARE_DIR)/$$name/; \
	done

# ── Core libraries ───────────────────────────────────────────────────────
library: $(CORE_LIBS)

CORE_HEADERS = $(wildcard orion/*.h orion/user/*.h orion/kernel/*.h orion/commctl/*.h orion/commdlg/*.h)

# unity_lib <name> <srcs> <lib-deps> <extra-cflags> <link-libs>
# The sources are #included into a single translation unit fed via stdin.
define unity_lib
$(LIB_DIR)/lib$(1).$(LIB_EXT): $(2) $(3) $(PLATFORM_LIB) $(CORE_HEADERS) | $(LIB_DIR)
	@echo "LIB     $$@"
	@printf '%s\n' $(sort $(2)) | sed 's/.*/\#include "&"/' | \
	    $(CC) $(CFLAGS) $(LIB_FLAGS) $(call lib_id_flags,$(1)) $(4) -x c -o $$@ - \
        $(LDFLAGS) $$(RPATH_FLAGS) $(5) $(PLATFORM_LDFLAGS) $(LIBS) $$(IMPLIB_FLAGS)
endef

USER_SRCS = $(filter-out orion/user/dialog.c orion/user/component_registry.c,$(wildcard orion/user/*.c))

$(eval $(call unity_lib,kernel,$(wildcard orion/kernel/*.c),,,))
$(eval $(call unity_lib,user,$(USER_SRCS),$(COMMDLG_LIB) $(KERNEL_LIB),,-lcommdlg -lkernel))
$(eval $(call unity_lib,commctl,$(wildcard orion/commctl/*.c),$(USER_LIB) $(KERNEL_LIB),-Icomponents,-luser -lkernel))

# commdlg is built by its own makefile as a static library
$(COMMDLG_LIB): $(wildcard orion/commdlg/*.c) $(CORE_HEADERS) | $(LIB_DIR)
	@echo "LIB     $@"
	@$(MAKE) -C orion/commdlg CC="$(CC)" CFLAGS="$(CFLAGS) -I$(abspath .)"
	@cp orion/commdlg/libcommdlg.a $(COMMDLG_LIB)

# ── Examples, plugins, phony apps ────────────────────────────────────────
apps: share $(EXAMPLE_BINS) $(COMPONENT_PLUGIN_BINS) $(PHONY_APP_BINS)

plugins: $(COMPONENT_PLUGIN_BINS)

scener: $(BIN_DIR)/scener$(EXE_EXT)

INSTALL_BINS = $(EXAMPLE_BINS) $(PHONY_APP_BINS) $(TOOLS_BINS)
INSTALL_RUNTIME_LIBS = $(KERNEL_LIB) $(USER_LIB) $(COMMCTL_LIB) $(COMMDLG_LIB) $(PLATFORM_LIB)
INSTALL_GEMS = $(if $(IS_WIN),,$(GEM_BINS) $(PHONY_APP_GEMS))

install: all
	@echo "INSTALL $(DESTDIR)$(PREFIX)"
	@$(INSTALL) -d "$(DESTDIR)$(PREFIX)/bin" "$(DESTDIR)$(PREFIX)/lib" \
	    "$(DESTDIR)$(PREFIX)/include/orion" "$(DESTDIR)$(PREFIX)/include/platform" \
	    "$(DESTDIR)$(PREFIX)/lib/orion/gems" "$(DESTDIR)$(PREFIX)/share" \
	    "$(DESTDIR)$(PREFIX)/share/doc/orion"
	@$(INSTALL) -m 755 $(INSTALL_BINS) "$(DESTDIR)$(PREFIX)/bin/"
	@$(INSTALL) -m 755 $(INSTALL_RUNTIME_LIBS) $(COMPONENT_PLUGIN_BINS) "$(DESTDIR)$(PREFIX)/lib/"
	@find orion -type f -name '*.h' | while IFS= read -r file; do \
	  dest="$(DESTDIR)$(PREFIX)/include/$$file"; \
	  $(INSTALL) -d "$$(dirname "$$dest")"; \
	  $(INSTALL) -m 644 "$$file" "$$dest"; \
	done
	@$(INSTALL) -m 644 platform/platform.h platform/events.h "$(DESTDIR)$(PREFIX)/include/platform/"
	@if [ -n "$(INSTALL_GEMS)" ]; then \
	  $(INSTALL) -m 755 $(INSTALL_GEMS) "$(DESTDIR)$(PREFIX)/lib/orion/gems/"; \
	fi
	@cp -R "$(SHARE_DIR)/." "$(DESTDIR)$(PREFIX)/share/"
	@$(INSTALL) -d "$(DESTDIR)$(PREFIX)/share/scener"
	@cp -R "$(APPS)/scener/scenes" "$(APPS)/scener/prefabs" "$(DESTDIR)$(PREFIX)/share/scener/"
	@find . -maxdepth 1 -type f -name '*.md' | while IFS= read -r file; do \
	  $(INSTALL) -m 644 "$$file" "$(DESTDIR)$(PREFIX)/share/doc/orion/$${file#./}"; \
	done
	@$(INSTALL) -m 644 packaging/README.md "$(DESTDIR)$(PREFIX)/share/doc/orion/package-manager.md"
	@cp -R docs "$(DESTDIR)$(PREFIX)/share/doc/orion/"
	@find apps -type f -name '*.md' | while IFS= read -r file; do \
	  dest="$(DESTDIR)$(PREFIX)/share/doc/orion/$$file"; \
	  $(INSTALL) -d "$$(dirname "$$dest")"; \
	  $(INSTALL) -m 644 "$$file" "$$dest"; \
	done

# Individual phony-app convenience targets (e.g. "make penciltest").
$(foreach a,$(PHONY_APP_NAMES),$(eval $(a): $(BIN_DIR)/$(a)$(EXE_EXT)))

$(LIB_DIR)/%_components.$(LIB_EXT): $$(wildcard $(APPS)/$$*/$(COMPS)/*.c) $(CORE_LIBS) $(GENERATED_HEADERS) | $(LIB_DIR)
	@echo "PLUGIN  $@"
	@$(CC) $(CFLAGS) $(LIB_FLAGS) -I. -I$(APPS)/$* -I$(APPS)/$*/$(COMPS) -o $@ $(wildcard $(APPS)/$*/$(COMPS)/*.c) \
	    $(LDFLAGS) $(CORE_LDLIBS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS)

$(EXAMPLE_BINS) $(PHONY_APP_BINS): $(BIN_DIR)/%$(EXE_EXT): $(CORE_LIBS) $(GENERATED_HEADERS) | $(BIN_DIR) share
	@printf '%-8s%s\n' '$(if $(PHONY_APPS_SRC_$*),PHONY,BIN)' "$@"
	@{ $(call unity_tu,$(call appdir,$*)); } | \
	    $(CC) $(CFLAGS) $(PHONY_APPS_CFLAGS_$*) $(app_inc) -x c -o $@ - \
	    $(LDFLAGS_EXAMPLE) $(app_libs)

# Each .gem is built against the split core libraries so it shares the same
# runtime infrastructure as the shell.  orion/gem.h is force-included at the
# top of the unity build so BUILD_AS_GEM macros (running stub, ui_init and
# ui_shutdown no-ops, etc.) apply to every source file without manual edits.
gems: $(GEM_BINS) $(PHONY_APP_GEMS)
	@echo "OK All .gems built and validated"

$(GEM_BINS) $(PHONY_APP_GEMS): $(GEM_DIR)/%.gem: $(CORE_LIBS) $(GENERATED_HEADERS) | $(GEM_DIR)
	@printf '%-8s%s\n' '$(if $(PHONY_APPS_SRC_$*),GEM(P),GEM)' "$@"
	@{ echo '#include <orion/gem.h>'; $(call unity_tu,$(call appdir,$*)); } | \
	    $(CC) $(GEM_CFLAGS) $(PHONY_APPS_CFLAGS_$*) $(LIB_FLAGS) $(app_inc) -x c -o $@ - \
	    $(app_libs) $(GEM_INSTALL_RPATH_FLAGS)
	@$(GEM_NM) $@ 2>/dev/null | grep -q '$(GEM_SYM)' || { echo 'FAIL missing gem_get_interface'; exit 1; }

# ── Tests ────────────────────────────────────────────────────────────────
test: $(TEST_BINS)
	@echo "Running tests..."
	@$(COPY_DLLS)
	@for t in $(TEST_BINS); do \
	    echo "Running $$t..."; \
	    $$t || exit 1; \
	done
	@echo "All tests passed!"

$(TEST_BINS): $(BIN_DIR)/test_%$(EXE_EXT): $(TEST_SRCS) $(TEST_DIR)/test_env.c $(GENERATED_HEADERS) $(CORE_LIBS) $(COMPONENT_PLUGIN_BINS) | $(BIN_DIR)
	@echo "TEST    $@"
	@src='$(firstword $(filter %/$*.c,$(TEST_SRCS)))'; \
	app_dir=; app=; stem='$*'; \
	case $$src in \
	  $(APPS)/*/tests/*.c) app_dir=$${src#$(APPS)/}; app_dir=$${app_dir%%/*}; app=$$app_dir ;; \
	esac; \
	if [ -z "$$app" ]; then \
	  for cand in "$${stem%_test}" "$${stem%%_*}"; do \
	    if [ -d "$(APPS)/$$cand" ]; then app=$$cand; break; fi; \
	  done; \
	fi; \
	{ \
	  printf '#include "%s"\n' "$$src"; \
	  printf '#include "$(TEST_DIR)/test_env.c"\n'; \
	  if [ -n "$$app_dir" ] && [ -d "$(APPS)/$$app_dir" ]; then \
	    find "$(APPS)/$$app_dir" -name '*.c' ! -name main.c ! -path '*/$(COMPS)/*' ! -path '*/tests/*' | sort | sed 's/.*/#include "&"/'; \
	  fi; \
	  if [ -n "$$app_dir" ] && [ -d "$(APPS)/$$app_dir/tests/support" ]; then \
	    find "$(APPS)/$$app_dir/tests/support" -name '*.c' | sort | sed 's/.*/#include "&"/'; \
	  fi; \
	} | $(CC) $(CFLAGS) -I. -Itests -I$(APPS)/$$app -I$(APPS)/$$app/$(COMPS) -x c -o $@ - \
	    $(LDFLAGS_TEST) $(app_libs)

# ── Directories, clean, help ─────────────────────────────────────────────
$(BUILD_DIRS):
	@mkdir -p $@

clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@$(MAKE) -s -C $(PLATFORM_DIR) OUTDIR=$(abspath $(LIB_DIR)) ARCH="$(ARCH)" clean 2>/dev/null || true

help:
	@echo "Orion UI Framework - Build System"
	@echo ""
	@echo "all       - Build library, apps, gems, and tools"
	@echo "install   - Install the complete Orion suite under PREFIX (default: /opt/orion)"
	@echo "library   - Build shared libraries"
	@echo "apps      - Build applications"
	@echo "scener    - Build the Scener application"
	@echo "gems      - Build all .gem shared libraries"
	@echo "tools     - Build command-line tools"
	@echo "test      - Build and run tests"
	@echo "ipad-all  - Build separate Image Editor and Pencil Test iPad bundles"
	@echo "ipad / ipad-simulator / ipad-run / ipad-deploy / ipad-mac - APP=imageeditor|penciltest (ipad-deploy auto-selects one connected iPad)"
	@echo "list-devices - List paired devices (see packaging/ipad/README.md)"
	@echo "ALLOW_HIGHDPI=0 - Disable high-DPI surfaces (use with -B)"
	@echo "clean     - Remove all build artifacts"
	@echo "help      - Show this help message"
	@echo ""
	@echo "Phony apps (derived builds with extra flags):"
	@$(foreach a,$(PHONY_APP_NAMES),echo "  $a - $(call appdir,$(a)) + $(PHONY_APPS_CFLAGS_$(a))";)
	@echo ""
	@echo "$(LIB_DIR)   - Libraries"
	@echo "$(BIN_DIR)   - Binaries and tests"
	@echo "$(GEM_DIR)   - .gem shared libraries"
	@echo "$(SHARE_DIR) - Shared data assets (icons, etc.)"

# Direct SDK builds, with separate bundle identities for the two editor variants.
APP ?= imageeditor
.PHONY: ipad ipad-simulator ipad-run ipad-deploy ipad-mac ipad-all list-devices
ipad:
	$(MAKE) -f packaging/ipad/build.mk APP=$(APP) SDK=iphoneos app
ipad-simulator:
	$(MAKE) -f packaging/ipad/build.mk APP=$(APP) SDK=iphonesimulator app
ipad-run:
	$(MAKE) -f packaging/ipad/build.mk APP=$(APP) SDK=iphonesimulator run
ipad-deploy:
	$(MAKE) -f packaging/ipad/build.mk APP=$(APP) SDK=iphoneos deploy
ipad-mac:
	$(MAKE) -f packaging/ipad/build.mk APP=$(APP) SDK=iphoneos mac
ipad-all:
	$(MAKE) -f packaging/ipad/build.mk APP=imageeditor SDK=iphoneos app
	$(MAKE) -f packaging/ipad/build.mk APP=penciltest SDK=iphoneos app
list-devices:
	xcrun devicectl list devices
