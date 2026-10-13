// tests/filelist_test.c — headless tests for win_filelist logic
// Tests the pure algorithmic parts: extension filter normalisation and
// matching, sort comparator, basename extraction, and path navigation
// arithmetic.  No SDL/OpenGL/filesystem access required.

#include "test_framework.h"
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Inline replicas of filelist.c logic (same code, no link dependencies)
// ---------------------------------------------------------------------------

// ----- Extension filter normalisation ----------------------------------------
static void fl_set_filter(char *out, size_t outsz, const char *ext) {
  if (!ext || !ext[0]) { out[0] = '\0'; return; }
  if (ext[0] == '*') ext++;
  if (ext[0] == '.') {
    strncpy(out, ext, outsz - 1);
  } else {
    out[0] = '.';
    strncpy(out + 1, ext, outsz - 2);
  }
  out[outsz - 1] = '\0';
}

// ----- Extension filter matching — mirrors fl_matches_filter in filelist.c ----
static bool fl_matches_filter(const char *name, const char *filter) {
  if (!filter || !filter[0]) return true;
  size_t nlen = strlen(name);
  size_t flen = strlen(filter);
  if (nlen < flen) return false;
  return strcasecmp(name + nlen - flen, filter) == 0;
}

// ----- Sort comparator -------------------------------------------------------
// fl_sort_entry_t now carries is_hidden (added with hidden-file bug fix);
// the sort comparator only looks at is_dir and basename, so it is unaffected.
typedef struct { char path[512]; bool is_dir; bool is_hidden; } fl_sort_entry_t;

static fl_sort_entry_t mk(const char *path, bool is_dir) {
  fl_sort_entry_t e = {0};
  strncpy(e.path, path, sizeof(e.path) - 1);
  e.is_dir    = is_dir;
  e.is_hidden = false;
  return e;
}

static int fl_sort_compare(const void *a, const void *b) {
  const fl_sort_entry_t *ea = (const fl_sort_entry_t *)a;
  const fl_sort_entry_t *eb = (const fl_sort_entry_t *)b;
  if (ea->is_dir != eb->is_dir)
    return (int)eb->is_dir - (int)ea->is_dir;
  const char *na = strrchr(ea->path, '/'); na = na ? na + 1 : ea->path;
  const char *nb = strrchr(eb->path, '/'); nb = nb ? nb + 1 : eb->path;
  return strcasecmp(na, nb);
}

// ----- Path-parent computation -----------------------------------------------
static void fl_parent_path(const char *path, char *out, size_t outsz) {
  strncpy(out, path, outsz - 1);
  out[outsz - 1] = '\0';
  char *slash = strrchr(out, '/');
  if (slash && slash != out) *slash = '\0';
  else { out[0] = '/'; out[1] = '\0'; }
}

// ----- Basename extraction ---------------------------------------------------
static const char *fl_basename(const char *path) {
  const char *s = strrchr(path, '/');
  return s ? s + 1 : path;
}

// ---------------------------------------------------------------------------
// Tests: fl_set_filter (normalisation)
// ---------------------------------------------------------------------------

void test_filter_set_with_dot(void) {
  TEST("fl_set_filter: '.png' stored as-is");
  char buf[64];
  fl_set_filter(buf, sizeof(buf), ".png");
  ASSERT_STR_EQUAL(buf, ".png");
  PASS();
}

void test_filter_set_without_dot(void) {
  TEST("fl_set_filter: 'png' gets leading dot added");
  char buf[64];
  fl_set_filter(buf, sizeof(buf), "png");
  ASSERT_STR_EQUAL(buf, ".png");
  PASS();
}

void test_filter_set_with_glob(void) {
  TEST("fl_set_filter: '*.png' strips '*' then adds dot");
  char buf[64];
  fl_set_filter(buf, sizeof(buf), "*.png");
  ASSERT_STR_EQUAL(buf, ".png");
  PASS();
}

void test_filter_set_null(void) {
  TEST("fl_set_filter: NULL clears the filter");
  char buf[64] = ".png";
  fl_set_filter(buf, sizeof(buf), NULL);
  ASSERT_EQUAL((int)buf[0], 0);
  PASS();
}

