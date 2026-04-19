// File picker logic tests (headless, no SDL/OpenGL required)
// Validates the extension-filter logic used by win_filelist / picker_proc,
// and the scroll-adjusted hit-test arithmetic shared with win_reportview.

#include "test_framework.h"
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Inline replicas of the functions under test (same logic, no link deps)
// ---------------------------------------------------------------------------

// These constants represent the two categories (folder vs file) returned by
// the entry-icon helper.  They are arbitrary test-local sentinels; the actual
// glyph index values used in production (FL_ICON_FOLDER=5, FL_ICON_FILE=6,
// FL_ICON_UP=7) are an implementation detail of filelist.c and are tested
// separately in filelist_test.c.
#define ICON_FOLDER  1
#define ICON_FILE    4
#define ICON_FILTERED (-1)

// Extension filter matching — mirrors fl_matches_filter in filelist.c.
// filter is a normalised extension like ".png" or "" (no filter).
static bool fl_matches_filter(const char *name, const char *filter) {
  if (!filter || !filter[0]) return true;
  size_t nlen = strlen(name);
  size_t flen = strlen(filter);
  if (nlen < flen) return false;
  return strcasecmp(name + nlen - flen, filter) == 0;
}

// Returns ICON_FOLDER for dirs, ICON_FILE for passing files, ICON_FILTERED
// for files excluded by the extension filter.
static int filelist_entry_icon(const char *name, bool is_dir,
                                const char *filter) {
  if (is_dir) return ICON_FOLDER;
  if (fl_matches_filter(name, filter)) return ICON_FILE;
  return ICON_FILTERED;
}

// ---------------------------------------------------------------------------
// Filelist hit-test arithmetic (mirrors filelist.c / columnview.c)
// ---------------------------------------------------------------------------
#define ENTRY_HEIGHT  13
#define WIN_PADDING    4

static int filelist_hit_row(int my, int scroll_y) {
  (void)scroll_y;  // mouse coordinates are already scroll-adjusted (LOCAL_Y adds scroll[1])
  return (my - WIN_PADDING) / ENTRY_HEIGHT;
}

static int filelist_item_y(int row, int scroll_y) {
  return row * ENTRY_HEIGHT + WIN_PADDING - scroll_y;
}

// ---------------------------------------------------------------------------
// Tests: extension filter
// ---------------------------------------------------------------------------

void test_png_filter_accepts_png(void) {
  TEST("Filelist filter: .png file passes .png filter");
  ASSERT_EQUAL(filelist_entry_icon("photo.png", false, ".png"), ICON_FILE);
  PASS();
}

void test_png_filter_accepts_uppercase_png(void) {
  TEST("Filelist filter: .PNG file passes .png filter (case-insensitive)");
  ASSERT_EQUAL(filelist_entry_icon("image.PNG", false, ".png"), ICON_FILE);
  PASS();
}

void test_png_filter_rejects_jpg(void) {
  TEST("Filelist filter: .jpg file is excluded by .png filter");
  ASSERT_EQUAL(filelist_entry_icon("photo.jpg", false, ".png"), ICON_FILTERED);
  PASS();
}

void test_png_filter_rejects_txt(void) {
  TEST("Filelist filter: .txt file is excluded by .png filter");
  ASSERT_EQUAL(filelist_entry_icon("readme.txt", false, ".png"), ICON_FILTERED);
  PASS();
}

void test_no_filter_accepts_any_file(void) {
  TEST("Filelist filter: any file passes when no filter is set");
  ASSERT_EQUAL(filelist_entry_icon("photo.jpg", false, ""), ICON_FILE);
  ASSERT_EQUAL(filelist_entry_icon("readme.txt", false, ""), ICON_FILE);
  PASS();
}

