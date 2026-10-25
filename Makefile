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
# Optimisation is opt-in: `make clean && make scener OPT=-O2` renders reels about 3x faster.
OPT ?=
CFLAGS += $(OPT)

# ── Platform ─────────────────────────────────────────────────────────────
# Native Windows and MinGW/MSYS share one configuration; IS_WIN gates the
# remaining differences (gems skipped in all, DLLs copied next to binaries).
COPY_DLLS = :
ifneq (,$(filter Windows_NT,$(OS))$(findstring MINGW,$(UNAME_S))$(findstring MSYS,$(UNAME_S)))
IS_WIN   = 1
# MinGW's math.h declares M_PI under -std=c11 only with _USE_MATH_DEFINES.
CFLAGS  += -D_USE_MATH_DEFINES
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
LIBS     += -framework OpenGL -framework AVFoundation -framework AudioToolbox
KERNEL_INPUT_OBJ = $(BUILD_DIR)/audio_input_apple.o
# Scener encodes reel videos with VideoToolbox.
SCENER_LIBS = -framework VideoToolbox -framework CoreMedia -framework CoreVideo -framework CoreFoundation
LIB_EXT  = dylib
LIB_FLAGS = -dynamiclib
RPATH_FLAGS = -Wl,-rpath,@loader_path/../lib
GEM_INSTALL_RPATH_FLAGS = -Wl,-rpath,@loader_path/../..
lib_id_flags = -Wl,-install_name,@rpath/lib$(1).$(LIB_EXT)
component_id_flags = -Wl,-install_name,@rpath/$(notdir $@)
GEM_NM   = nm -g
GEM_SYM  = T _gem_get_interface
else ifeq ($(UNAME_S),Linux)
LIBS     += -lGL -lutil
LDFLAGS_TEST += -lEGL
CFLAGS   += -fPIC
LIB_EXT  = so
# Like dylibs and DLLs, a library must resolve every symbol it uses at link time.
LIB_FLAGS = -shared -fPIC -Wl,--no-undefined
RPATH_FLAGS = -Wl,-rpath,'$$ORIGIN/../lib'
GEM_INSTALL_RPATH_FLAGS = -Wl,-rpath,'$$ORIGIN/../..'
component_id_flags = -Wl,-soname,$(notdir $@)
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
SCRIPT_LIB  = $(LIB_DIR)/libscript.$(LIB_EXT)
CORE_LIBS   = $(USER_LIB) $(COMMCTL_LIB) $(COMMDLG_LIB) $(KERNEL_LIB) $(SCRIPT_LIB)

# Shared relocatable rpath used exactly once per link command.
PLATFORM_LDFLAGS = -L$(LIB_DIR) -lplatform
CORE_LDLIBS      = -L$(LIB_DIR) -lscript -lkernel -lcommctl -lcommdlg -luser

