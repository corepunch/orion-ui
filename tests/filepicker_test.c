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

// Mimic picker_proc logic: FLN_SELCHANGE → update edit box for files only;
// FLN_FILEOPEN → update edit box (directory double-clicks never reach picker).
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

  TEST_END();
}