void test_filter_set_empty(void) {
  TEST("fl_set_filter: empty string clears the filter");
  char buf[64] = ".png";
  fl_set_filter(buf, sizeof(buf), "");
  ASSERT_EQUAL((int)buf[0], 0);
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: fl_matches_filter
// ---------------------------------------------------------------------------

void test_filter_match_exact(void) {
  TEST("fl_matches_filter: 'photo.png' matches '.png'");
  ASSERT_TRUE(fl_matches_filter("photo.png", ".png"));
  PASS();
}

void test_filter_match_case_insensitive(void) {
  TEST("fl_matches_filter: 'image.PNG' matches '.png' (case-insensitive)");
  ASSERT_TRUE(fl_matches_filter("image.PNG", ".png"));
  PASS();
}

void test_filter_no_match(void) {
  TEST("fl_matches_filter: 'photo.jpg' does not match '.png'");
  ASSERT_FALSE(fl_matches_filter("photo.jpg", ".png"));
  PASS();
}

void test_filter_empty_matches_all(void) {
  TEST("fl_matches_filter: empty filter matches any filename");
  ASSERT_TRUE(fl_matches_filter("anything.jpg", ""));
  ASSERT_TRUE(fl_matches_filter("readme.txt", ""));
  PASS();
}

void test_filter_name_shorter_than_ext(void) {
  TEST("fl_matches_filter: name shorter than extension does not match");
  ASSERT_FALSE(fl_matches_filter(".p", ".png"));
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: fl_sort_compare (dirs-first, then alphabetical)
// ---------------------------------------------------------------------------

void test_sort_dir_before_file(void) {
  TEST("fl_sort_compare: directory sorts before file");
  fl_sort_entry_t dir  = mk("/dir/subdir", true);
  fl_sort_entry_t file = mk("/dir/file.txt", false);
  ASSERT_TRUE(fl_sort_compare(&dir, &file) < 0);
  PASS();
}

void test_sort_file_after_dir(void) {
  TEST("fl_sort_compare: file sorts after directory");
  fl_sort_entry_t dir  = mk("/dir/subdir", true);
  fl_sort_entry_t file = mk("/dir/file.txt", false);
  ASSERT_TRUE(fl_sort_compare(&file, &dir) > 0);
  PASS();
}

void test_sort_alpha_within_files(void) {
  TEST("fl_sort_compare: 'alpha.png' sorts before 'beta.png'");
  fl_sort_entry_t a = mk("/dir/alpha.png", false);
  fl_sort_entry_t b = mk("/dir/beta.png", false);
  ASSERT_TRUE(fl_sort_compare(&a, &b) < 0);
  PASS();
}

void test_sort_alpha_case_insensitive(void) {
  TEST("fl_sort_compare: 'Alpha.png' and 'alpha.png' sort as equal");
  fl_sort_entry_t a = mk("/dir/Alpha.png", false);
  fl_sort_entry_t b = mk("/dir/alpha.png", false);
  ASSERT_EQUAL(fl_sort_compare(&a, &b), 0);
  PASS();
}

void test_sort_stable_for_two_dirs(void) {
  TEST("fl_sort_compare: two dirs are sorted alphabetically");
  fl_sort_entry_t a = mk("/dir/abc", true);
  fl_sort_entry_t b = mk("/dir/xyz", true);
  ASSERT_TRUE(fl_sort_compare(&a, &b) < 0);
  ASSERT_TRUE(fl_sort_compare(&b, &a) > 0);
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: fl_parent_path
// ---------------------------------------------------------------------------

void test_parent_path_normal(void) {
  TEST("fl_parent_path: strips last component");
  char out[512];
  fl_parent_path("/home/user/docs", out, sizeof(out));
  ASSERT_STR_EQUAL(out, "/home/user");
  PASS();
}

void test_parent_path_at_root(void) {
  TEST("fl_parent_path: stays at '/' when already at root");
  char out[512];
  fl_parent_path("/", out, sizeof(out));
  ASSERT_STR_EQUAL(out, "/");
  PASS();
}

void test_parent_path_one_level_deep(void) {
  TEST("fl_parent_path: /foo -> /");
  char out[512];
  fl_parent_path("/foo", out, sizeof(out));
  ASSERT_STR_EQUAL(out, "/");
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: ".." sentinel display and navigation
// ---------------------------------------------------------------------------

// The ".." entry's path is the literal string ".." (no '/').
// fl_basename must therefore return ".." so the columnview shows "..".
void test_dotdot_basename_is_dotdot(void) {
  TEST("fl_basename: \"..\" sentinel path displays as \"..\"");
  ASSERT_STR_EQUAL(fl_basename(".."), "..");
  PASS();
}

// Inline replica of the navigate-to-parent logic from fl_navigate.
static void fl_navigate_parent(char *curpath) {
  char *slash = strrchr(curpath, '/');
  if (slash && slash != curpath) {
    *slash = '\0';
  } else {
    curpath[0] = '/';
    curpath[1] = '\0';
  }
}

void test_navigate_parent_normal(void) {
  TEST("fl_navigate parent: /home/user/mapview/ui -> /home/user/mapview");
  char path[512] = "/home/user/mapview/ui";
  fl_navigate_parent(path);
  ASSERT_STR_EQUAL(path, "/home/user/mapview");
  PASS();
}

void test_navigate_parent_one_level(void) {
  TEST("fl_navigate parent: /foo -> /");
  char path[512] = "/foo";
  fl_navigate_parent(path);
  ASSERT_STR_EQUAL(path, "/");
  PASS();
}

void test_navigate_parent_stays_at_root(void) {
  TEST("fl_navigate parent: / stays at /");
  char path[512] = "/";
  fl_navigate_parent(path);
  ASSERT_STR_EQUAL(path, "/");
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: hidden-file colour logic
// ---------------------------------------------------------------------------

#define FL_COLOR_FOLDER_TEST     0xffa0d000u
#define COLOR_TEXT_DISABLED_TEST 0xff808080u
#define COLOR_TEXT_NORMAL_TEST   0xffc0c0c0u

static uint32_t fl_entry_color(bool is_dir, bool is_hidden) {
  return is_hidden  ? (uint32_t)COLOR_TEXT_DISABLED_TEST
       : is_dir     ? FL_COLOR_FOLDER_TEST
                    : (uint32_t)COLOR_TEXT_NORMAL_TEST;
}

void test_hidden_file_gets_disabled_color(void) {
  TEST("Hidden file gets COLOR_TEXT_DISABLED");
  ASSERT_EQUAL((int)fl_entry_color(false, true), (int)COLOR_TEXT_DISABLED_TEST);
  PASS();
}

void test_hidden_dir_gets_disabled_color(void) {
  TEST("Hidden directory gets COLOR_TEXT_DISABLED");
  ASSERT_EQUAL((int)fl_entry_color(true, true), (int)COLOR_TEXT_DISABLED_TEST);
  PASS();
}

void test_normal_file_gets_normal_color(void) {
  TEST("Non-hidden file gets COLOR_TEXT_NORMAL");
  ASSERT_EQUAL((int)fl_entry_color(false, false), (int)COLOR_TEXT_NORMAL_TEST);
  PASS();
}

void test_normal_dir_gets_folder_color(void) {
  TEST("Non-hidden directory gets FL_COLOR_FOLDER");
  ASSERT_EQUAL((int)fl_entry_color(true, false), (int)FL_COLOR_FOLDER_TEST);
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: hidden-file skip logic ("." and ".." only, not all dot-files)
// ---------------------------------------------------------------------------

static bool fl_should_skip(const char *name) {
  return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

void test_dot_is_skipped(void) {
  TEST("Entry \".\" is skipped");
  ASSERT_TRUE(fl_should_skip("."));
  PASS();
}

void test_dotdot_entry_is_skipped(void) {
  TEST("Entry \"..\" is skipped (handled separately as the Up entry)");
  ASSERT_TRUE(fl_should_skip(".."));
  PASS();
}

void test_hidden_file_is_not_skipped(void) {
  TEST("Hidden file \".gitignore\" is NOT skipped");
  ASSERT_FALSE(fl_should_skip(".gitignore"));
  PASS();
}

void test_hidden_dir_is_not_skipped(void) {
  TEST("Hidden directory \".git\" is NOT skipped");
  ASSERT_FALSE(fl_should_skip(".git"));
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: columnview clip formula for root vs child windows
// ---------------------------------------------------------------------------
// Mirrors the corrected clip_top / clip_bottom logic in columnview.c:
//   root window  (parent == NULL): clip_top = 0,       clip_bottom = frame.h
//   child window (parent != NULL): clip_top = frame.y, clip_bottom = frame.y+frame.h

#define CV_ENTRY_HEIGHT 13
#define CV_WIN_PADDING   4

static bool cv_item_visible(int row, int scroll_y,
                             bool is_child, int frame_y, int frame_h) {
  int clip_top    = is_child ? frame_y           : 0;
  int clip_bottom = is_child ? frame_y + frame_h : frame_h;
  int y = row * CV_ENTRY_HEIGHT + CV_WIN_PADDING - scroll_y;
  return (y + CV_ENTRY_HEIGHT > clip_top) && (y < clip_bottom);
}

void test_clip_root_row0_visible_at_y20(void) {
  TEST("Clip (root win y=20): row 0 is visible with corrected clip_top=0");
  // Old bug: clip_top=20, y+13=17 <= 20 → hidden. Fixed: clip_top=0.
  ASSERT_TRUE(cv_item_visible(0, 0, false, 20, 240));
  PASS();
}

void test_clip_root_row0_visible_at_y100(void) {
  TEST("Clip (root win y=100): row 0 is visible regardless of screen position");
  ASSERT_TRUE(cv_item_visible(0, 0, false, 100, 240));
  PASS();
}

void test_clip_child_row0_visible(void) {
  TEST("Clip (child win frame.y=2): row 0 is visible");
  ASSERT_TRUE(cv_item_visible(0, 0, true, 2, 220));
  PASS();
}

void test_clip_child_clips_out_of_bounds_row(void) {
  TEST("Clip (child win frame.y=2, frame.h=220): out-of-bounds row is clipped");
  // Row 17: y=17*13+4=225; 225 >= 2+220=222 → clipped.
  ASSERT_FALSE(cv_item_visible(17, 0, true, 2, 220));
  PASS();
}

void test_clip_root_out_of_bounds_row(void) {
  TEST("Clip (root win frame.h=240): row beyond window height is clipped");
  // Row 19: y=19*13+4=251; 251 >= 240 → clipped.
  ASSERT_FALSE(cv_item_visible(19, 0, false, 20, 240));
  PASS();
}

// ---------------------------------------------------------------------------
// Tests: fl_basename
// ---------------------------------------------------------------------------

void test_basename_normal(void) {
  TEST("fl_basename: returns last path component");
  ASSERT_STR_EQUAL(fl_basename("/home/user/file.png"), "file.png");
  PASS();
}

void test_basename_no_slash(void) {
  TEST("fl_basename: returns full string when no slash present");
  ASSERT_STR_EQUAL(fl_basename("file.png"), "file.png");
  PASS();
}

void test_basename_trailing_component(void) {
  TEST("fl_basename: returns component after last slash");
  ASSERT_STR_EQUAL(fl_basename("/a/b/c"), "c");
  PASS();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("win_filelist Logic");

  test_filter_set_with_dot();
  test_filter_set_without_dot();
  test_filter_set_with_glob();
  test_filter_set_null();
  test_filter_set_empty();

  test_filter_match_exact();
  test_filter_match_case_insensitive();
  test_filter_no_match();
  test_filter_empty_matches_all();
  test_filter_name_shorter_than_ext();

  test_sort_dir_before_file();
  test_sort_file_after_dir();
  test_sort_alpha_within_files();
  test_sort_alpha_case_insensitive();
  test_sort_stable_for_two_dirs();

  test_parent_path_normal();
  test_parent_path_at_root();
  test_parent_path_one_level_deep();

  test_basename_normal();
  test_basename_no_slash();
  test_basename_trailing_component();

  // Bug-fix regression tests
  test_dotdot_basename_is_dotdot();
  test_navigate_parent_normal();
  test_navigate_parent_one_level();
  test_navigate_parent_stays_at_root();

  test_hidden_file_gets_disabled_color();
  test_hidden_dir_gets_disabled_color();
  test_normal_file_gets_normal_color();
  test_normal_dir_gets_folder_color();

  test_dot_is_skipped();
  test_dotdot_entry_is_skipped();
  test_hidden_file_is_not_skipped();
  test_hidden_dir_is_not_skipped();

  test_clip_root_row0_visible_at_y20();
  test_clip_root_row0_visible_at_y100();
  test_clip_child_row0_visible();
  test_clip_child_clips_out_of_bounds_row();
  test_clip_root_out_of_bounds_row();

  TEST_END();
}
