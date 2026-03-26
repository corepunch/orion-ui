// tests/filelist_test.c — headless tests for win_filelist logic
// Tests the pure algorithmic parts: extension filter normalisation and
// matching, sort comparator, basename extraction, and path navigation
// arithmetic.  No SDL/OpenGL/filesystem access required.

#include "test_framework.h"
#include <string.h>
#include <strings.h>
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
typedef struct { char path[512]; bool is_dir; } fl_sort_entry_t;

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

static fl_sort_entry_t mk(const char *path, bool is_dir) {
  fl_sort_entry_t e;
  strncpy(e.path, path, sizeof(e.path) - 1);
  e.path[sizeof(e.path) - 1] = '\0';
  e.is_dir = is_dir;
  return e;
}

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

  TEST_END();
}
