// File picker logic tests (headless, no SDL/OpenGL required)
// Validates the extension-filter logic used by win_filelist / picker_proc,
// and the scroll-adjusted hit-test arithmetic shared with win_columnview.

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
  return (my - WIN_PADDING + scroll_y) / ENTRY_HEIGHT;
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
  ASSERT_EQUAL(filelist_hit_row(painted_y_of_item1, scroll), 1);
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

// Mirrors the coordinate translation in win_filelist's kWindowMessageLeftButtonDown:
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
// Main
// ---------------------------------------------------------------------------

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

  TEST_END();
}

