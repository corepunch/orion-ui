# Doom's game tables and renderer are separate translation units: both game
# headers deliberately use overlapping names and cannot be unity-included.
DOOM_ED_DIR := apps/doom-ed
DOOM_ED_CGLM_DIR := $(DOOM_ED_DIR)/third_party/cglm
DOOM_ED_SRCS := $(DOOM_ED_DIR)/wad.c $(wildcard $(DOOM_ED_DIR)/mapview/*.c) \
  $(wildcard $(DOOM_ED_DIR)/editor/*.c) \
  $(wildcard $(DOOM_ED_DIR)/editor/windows/*.c) \
  $(wildcard $(DOOM_ED_DIR)/editor/windows/inspector/*.c) \
  $(wildcard $(DOOM_ED_DIR)/hexen/*.c)
# map_data_t currently embeds two large vertex arrays and is returned by value.
ifeq ($(IS_WIN),1)
DOOM_ED_LDFLAGS := -Wl,--stack,16777216
endif
DOOM_ED_OBJS := $(patsubst $(DOOM_ED_DIR)/%.c,$(BUILD_DIR)/doom-ed/%.o,$(DOOM_ED_SRCS))

.PHONY: doom-ed
doom-ed: $(BIN_DIR)/doom-ed$(EXE_EXT)
apps: $(BIN_DIR)/doom-ed$(EXE_EXT)
INSTALL_BINS += $(BIN_DIR)/doom-ed$(EXE_EXT)
install: $(BIN_DIR)/doom-ed$(EXE_EXT)

DOOM_ED_TEST_NAMES := triangulate bbox bsp collision wad walls player
DOOM_ED_TEST_BINS := $(addprefix $(BUILD_DIR)/doom-ed/tests/,$(DOOM_ED_TEST_NAMES))
.PHONY: doom-ed-test
doom-ed-test: $(DOOM_ED_TEST_BINS) $(BIN_DIR)/test_doom_ed_wad_test$(EXE_EXT) $(BIN_DIR)/doom-ed$(EXE_EXT)
	@$(BIN_DIR)/test_doom_ed_wad_test$(EXE_EXT)
	@for test in $(DOOM_ED_TEST_BINS); do $$test || exit 1; done
	@python3 $(DOOM_ED_DIR)/tests/map_import_test.py $(BIN_DIR)/doom-ed$(EXE_EXT)

$(BUILD_DIR)/doom-ed/tests/%: $(DOOM_ED_DIR)/tests/legacy/%_test.c $(DOOM_ED_DIR)/tests/map_test.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -std=c99 -I$(DOOM_ED_DIR) $< -lm -o $@

$(BUILD_DIR)/doom-ed/tests/triangulate: $(DOOM_ED_DIR)/tests/legacy/triangulate_test.c $(DOOM_ED_DIR)/mapview/triangulate.c $(DOOM_ED_DIR)/tests/map_test.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -std=c99 -DTEST_MODE -I$(DOOM_ED_DIR) $(filter %.c,$^) -lm -o $@

$(BUILD_DIR)/doom-ed/%.o: $(DOOM_ED_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(DOOM_ED_DIR) -I$(DOOM_ED_CGLM_DIR)/include -MMD -MP -c $< -o $@

$(BIN_DIR)/doom-ed$(EXE_EXT): $(DOOM_ED_OBJS) $(CORE_LIBS) $(PLATFORM_LIB) | $(BIN_DIR) share
	$(CC) $(DOOM_ED_OBJS) $(LDFLAGS) $(DOOM_ED_LDFLAGS) $(CORE_LDLIBS) $(PLATFORM_LDFLAGS) $(RPATH_FLAGS) $(LIBS) -o $@

-include $(DOOM_ED_OBJS:.o=.d)
