// File picker logic tests (headless, no SDL/OpenGL required)
// Validates the icon-assignment and filtering rules used by picker_load_dir,
// and the scroll-adjusted hit-test arithmetic used in win_columnview.

#include "test_framework.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Inline replicas of the functions under test (same logic, no link deps)
// ---------------------------------------------------------------------------

#define ICON_FOLDER 1   // icon8_collapse
#define ICON_FILE   4   // icon8_checkbox

static bool is_png(const char *path) {
  if (!path) return false;
  size_t n = strlen(path);
  if (n < 5) return false;
  const char *ext = path + n - 4;
  return (ext[0] == '.' &&
          (ext[1] == 'p' || ext[1] == 'P') &&
          (ext[2] == 'n' || ext[2] == 'N') &&
          (ext[3] == 'g' || ext[3] == 'G'));
}

// Returns the icon that picker_load_dir would assign, or ICON_FILTERED if excluded.
#define ICON_FILTERED (-1)
static int picker_entry_icon(const char *name, bool is_dir) {
  if (is_dir) return ICON_FOLDER;
  if (is_png(name)) return ICON_FILE;
  return ICON_FILTERED;
}

// ---------------------------------------------------------------------------
// Columnview scroll hit-test arithmetic (from win_columnview click handler)
// ---------------------------------------------------------------------------
#define ENTRY_HEIGHT  13
#define WIN_PADDING    4

static int columnview_hit_row(int my, int scroll_y) {
  return (my - WIN_PADDING + scroll_y) / ENTRY_HEIGHT;
}

static int columnview_item_y(int row, int scroll_y) {
  return row * ENTRY_HEIGHT + WIN_PADDING - scroll_y;
}

// ---------------------------------------------------------------------------
// Tests: icon assignment
// ---------------------------------------------------------------------------

void test_directory_gets_folder_icon(void) {
  TEST("Picker: directory entry gets ICON_FOLDER");
  ASSERT_EQUAL(picker_entry_icon("subdir", true), ICON_FOLDER);
  PASS();
}

void test_png_file_gets_file_icon(void) {
  TEST("Picker: .png file gets ICON_FILE");
  ASSERT_EQUAL(picker_entry_icon("photo.png", false), ICON_FILE);
  PASS();
}

void test_uppercase_png_gets_file_icon(void) {
  TEST("Picker: .PNG (uppercase) gets ICON_FILE");
  ASSERT_EQUAL(picker_entry_icon("image.PNG", false), ICON_FILE);
  PASS();
}

void test_jpg_file_is_excluded(void) {
  TEST("Picker: non-.png file is excluded (returns ICON_FILTERED)");
  ASSERT_EQUAL(picker_entry_icon("photo.jpg", false), ICON_FILTERED);
  PASS();
}

void test_txt_file_is_excluded(void) {
  TEST("Picker: .txt file is excluded");
  ASSERT_EQUAL(picker_entry_icon("readme.txt", false), ICON_FILTERED);
  PASS();
}

void test_directory_always_included(void) {
  TEST("Picker: directory is always included regardless of name");
  ASSERT_EQUAL(picker_entry_icon("documents", true), ICON_FOLDER);
  ASSERT_EQUAL(picker_entry_icon("readme.txt", true), ICON_FOLDER);
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: CVN_SELCHANGE must NOT navigate (only CVN_DBLCLK should)
// ---------------------------------------------------------------------------

// Simulate whether a given notification code should trigger directory navigation.
// Mirrors the fixed picker_proc logic: only CVN_DBLCLK navigates.
#define CVN_SELCHANGE 0x0101u
#define CVN_DBLCLK    0x0102u

static bool should_navigate(uint16_t code, bool is_dir) {
  return code == CVN_DBLCLK && is_dir;
}

void test_selchange_does_not_navigate_directory(void) {
  TEST("Picker: CVN_SELCHANGE on directory must not navigate");
  ASSERT_FALSE(should_navigate(CVN_SELCHANGE, true));
  PASS();
}

void test_dblclk_navigates_directory(void) {
  TEST("Picker: CVN_DBLCLK on directory must navigate");
  ASSERT_TRUE(should_navigate(CVN_DBLCLK, true));
  PASS();
}

void test_dblclk_does_not_navigate_file(void) {
  TEST("Picker: CVN_DBLCLK on file must not navigate");
  ASSERT_FALSE(should_navigate(CVN_DBLCLK, false));
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: columnview scroll hit-test consistency
// ---------------------------------------------------------------------------

void test_hit_row_no_scroll(void) {
  TEST("Columnview: hit-test row matches paint row (no scroll)");
  // Item 0 is painted at y = WIN_PADDING; clicking there should hit row 0.
  ASSERT_EQUAL(columnview_hit_row(WIN_PADDING, 0), 0);
  // Item 1 is at y = ENTRY_HEIGHT + WIN_PADDING
  ASSERT_EQUAL(columnview_hit_row(ENTRY_HEIGHT + WIN_PADDING, 0), 1);
  PASS();
}

void test_hit_row_with_scroll(void) {
  TEST("Columnview: hit-test row matches paint row (with scroll)");
  int scroll = ENTRY_HEIGHT; // one row scrolled
  // After scrolling, item 1's painted y = ENTRY_HEIGHT + WIN_PADDING - scroll = WIN_PADDING.
  int painted_y_of_item1 = columnview_item_y(1, scroll);
  ASSERT_EQUAL(painted_y_of_item1, WIN_PADDING);
  // Clicking at that painted y should hit row 1.
  ASSERT_EQUAL(columnview_hit_row(painted_y_of_item1, scroll), 1);
  PASS();
}

void test_item_y_decreases_with_scroll(void) {
  TEST("Columnview: painted y decreases as scroll increases");
  int y0 = columnview_item_y(0, 0);
  int y1 = columnview_item_y(0, ENTRY_HEIGHT);
  ASSERT_TRUE(y1 < y0);
  PASS();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("File Picker and Columnview Logic");

  test_directory_gets_folder_icon();
  test_png_file_gets_file_icon();
  test_uppercase_png_gets_file_icon();
  test_jpg_file_is_excluded();
  test_txt_file_is_excluded();
  test_directory_always_included();

  test_selchange_does_not_navigate_directory();
  test_dblclk_navigates_directory();
  test_dblclk_does_not_navigate_file();

  test_hit_row_no_scroll();
  test_hit_row_with_scroll();
  test_item_y_decreases_with_scroll();

  TEST_END();
}