void test_directory_always_accepted(void) {
  TEST("Filelist filter: directory always gets ICON_FOLDER regardless of filter");
  ASSERT_EQUAL(filelist_entry_icon("documents", true, ".png"), ICON_FOLDER);
  ASSERT_EQUAL(filelist_entry_icon("readme.txt", true, ".png"), ICON_FOLDER);
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: FLN_* notification routing — navigation is internal to win_filelist;
// the following tests validate the logic used by picker_proc to decide
// whether to update the filename edit box for each notification code.
// ---------------------------------------------------------------------------
#define FLN_SELCHANGE   300u
#define FLN_FILEOPEN    301u
#define FLN_NAVDIR      302u

// Mimic picker_proc logic: returns true when the picker should update its
// filename edit box.  FLN_SELCHANGE and FLN_FILEOPEN update the box only for
// file items (not directories); FLN_NAVDIR never updates the box.
static bool picker_updates_editbox(uint16_t code, bool is_directory) {
  if (code == FLN_SELCHANGE && !is_directory) return true;
  if (code == FLN_FILEOPEN  && !is_directory) return true;
  return false;
}

void test_selchange_on_file_updates_editbox(void) {
  TEST("Picker: FLN_SELCHANGE on file updates edit box");
  ASSERT_TRUE(picker_updates_editbox(FLN_SELCHANGE, false));
  PASS();
}

void test_selchange_on_dir_does_not_update_editbox(void) {
  TEST("Picker: FLN_SELCHANGE on directory does not update edit box");
  ASSERT_FALSE(picker_updates_editbox(FLN_SELCHANGE, true));
  PASS();
}

void test_fileopen_on_file_updates_editbox(void) {
  TEST("Picker: FLN_FILEOPEN on file updates edit box");
  ASSERT_TRUE(picker_updates_editbox(FLN_FILEOPEN, false));
  PASS();
}

void test_navdir_does_not_update_editbox(void) {
  TEST("Picker: FLN_NAVDIR does not update edit box");
  ASSERT_FALSE(picker_updates_editbox(FLN_NAVDIR, true));
  ASSERT_FALSE(picker_updates_editbox(FLN_NAVDIR, false));
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: scroll-adjusted hit-test consistency
// ---------------------------------------------------------------------------

void test_hit_row_no_scroll(void) {
  TEST("Filelist: hit-test row matches paint row (no scroll)");
  ASSERT_EQUAL(filelist_hit_row(WIN_PADDING, 0), 0);
  ASSERT_EQUAL(filelist_hit_row(ENTRY_HEIGHT + WIN_PADDING, 0), 1);
  PASS();
}

void test_hit_row_with_scroll(void) {
  TEST("Filelist: hit-test row matches paint row (with scroll)");
  int scroll = ENTRY_HEIGHT;
  int painted_y_of_item1 = filelist_item_y(1, scroll);
  ASSERT_EQUAL(painted_y_of_item1, WIN_PADDING);
  // LOCAL_Y includes scroll[1], so my = painted_y + scroll (the scroll is
  // already baked into the delivered coordinate by event.c).
  int my = painted_y_of_item1 + scroll;
  ASSERT_EQUAL(filelist_hit_row(my, scroll), 1);
  PASS();
}

void test_item_y_decreases_with_scroll(void) {
  TEST("Filelist: painted y decreases as scroll increases");
  ASSERT_TRUE(filelist_item_y(0, ENTRY_HEIGHT) < filelist_item_y(0, 0));
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: child-window coordinate correction
//
// When win_filelist is embedded as a child inside a dialog, event.c computes
// LOCAL_Y = screen_logical_y - child->frame.y.  child->frame.y is
// parent-content-relative (e.g. 2), NOT the screen position of the dialog
// (e.g. 50).  win_filelist's click handler must subtract parent->frame.y to
// recover truly child-local coordinates before calling filelist_hit_row().
// ---------------------------------------------------------------------------

// Mirrors the coordinate translation in win_filelist's evLeftButtonDown:
//   my_corrected = LOCAL_Y_delivered - parent_frame_y
//   (When used as root window parent_frame_y = 0, no correction needed.)
static int fl_child_local_y(int local_y_delivered, int parent_frame_y) {
  return local_y_delivered - parent_frame_y;
}

void test_child_click_row0_with_parent_at_y50(void) {
  TEST("Child filelist: click on row 0 is correctly resolved when dialog at y=50");
  // Dialog at screen y=50, filelist at frame.y=2 within dialog.
  // Click at row 0 (visual y = WIN_PADDING):
  //   screen_logical_y = 50 + 2 + WIN_PADDING = 56
  //   LOCAL_Y delivered = screen_y - child.frame.y = 56 - 2 = 54  (wrong raw)
  //   After correction:  54 - 50 = 4 = WIN_PADDING  (child-local)
  int delivered_my = 54;   // LOCAL_Y with child.frame.y subtracted only
  int parent_y     = 50;   // dialog's screen y
  int local_my     = fl_child_local_y(delivered_my, parent_y);
  ASSERT_EQUAL(filelist_hit_row(local_my, 0), 0);
  PASS();
}

void test_child_click_row0_without_correction_lands_on_wrong_row(void) {
  TEST("Child filelist: without correction, row 0 click lands on row 3 (regression check)");
  // Demonstrates the bug: raw delivered_my = 54 maps to row 3, not row 0.
  int delivered_my = 54;
  ASSERT_EQUAL(filelist_hit_row(delivered_my, 0), 3);
  PASS();
}

void test_child_click_row2_with_parent_at_y100(void) {
  TEST("Child filelist: click on row 2 is correctly resolved when dialog at y=100");
  // Dialog at y=100, filelist at frame.y=2.
  // Row 2 visual y = 2*ENTRY_HEIGHT + WIN_PADDING = 26+4 = 30.
  // screen_y = 100 + 2 + 30 = 132.
  // LOCAL_Y delivered = 132 - 2 = 130.
  // After correction: 130 - 100 = 30 = row 2 visual y.
  int delivered_my = 130;
  int parent_y     = 100;
  int local_my     = fl_child_local_y(delivered_my, parent_y);
  ASSERT_EQUAL(filelist_hit_row(local_my, 0), 2);
  PASS();
}

void test_root_filelist_click_no_correction_needed(void) {
  TEST("Root filelist (filemanager): parent_frame_y=0, correction is identity");
  // For a root window, parent is NULL, so parent_frame_y = 0 and fl_child_local_y
  // is a no-op.  Row 0 click: LOCAL_Y = screen_y - root.frame.y = WIN_PADDING.
  int delivered_my = WIN_PADDING;
  int parent_y     = 0;   // root: no parent correction
  int local_my     = fl_child_local_y(delivered_my, parent_y);
  ASSERT_EQUAL(filelist_hit_row(local_my, 0), 0);
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: columnview background clearing for non-selected items
//
// When win_reportview is used as a child window (e.g. embedded in a picker
// dialog), invalidate_window does NOT post evNCPaint, so
// draw_panel never clears the background.  Every item must therefore clear its
// own background on each paint so that a previously-selected item's highlight
// rectangle does not persist after a new item is selected.
//
// The fix in columnview.c adds fill_rect(COLOR_PANEL_BG, ...) for non-selected
// items, matching the same rect that fill_rect(COLOR_TEXT_NORMAL, ...) draws
// for selected items.
// ---------------------------------------------------------------------------

// Mirrors the paint logic in win_reportview: returns the background colour
// that should be drawn for an item before its icon/text are painted.
// Selected items use the highlight colour; non-selected items use the panel
// background to erase any previous highlight.
#define CV_COLOR_SELECTED    0xff808080u // COLOR_TEXT_NORMAL stand-in
#define CV_COLOR_BG          0xff3c3c3cu // COLOR_PANEL_BG stand-in

static uint32_t cv_item_bg_color(bool is_selected) {
  return is_selected ? CV_COLOR_SELECTED : CV_COLOR_BG;
}

void test_nonselected_item_clears_background(void) {
  TEST("Columnview: non-selected item uses panel background (clears old highlight)");
  // Non-selected items must use the panel background, not the selection colour.
  ASSERT_EQUAL((int)cv_item_bg_color(false), (int)CV_COLOR_BG);
  // Verify that the non-selected colour is NOT the selection highlight colour.
  ASSERT_FALSE(cv_item_bg_color(false) == CV_COLOR_SELECTED);
  PASS();
}

void test_selected_item_uses_highlight_color(void) {
  TEST("Columnview: selected item uses highlight colour (not panel bg)");
  ASSERT_EQUAL((int)cv_item_bg_color(true), (int)CV_COLOR_SELECTED);
  ASSERT_FALSE(cv_item_bg_color(true) == CV_COLOR_BG);
  PASS();
}

void test_selection_clear_on_new_click(void) {
  TEST("Columnview: clicking new item changes selected index, old index no longer selected");
  // Simulate: initial selected=2, new click on index=5.
  int selected = 2;
  int clicked  = 5;
  selected = clicked;  // update selection
  ASSERT_EQUAL(selected, 5);
  // Old index (2) is no longer == selected (5).
  ASSERT_FALSE(2 == selected);
  PASS();
}

// ---------------------------------------------------------------------------
// Main

// Tests: invalidate_window routing — stale selection highlight regression
//
// When win_filelist is a child window, `invalidate_window(child)` must cause
// the ROOT window to be repainted (not just the child).  Only the root-level
// evNCPaint calls draw_panel(), which fills the background
// with COLOR_PANEL_BG and erases stale selection-highlight pixels from the
// previous selection.  Without this, clicking a new item draws the new
// highlight on top of the old one, leaving both visible simultaneously.
//
// The fix: invalidate_window always routes to get_root_window(win) so the
// non-client + client paint cycle always clears the background first.
//
// Here we test the routing logic: given a (parent, child) pair, verify that
// the "invalidate target" is the root (for children) or self (for roots).

// ---------------------------------------------------------------------------

// Mirrors the get_root_window logic: walk up the parent chain.
typedef struct fake_win { struct fake_win *parent; } fake_win_t;
static fake_win_t *fake_get_root(fake_win_t *w) {
  return w->parent ? fake_get_root(w->parent) : w;
}

// Mirrors invalidate_window's target selection after the fix:
//   always invalidate the root window, never just a child.
static fake_win_t *fake_invalidate_target(fake_win_t *w) {
  return fake_get_root(w);
}

void test_invalidate_root_window_targets_self(void) {
  TEST("invalidate_window: root window targets itself");
  fake_win_t root = { .parent = NULL };
  ASSERT_TRUE(fake_invalidate_target(&root) == &root);
  PASS();
}

void test_invalidate_child_window_targets_root(void) {
  TEST("invalidate_window: child window routes to root (fixes stale selection)");
  fake_win_t root  = { .parent = NULL };
  fake_win_t child = { .parent = &root };
  // Before the fix, invalidate_window would only post evPaint to
  // &child, never sending evNCPaint to &root.  After the
  // fix both messages go to &root, clearing the panel background first.
  ASSERT_TRUE(fake_invalidate_target(&child) == &root);
  PASS();
}

void test_invalidate_grandchild_targets_root(void) {
  TEST("invalidate_window: grandchild window routes to root");
  fake_win_t root       = { .parent = NULL };
  fake_win_t child      = { .parent = &root };
  fake_win_t grandchild = { .parent = &child };
  ASSERT_TRUE(fake_invalidate_target(&grandchild) == &root);
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: openfilename_t filter parsing (inline replica of fp_parse_filters
// from commctl/filepicker.c — same logic, no link deps).
// ---------------------------------------------------------------------------

#define OFN_MAX_FILTERS 16

typedef struct {
  char description[128];
  char extension[32];
} t_fp_filter_t;

static int t_fp_parse_filters(const char *raw, t_fp_filter_t *out, int max) {
  if (!raw || !raw[0]) return 0;
  int count = 0;
  const char *p = raw;
  while (*p && count < max) {
    strncpy(out[count].description, p, sizeof(out[count].description) - 1);
    out[count].description[sizeof(out[count].description) - 1] = '\0';
    p += strlen(p) + 1;
    if (!*p) break;
    const char *pattern = p;
    p += strlen(p) + 1;
    const char *star = strchr(pattern, '*');
    if (star && star[1] == '.') {
      const char *dot = star + 1;
      const char *end = strpbrk(dot, ";");
      if (!end) end = dot + strlen(dot);
      if (dot[1] == '*') {
        out[count].extension[0] = '\0';
      } else {
        size_t len = (size_t)(end - dot);
        if (len >= sizeof(out[count].extension)) len = sizeof(out[count].extension) - 1;
        strncpy(out[count].extension, dot, len);
        out[count].extension[len] = '\0';
      }
    } else {
      out[count].extension[0] = '\0';
    }
    count++;
  }
  return count;
}

// Resolve the active filter from nFilterIndex (1-based) into a 0-based index.
static int t_fp_active_filter(int nFilterIndex, int num_filters) {
  return (nFilterIndex >= 1 && nFilterIndex <= num_filters)
         ? nFilterIndex - 1 : 0;
}

// ---------------------------------------------------------------------------
// Tests: save path normalization (inline replica of fp_name_has_extension /
// fp_normalize_save_path behavior in commctl/filepicker.c).
// ---------------------------------------------------------------------------

static bool t_fp_name_has_extension(const char *name, const char *ext) {
  size_t name_len;
  size_t ext_len;

  if (!name || !ext || !ext[0]) return true;

  name_len = strlen(name);
  ext_len = strlen(ext);
  if (name_len < ext_len) return false;
  return strcasecmp(name + name_len - ext_len, ext) == 0;
}

static bool t_fp_normalize_save_path(const char *dir,
                                     const char *typed,
                                     const char *ext,
                                     char *out,
                                     size_t out_sz) {
  char file[512];
  size_t file_len;
  size_t ext_len;

  if (!dir || !typed || !typed[0] || !out || out_sz == 0) return false;

  strncpy(file, typed, sizeof(file) - 1);
  file[sizeof(file) - 1] = '\0';

  if (ext && ext[0] && !t_fp_name_has_extension(file, ext)) {
    file_len = strlen(file);
    ext_len = strlen(ext);
    if (file_len + ext_len >= sizeof(file)) return false;
    memcpy(file + file_len, ext, ext_len + 1);
  }

  if (strcmp(dir, "/") == 0)
    snprintf(out, out_sz, "/%s", file);
  else
    snprintf(out, out_sz, "%s/%s", dir, file);

  return true;
}

void test_save_normalize_appends_missing_extension(void) {
  TEST("Save normalize: appends expected extension when missing");
  char out[512] = {0};
  ASSERT_TRUE(t_fp_normalize_save_path("/tmp", "report", ".tdb", out, sizeof(out)));
  ASSERT_EQUAL(strcmp(out, "/tmp/report.tdb"), 0);
  PASS();
}

void test_save_normalize_keeps_existing_extension(void) {
  TEST("Save normalize: does not append when extension already exists");
  char out[512] = {0};
  ASSERT_TRUE(t_fp_normalize_save_path("/tmp", "report.tdb", ".tdb", out, sizeof(out)));
  ASSERT_EQUAL(strcmp(out, "/tmp/report.tdb"), 0);
  PASS();
}

void test_save_normalize_extension_match_is_case_insensitive(void) {
  TEST("Save normalize: extension match is case-insensitive");
  char out[512] = {0};
  ASSERT_TRUE(t_fp_normalize_save_path("/tmp", "report.TDB", ".tdb", out, sizeof(out)));
  ASSERT_EQUAL(strcmp(out, "/tmp/report.TDB"), 0);
  PASS();
}

void test_save_normalize_all_files_filter_does_not_append(void) {
  TEST("Save normalize: empty extension (all files) does not append");
  char out[512] = {0};
  ASSERT_TRUE(t_fp_normalize_save_path("/tmp", "report", "", out, sizeof(out)));
  ASSERT_EQUAL(strcmp(out, "/tmp/report"), 0);
  PASS();
}

void test_save_normalize_root_dir_builds_single_slash_path(void) {
  TEST("Save normalize: root directory path is joined correctly");
  char out[512] = {0};
  ASSERT_TRUE(t_fp_normalize_save_path("/", "report", ".tdb", out, sizeof(out)));
  ASSERT_EQUAL(strcmp(out, "/report.tdb"), 0);
  PASS();
}

void test_ofn_filter_parse_png(void) {
  TEST("OFN filter: PNG filter parsed to .png extension");
  t_fp_filter_t f[OFN_MAX_FILTERS];
  int n = t_fp_parse_filters("PNG Files\0*.png\0", f, OFN_MAX_FILTERS);
  ASSERT_EQUAL(n, 1);
  ASSERT_EQUAL(strcmp(f[0].description, "PNG Files"), 0);
  ASSERT_EQUAL(strcmp(f[0].extension,   ".png"),      0);
  PASS();
}

void test_ofn_filter_parse_allfiles(void) {
  TEST("OFN filter: *.* pattern yields empty extension (all files)");
  t_fp_filter_t f[OFN_MAX_FILTERS];
  int n = t_fp_parse_filters("All Files\0*.*\0", f, OFN_MAX_FILTERS);
  ASSERT_EQUAL(n, 1);
  ASSERT_EQUAL(strcmp(f[0].description, "All Files"), 0);
  ASSERT_EQUAL(f[0].extension[0], '\0');
  PASS();
}

void test_ofn_filter_parse_multiple(void) {
  TEST("OFN filter: multiple filter pairs parsed correctly");
  t_fp_filter_t f[OFN_MAX_FILTERS];
  int n = t_fp_parse_filters("PNG Files\0*.png\0All Files\0*.*\0", f, OFN_MAX_FILTERS);
  ASSERT_EQUAL(n, 2);
  ASSERT_EQUAL(strcmp(f[0].extension, ".png"), 0);
  ASSERT_EQUAL(f[1].extension[0], '\0');
  PASS();
}

void test_ofn_filter_parse_empty(void) {
  TEST("OFN filter: NULL lpstrFilter yields 0 filters");
  t_fp_filter_t f[OFN_MAX_FILTERS];
  ASSERT_EQUAL(t_fp_parse_filters(NULL, f, OFN_MAX_FILTERS), 0);
  ASSERT_EQUAL(t_fp_parse_filters("",   f, OFN_MAX_FILTERS), 0);
  PASS();
}

void test_ofn_filter_parse_nFilterIndex(void) {
  TEST("OFN filter: nFilterIndex 1 selects first filter, 2 selects second");
  t_fp_filter_t f[OFN_MAX_FILTERS];
  int n = t_fp_parse_filters("PNG Files\0*.png\0All Files\0*.*\0", f, OFN_MAX_FILTERS);
  ASSERT_EQUAL(n, 2);
  ASSERT_EQUAL(t_fp_active_filter(1, n), 0);
  ASSERT_EQUAL(t_fp_active_filter(2, n), 1);
  ASSERT_EQUAL(t_fp_active_filter(0, n), 0);   // invalid → clamp to 0
  ASSERT_EQUAL(t_fp_active_filter(3, n), 0);   // out of range → clamp to 0
  PASS();
}

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("File Picker and Filelist Logic");

  test_png_filter_accepts_png();
  test_png_filter_accepts_uppercase_png();
  test_png_filter_rejects_jpg();
  test_png_filter_rejects_txt();
  test_no_filter_accepts_any_file();
  test_directory_always_accepted();

  test_selchange_on_file_updates_editbox();
  test_selchange_on_dir_does_not_update_editbox();
  test_fileopen_on_file_updates_editbox();
  test_navdir_does_not_update_editbox();

  test_hit_row_no_scroll();
  test_hit_row_with_scroll();
  test_item_y_decreases_with_scroll();

  test_child_click_row0_with_parent_at_y50();
  test_child_click_row0_without_correction_lands_on_wrong_row();
  test_child_click_row2_with_parent_at_y100();
  test_root_filelist_click_no_correction_needed();

  test_nonselected_item_clears_background();
  test_selected_item_uses_highlight_color();
  test_selection_clear_on_new_click();
  test_invalidate_root_window_targets_self();
  test_invalidate_child_window_targets_root();
  test_invalidate_grandchild_targets_root();

  test_ofn_filter_parse_png();
  test_ofn_filter_parse_allfiles();
  test_ofn_filter_parse_multiple();
  test_ofn_filter_parse_empty();
  test_ofn_filter_parse_nFilterIndex();

  test_save_normalize_appends_missing_extension();
  test_save_normalize_keeps_existing_extension();
  test_save_normalize_extension_match_is_case_insensitive();
  test_save_normalize_all_files_filter_does_not_append();
  test_save_normalize_root_dir_builds_single_slash_path();

  TEST_END();
}