# Tools
ORIONC_BIN   = $(BIN_DIR)/orionc$(EXE_EXT)
CGEN_BIN     = $(BIN_DIR)/cgen$(EXE_EXT)
TOOLS_SRCS   = $(filter-out tools/orionc.c tools/cgen.c tools/penciltest_video.c,$(wildcard tools/*.c))
TOOLS_BINS   = $(patsubst tools/%.c,$(BIN_DIR)/%$(EXE_EXT),$(TOOLS_SRCS)) $(ORIONC_BIN) $(CGEN_BIN) $(if $(IS_WIN),,$(BIN_DIR)/penciltest-video)
TOOLS_CFLAGS = $(CFLAGS) -Wno-unused-function
PENCILTEST_VIDEO_CFLAGS = $(shell $(PKG_CONFIG) --cflags libpng 2>/dev/null)
PENCILTEST_VIDEO_LIBS   = $(shell $(PKG_CONFIG) --libs libpng 2>/dev/null)

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

# Per-app theme for standalone binaries only. Gems never receive this flag —
# get_theme() is already the shell's live theme (and its palette).
# Values: classic, modern (dark, omit / default), light, navy, amber.
# Override per build: make imageeditor THEME_imageeditor=light
THEME_ledger ?= amber
app_theme_cflags = $(if $(filter-out modern,$(THEME_$(1))),-DORION_THEME=$(THEME_$(1)))
define check_app_theme
$(if $(THEME_$(1)),$(if $(filter classic modern light navy amber,$(THEME_$(1))),,$(error unknown THEME_$(1)=$(THEME_$(1)) (want classic, modern, light, navy, or amber))))
endef
$(foreach n,$(EXAMPLES) $(PHONY_APP_NAMES),$(eval $(call check_app_theme,$(n))))
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

# Lua bindings declared in .cgen files (tools/cgen.c); the file implementing
# a module includes its generated <stem>_lua.h. Framework modules also get a
# generated reference in docs/lua/.
CGEN_SRCS      = $(wildcard orion/*/*.cgen) $(wildcard $(APPS)/*/*.cgen)
CGEN_HEADERS   = $(patsubst %.cgen,$(GENERATED_DIR)/%_lua.h,$(CGEN_SRCS))
CGEN_DOC_SRCS  = $(wildcard orion/*/*.cgen)
GENERATED_HEADERS += $(CGEN_HEADERS)

.SECONDEXPANSION:

TEST_SRCS = $(sort $(filter-out $(TEST_DIR)/test_env.c,$(wildcard $(TEST_DIR)/*.c)) \
    $(wildcard $(APPS)/*/tests/*.c))
ifeq ($(IS_WIN),1)
TEST_SRCS := $(filter-out $(APPS)/imageeditor/tests/penciltest_video_test.c,$(TEST_SRCS))
endif
TEST_BINS = $(patsubst %,$(BIN_DIR)/test_%$(EXE_EXT),$(basename $(notdir $(TEST_SRCS))))

# Tests unity-include app implementations; edits must rebuild their executables.
$(foreach n,$(EXAMPLES),$(foreach t,$(wildcard $(APPS)/$(n)/tests/*.c),$(eval $(BIN_DIR)/test_$(basename $(notdir $(t)))$(EXE_EXT): $(call app_srcs,$(n)) $(wildcard $(APPS)/$(n)/tests/*.h))))
$(BIN_DIR)/test_groove_waveform_cache_test$(EXE_EXT): $(APPS)/groove/waveform_cache.c $(APPS)/groove/groove.h
$(BIN_DIR)/test_groove_synth_test$(EXE_EXT): $(APPS)/groove/synth.c $(wildcard $(APPS)/groove/synth*.h) $(APPS)/groove/third_party/tsf.h $(APPS)/groove/groove.h

# Shell fragment emitting the unity translation unit for example dir $(1):
# every .c outside $(COMPS), main.c last.  ('#' is backslash-escaped for make.)
unity_tu = find $(1) -name '*.c' ! -name main.c ! -path '*/$(COMPS)/*' ! -path '*/tests/*' ! -path '*/reel/*' | sort | sed 's/.*/\#include "&"/'; echo '\#include "$(1)/main.c"'
app_inc  = -I. -I$(call appdir,$*) -I$(call appdir,$*)/$(COMPS) -DSHAREDIR='"../share/$(notdir $(call appdir,$*))"'
app_libs = $(LDFLAGS) $(CORE_LDLIBS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(call app_plugin,$*) $(LIBS) $(if $(findstring scener,$*),$(SCENER_LIBS))

.PHONY: all install tools platform share library apps plugins gems scener test clean help groove groove-demo mac-deploy $(PHONY_APP_NAMES)

all: library apps tools $(if $(IS_WIN),,gems)

# Build and run Groove: empty sheet, or preloaded with the demo song.
groove: share $(BIN_DIR)/groove$(EXE_EXT)
	@cd $(BIN_DIR) && ./groove$(EXE_EXT)
groove-demo: share $(BIN_DIR)/groove$(EXE_EXT)
	@cd $(BIN_DIR) && ./groove$(EXE_EXT) --demo

# ── Tools ────────────────────────────────────────────────────────────────
tools: $(TOOLS_BINS)
	@echo "All tools built"

$(BIN_DIR)/%$(EXE_EXT): tools/%.c $(CORE_LIBS) | $(BIN_DIR)
	@echo "TOOL    $@"
	@$(CC) $(TOOLS_CFLAGS) -I. -Itools -o $@ $< \
	    $(LDFLAGS) $(CORE_LDLIBS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS)
	@$(COPY_DLLS)

# Self-contained tools that don't need the core libraries.
$(ORIONC_BIN) $(CGEN_BIN): TOOL_LINK = $(LDFLAGS) $(LIBS)
$(ORIONC_BIN) $(CGEN_BIN): $(BIN_DIR)/%$(EXE_EXT): tools/%.c | $(BIN_DIR)
	@echo "TOOL    $@"
	@$(CC) $(TOOLS_CFLAGS) -I. -Itools -o $@ $< $(TOOL_LINK)

# POSIX command-line converter; no Orion window system or graphics dependency.
$(BIN_DIR)/penciltest-video: tools/penciltest_video.c tools/simplify_lineart.py tools/lineart_strokes.py apps/imageeditor/io/flc_codec.h apps/imageeditor/palette.h apps/imageeditor/palette.c orion/user/palette_file.h | $(BIN_DIR)
	@echo "TOOL    $@"
	@$(CC) -std=c99 -Wall -Wextra -O2 $(PENCILTEST_VIDEO_CFLAGS) -o $@ $< apps/imageeditor/palette.c $(PENCILTEST_VIDEO_LIBS) -lm
	@cp tools/simplify_lineart.py tools/lineart_strokes.py $(BIN_DIR)/

.PHONY: penciltest-video
penciltest-video: $(BIN_DIR)/penciltest-video

$(BIN_DIR)/test_penciltest_video_test$(EXE_EXT): tools/penciltest_video.c
$(BIN_DIR)/test_penciltest_video_test$(EXE_EXT): CFLAGS += $(PENCILTEST_VIDEO_CFLAGS)
$(BIN_DIR)/test_penciltest_video_test$(EXE_EXT): LDFLAGS_TEST += $(PENCILTEST_VIDEO_LIBS)

$(GENERATED_DIR)/$(APPS)/%.h: $(APPS)/%.orion $(ORIONC_BIN) | $(GENERATED_DIR)
	@mkdir -p $(dir $@)
	@echo "GEN     $@"
	@$(ORIONC_BIN) --input $< --output $@ --prefix $(notdir $(basename $<))

$(GENERATED_DIR)/%_lua.h: %.cgen $(CGEN_BIN) | $(GENERATED_DIR)
	@mkdir -p $(dir $@)
	@echo "GEN     $@"
	@$(CGEN_BIN) --input $< --output $@

# docs/lua/<module>.md, the reference generated from each framework .cgen.
.PHONY: lua-docs lua-docs-check
lua-docs: $(CGEN_BIN)
	@mkdir -p docs/lua
	@for f in $(CGEN_DOC_SRCS); do \
	  $(CGEN_BIN) --input $$f --markdown docs/lua/$$(basename $$f .cgen).md --parent "Lua Add-ons" || exit 1; \
	done
lua-docs-check: $(CGEN_BIN)
	@rm -rf $(BUILD_DIR)/lua-docs && mkdir -p $(BUILD_DIR)/lua-docs
	@for f in $(CGEN_DOC_SRCS); do \
	  $(CGEN_BIN) --input $$f --markdown $(BUILD_DIR)/lua-docs/$$(basename $$f .cgen).md --parent "Lua Add-ons" || exit 1; \
	done
	@diff -r docs/lua $(BUILD_DIR)/lua-docs > /dev/null || { echo "FAIL docs/lua is out of date: run make lua-docs"; exit 1; }

# Lua bindings, editor metadata and reference share an XML source of truth.
LEDGER_API_XML = apps/ledger/ledger-api.xml
LEDGER_API_HEADER = $(GENERATED_DIR)/apps/ledger/ledger_api.h
LEDGER_API_DOCS = apps/ledger/docs/lua-api.md
LEDGER_API_SITE = docs/ledger/lua-api.md
LEDGER_API_ANNOTATIONS = apps/ledger/share/stdplugs/automation/ledger-api.d.lua
LEDGER_CATALOG = apps/ledger/share/stdplugs/automation/catalog.lua
LEDGER_CATALOG_DOCS = apps/ledger/docs/automation-catalog.md
LEDGER_CATALOG_SITE = docs/ledger/automation-catalog.md

$(LEDGER_API_HEADER): $(LEDGER_API_XML) apps/ledger/ledger.orion tools/generate_lua_api.py
	python3 tools/generate_lua_api.py $(LEDGER_API_XML) --header $(LEDGER_API_HEADER) --docs $(LEDGER_API_DOCS) --annotations $(LEDGER_API_ANNOTATIONS) --site $(LEDGER_API_SITE)

$(LEDGER_CATALOG): apps/ledger/automation.xml tools/generate_automation_catalog.py
	python3 tools/generate_automation_catalog.py apps/ledger/automation.xml --lua $(LEDGER_CATALOG) --docs $(LEDGER_CATALOG_DOCS) --site $(LEDGER_CATALOG_SITE)

# Secondary outputs are normally written by the primary target above. Recreate
# them when individually removed, including on older GNU Make versions.
$(LEDGER_API_DOCS): $(LEDGER_API_HEADER)
	python3 tools/generate_lua_api.py $(LEDGER_API_XML) --docs $@
$(LEDGER_API_ANNOTATIONS): $(LEDGER_API_DOCS)
	python3 tools/generate_lua_api.py $(LEDGER_API_XML) --annotations $@
$(LEDGER_CATALOG_DOCS): $(LEDGER_CATALOG)
	python3 tools/generate_automation_catalog.py apps/ledger/automation.xml --lua $(LEDGER_CATALOG) --docs $@

$(LEDGER_API_SITE): $(LEDGER_API_HEADER)
	python3 tools/generate_lua_api.py $(LEDGER_API_XML) --site $@
$(LEDGER_CATALOG_SITE): $(LEDGER_CATALOG)
	python3 tools/generate_automation_catalog.py apps/ledger/automation.xml --lua $(LEDGER_CATALOG) --docs $(LEDGER_CATALOG_DOCS) --site $@

LEDGER_CUSTOM_CATALOG = $(GENERATED_DIR)/apps/ledger/custom_catalog.lua
$(LEDGER_CUSTOM_CATALOG): apps/ledger/tests/fixtures/custom-plugin.xml tools/generate_automation_catalog.py
	python3 tools/generate_automation_catalog.py $< --lua $@ --docs $(GENERATED_DIR)/apps/ledger/custom-plugin.md
$(BIN_DIR)/test_ledger_automation_test$(EXE_EXT): $(LEDGER_CUSTOM_CATALOG)

.PHONY: ledger-api ledger-api-check
ledger-api: $(LEDGER_API_SITE) $(LEDGER_CATALOG_SITE) $(LEDGER_API_HEADER) $(LEDGER_API_DOCS) $(LEDGER_API_ANNOTATIONS) $(LEDGER_CATALOG) $(LEDGER_CATALOG_DOCS)
ledger-api-check:
	python3 tools/generate_lua_api.py $(LEDGER_API_XML) --header $(LEDGER_API_HEADER) --docs $(LEDGER_API_DOCS) --annotations $(LEDGER_API_ANNOTATIONS) --site $(LEDGER_API_SITE) --check
	python3 tools/generate_automation_catalog.py apps/ledger/automation.xml --lua $(LEDGER_CATALOG) --docs $(LEDGER_CATALOG_DOCS) --site $(LEDGER_CATALOG_SITE) --check

$(BIN_DIR)/ledger-run$(EXE_EXT): $(LEDGER_API_HEADER) $(GENERATED_DIR)/apps/ledger/ledger.h $(call app_srcs,ledger) $(LEDGER_CATALOG)
$(BIN_DIR)/ledger$(EXE_EXT) $(GEM_DIR)/ledger.gem $(patsubst apps/ledger/tests/%.c,$(BIN_DIR)/test_%$(EXE_EXT),$(wildcard apps/ledger/tests/*.c)): $(LEDGER_API_HEADER) $(LEDGER_CATALOG)
share: $(LEDGER_CATALOG)

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

CORE_HEADERS = $(wildcard vendor/gl_shader/*.[ch]) $(wildcard orion/*.h orion/user/*.h orion/kernel/*.h orion/commctl/*.h orion/commdlg/*.h orion/script/*.h)

# unity_lib <name> <srcs> <lib-deps> <extra-cflags> <link-libs>
# The sources are #included into a single translation unit fed via stdin.
define unity_lib
$(LIB_DIR)/lib$(1).$(LIB_EXT): $(2) $(3) $(PLATFORM_LIB) $(CORE_HEADERS) | $(LIB_DIR)
	@echo "LIB     $$@"
	@printf '%s\n' $(sort $(2)) | sed 's/.*/\#include "&"/' | \
	    $(CC) $(CFLAGS) $(LIB_FLAGS) $(call lib_id_flags,$(1)) $(4) -x c -o $$@ - \
        $(LDFLAGS) $$(RPATH_FLAGS) $(5) $(PLATFORM_LDFLAGS) $(LIBS) $$(IMPLIB_FLAGS)
endef

# libuser already carries commdlg's dialog runtime (event.c ends dialogs); the
# message box joins it, as MessageBox lives in user32, so controls can use it.
USER_SRCS = $(filter-out orion/user/dialog.c orion/user/component_registry.c,$(wildcard orion/user/*.c)) \
            orion/commdlg/msgbox.c

ifeq ($(UNAME_S),Darwin)
$(KERNEL_INPUT_OBJ): orion/kernel/audio_input_apple.m orion/kernel/audio_input.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fblocks -c $< -o $@
endif
$(eval $(call unity_lib,kernel,$(wildcard orion/kernel/*.c),$(KERNEL_INPUT_OBJ),,$(if $(KERNEL_INPUT_OBJ),-x none $(KERNEL_INPUT_OBJ))))
$(eval $(call unity_lib,user,$(USER_SRCS),$(COMMDLG_LIB) $(KERNEL_LIB),,-lcommdlg -lkernel))
$(eval $(call unity_lib,commctl,$(wildcard orion/commctl/*.c),$(USER_LIB) $(KERNEL_LIB),-Icomponents,-luser -lkernel))
# Lua add-ons: runtime .orion forms driven by a script (orion/script/script.h).
$(eval $(call unity_lib,script,$(wildcard orion/script/*.c),$(COMMCTL_LIB) $(USER_LIB) $(KERNEL_LIB) $(filter $(GENERATED_DIR)/orion/%,$(CGEN_HEADERS)),,-lcommctl -luser -lkernel))

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
INSTALL_RUNTIME_LIBS = $(KERNEL_LIB) $(USER_LIB) $(COMMCTL_LIB) $(COMMDLG_LIB) $(SCRIPT_LIB) $(PLATFORM_LIB)
INSTALL_GEMS = $(if $(IS_WIN),,$(GEM_BINS) $(PHONY_APP_GEMS))

install: all
	@echo "INSTALL $(DESTDIR)$(PREFIX)"
	@$(INSTALL) -d "$(DESTDIR)$(PREFIX)/bin" "$(DESTDIR)$(PREFIX)/lib" \
	    "$(DESTDIR)$(PREFIX)/include/vendor/gl_shader" "$(DESTDIR)$(PREFIX)/include/orion" "$(DESTDIR)$(PREFIX)/include/platform" \
	    "$(DESTDIR)$(PREFIX)/lib/orion/gems" "$(DESTDIR)$(PREFIX)/share" \
	    "$(DESTDIR)$(PREFIX)/share/doc/orion"
	@$(INSTALL) -m 755 $(INSTALL_BINS) "$(DESTDIR)$(PREFIX)/bin/"
	@$(INSTALL) -m 755 $(INSTALL_RUNTIME_LIBS) $(COMPONENT_PLUGIN_BINS) "$(DESTDIR)$(PREFIX)/lib/"
	@find orion -type f -name '*.h' | while IFS= read -r file; do \
	  dest="$(DESTDIR)$(PREFIX)/include/$$file"; \
	  $(INSTALL) -d "$$(dirname "$$dest")"; \
	  $(INSTALL) -m 644 "$$file" "$$dest"; \
	done
	@$(INSTALL) -m 644 vendor/gl_shader/gl_shader.h vendor/gl_shader/gl_shader.c vendor/gl_shader/LICENSE "$(DESTDIR)$(PREFIX)/include/vendor/gl_shader/"
	@$(INSTALL) -m 644 platform/platform.h platform/events.h "$(DESTDIR)$(PREFIX)/include/platform/"
	@if [ -n "$(INSTALL_GEMS)" ]; then \
	  $(INSTALL) -m 755 $(INSTALL_GEMS) "$(DESTDIR)$(PREFIX)/lib/orion/gems/"; \
	fi
	@cp -R "$(SHARE_DIR)/." "$(DESTDIR)$(PREFIX)/share/"
	@$(INSTALL) -d "$(DESTDIR)$(PREFIX)/share/scener/share"
	@cp -R "$(APPS)/scener/scenes" "$(APPS)/scener/prefabs" "$(APPS)/scener/mocap" "$(DESTDIR)$(PREFIX)/share/scener/"
	@cp -R "$(APPS)/scener/share/screens" "$(DESTDIR)$(PREFIX)/share/scener/share/"
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
	@$(CC) $(CFLAGS) $(LIB_FLAGS) $(component_id_flags) -I. -I$(APPS)/$* -I$(APPS)/$*/$(COMPS) -o $@ $(wildcard $(APPS)/$*/$(COMPS)/*.c) \
	    $(LDFLAGS) $(CORE_LDLIBS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS)

$(EXAMPLE_BINS) $(PHONY_APP_BINS): $(BIN_DIR)/%$(EXE_EXT): $(CORE_LIBS) $(GENERATED_HEADERS) | $(BIN_DIR) share
	@printf '%-8s%s\n' '$(if $(PHONY_APPS_SRC_$*),PHONY,BIN)' "$@"
	@{ $(call unity_tu,$(call appdir,$*)); } | \
	    $(CC) $(CFLAGS) $(PHONY_APPS_CFLAGS_$*) $(call app_theme_cflags,$*) $(app_inc) -x c -o $@ - \
	    $(LDFLAGS_EXAMPLE) $(app_libs)

# Ledger's native launcher identity uses the same artwork on every platform.
ifeq ($(IS_WIN),1)
WINDRES ?= windres
$(BUILD_DIR)/ledger-icon.o: apps/ledger/icon.rc apps/ledger/share/icon.ico
	@mkdir -p "$(@D)"
	$(WINDRES) -I apps/ledger -i $< -o $@
$(BIN_DIR)/ledger$(EXE_EXT): $(BUILD_DIR)/ledger-icon.o
$(BIN_DIR)/ledger$(EXE_EXT): LDFLAGS_EXAMPLE += -x none $(BUILD_DIR)/ledger-icon.o
endif

ifeq ($(UNAME_S),Linux)
.PHONY: install-ledger-desktop
install: install-ledger-desktop
install-ledger-desktop:
	@$(INSTALL) -d "$(DESTDIR)$(PREFIX)/share/applications" "$(DESTDIR)$(PREFIX)/share/icons/hicolor/1024x1024/apps"
	@sed 's|^Exec=ledger|Exec=$(PREFIX)/bin/ledger|' packaging/linux/com.orion.ledger.desktop > "$(DESTDIR)$(PREFIX)/share/applications/com.orion.ledger.desktop"
	@chmod 644 "$(DESTDIR)$(PREFIX)/share/applications/com.orion.ledger.desktop"
	@$(INSTALL) -m 644 apps/ledger/share/icon.png "$(DESTDIR)$(PREFIX)/share/icons/hicolor/1024x1024/apps/com.orion.ledger.png"
endif

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
test: share $(TEST_BINS) lua-docs-check
	@echo "Running tests..."
	@$(COPY_DLLS)
	@for t in $(TEST_BINS); do \
	    echo "Running $$t..."; \
	    $$t || exit 1; \
	done
	@echo "All tests passed!"

$(TEST_BINS): $(BIN_DIR)/test_%$(EXE_EXT): $(TEST_SRCS) $(wildcard $(TEST_DIR)/*.h) $(TEST_DIR)/test_env.c $(GENERATED_HEADERS) $(CORE_LIBS) $(COMPONENT_PLUGIN_BINS) | $(BIN_DIR) share
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
	    find "$(APPS)/$$app_dir" -name '*.c' ! -name main.c ! -path '*/$(COMPS)/*' ! -path '*/tests/*' ! -path '*/reel/*' | sort | sed 's/.*/#include "&"/'; \
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
	@echo "groove    - Build and run Groove (empty); groove-demo preloads the demo song"
	@echo "gems      - Build all .gem shared libraries"
	@echo "tools     - Build command-line tools"
	@echo "test      - Build and run tests"
	@echo "ipad-all  - Build separate Image Editor, Pencil Test and Groove iPad bundles"
	@echo "ipad / ipad-simulator / ipad-run / ipad-deploy / ipad-mac - APP=imageeditor|penciltest|groove|winamp (winamp is an iPhone app; ipad-deploy auto-selects one connected device)"
	@echo "mac-deploy - Build a native macOS app bundle with its app icon (APP=<appname>)"
	@echo "bundles   - Build portable downloads of every app for this platform into build/bundles (BUNDLE_APPS=\"a b\" for a subset)"
	@echo "list-devices - List paired devices (see packaging/ipad/README.md)"
	@echo "ALLOW_HIGHDPI=0 - Disable high-DPI surfaces (use with -B)"
	@echo "clean     - Remove all build artifacts"
	@echo "help      - Show this help message"
	@echo ""
	@echo "Phony apps (derived builds with extra flags):"
	@$(foreach a,$(PHONY_APP_NAMES),echo "  $a - $(call appdir,$(a)) + $(PHONY_APPS_CFLAGS_$(a))";)
	@echo ""
	@echo "Themes (standalone only; gems inherit the shell's get_theme()):"
	@echo "  THEME_<app>=classic|modern|light|navy|amber   (modern=dark default)"
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
	$(MAKE) -f packaging/ipad/build.mk APP=groove SDK=iphoneos app
mac-deploy:
	$(MAKE) -f packaging/macos/build.mk APP=$(APP) APP_SOURCE=$(or $(PHONY_APPS_SRC_$(APP)),$(APP)) deploy

# Portable per-app downloads (zip / tar.gz) for this platform; see packaging/README.md.
.PHONY: bundles app-source
bundles: all
	sh scripts/bundle-apps.sh "" $(BUILD_DIR)/bundles $(BUNDLE_APPS)
# Source directory name of an app: itself, or the app a phony app is derived from.
app-source:
	@echo $(or $(PHONY_APPS_SRC_$(APP)),$(APP))
list-devices:
	xcrun devicectl list devices
