// tests/fill_gap_test.c — Tests for gap-closing flood fill (pencil test).
//
// Covers the Gangnet/Van Thong pipeline: dangling-endpoint and curvature
// maxima detection, invisible stitching, gap-size selectivity, and sliver
// merging.

#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app = NULL;

#define GAP_INK  MAKE_COLOR(0x00, 0x00, 0x00, 0xFF)
#define GAP_FILL MAKE_COLOR(0xFF, 0x00, 0x00, 0xFF)

static bool has_point(const ipoint16_t *pts, int count, int x, int y) {
  for (int i = 0; i < count; i++)
    if (pts[i].x == x && pts[i].y == y) return true;
  return false;
}

void test_gap_detect_endpoints(void) {
  TEST("gap detection finds dangling stroke endpoints");
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  uint32_t paper = canvas_get_pixel(doc, 0, 0);
  canvas_draw_line(doc, 5, 10, 15, 10, 0, GAP_INK);

  ipoint16_t out[16];
  int found = canvas_gap_detect_endpoints(doc, paper, out, 16);
  ASSERT_EQUAL(found, 2);
  ASSERT_TRUE(has_point(out, found, 5, 10));
  ASSERT_TRUE(has_point(out, found, 15, 10));

  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_gap_detect_corners(void) {
  TEST("gap detection finds sharp curvature maxima, not straight runs");
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  uint32_t paper = canvas_get_pixel(doc, 0, 0);
  canvas_draw_line(doc, 10, 5, 10, 15, 0, GAP_INK);
  canvas_draw_line(doc, 10, 15, 20, 15, 0, GAP_INK);

  ipoint16_t out[32];
  int corners = canvas_gap_detect_corners(doc, paper, out, 32);
  ASSERT_EQUAL(corners, 1);
  ASSERT_TRUE(has_point(out, corners, 10, 15));
  ASSERT_FALSE(has_point(out, corners, 10, 10));

  int ends = canvas_gap_detect_endpoints(doc, paper, out, 32);
  ASSERT_EQUAL(ends, 2);
  ASSERT_TRUE(has_point(out, ends, 10, 5));
  ASSERT_TRUE(has_point(out, ends, 20, 15));

  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

static canvas_doc_t *gapped_box(uint32_t *paper_out) {
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  if (!doc) return NULL;
  *paper_out = canvas_get_pixel(doc, 0, 0);
  canvas_draw_line(doc, 8, 8, 23, 8, 0, GAP_INK);
  canvas_draw_line(doc, 8, 8, 8, 23, 0, GAP_INK);
  canvas_draw_line(doc, 23, 8, 23, 23, 0, GAP_INK);
  canvas_draw_line(doc, 8, 23, 14, 23, 0, GAP_INK);
  canvas_draw_line(doc, 17, 23, 23, 23, 0, GAP_INK);
  return doc;
}

void test_gap_stitch_contains_fill(void) {
  TEST("gap fill stitches a 2px gap and contains the fill");
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  uint32_t paper;
  canvas_doc_t *doc = gapped_box(&paper);
  ASSERT_NOT_NULL(doc);

  int stitches = canvas_flood_fill_with_gap(doc, 15, 15, GAP_FILL, IE_FILL_GAP_MEDIUM);
  ASSERT_TRUE(stitches >= 1);
  ASSERT_EQUAL(canvas_get_pixel(doc, 15, 15), GAP_FILL);
  ASSERT_EQUAL(canvas_get_pixel(doc, 2, 2), paper);
  ASSERT_EQUAL(canvas_get_pixel(doc, 15, 26), paper);
  ASSERT_EQUAL(canvas_get_pixel(doc, 15, 23), paper);
  ASSERT_EQUAL(canvas_get_pixel(doc, 16, 23), paper);

  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_gap_off_leaks(void) {
  TEST("gap fill with gap=0 leaks through the opening");
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  uint32_t paper;
  canvas_doc_t *doc = gapped_box(&paper);
  ASSERT_NOT_NULL(doc);
  (void)paper;

  int stitches = canvas_flood_fill_with_gap(doc, 15, 15, GAP_FILL, 0);
  ASSERT_EQUAL(stitches, 0);
  ASSERT_EQUAL(canvas_get_pixel(doc, 15, 15), GAP_FILL);
  ASSERT_EQUAL(canvas_get_pixel(doc, 2, 2), GAP_FILL);

  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_gap_too_large_still_leaks(void) {
  TEST("gap fill does not close an opening wider than the setting");
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  canvas_draw_line(doc, 8, 8, 23, 8, 0, GAP_INK);
  canvas_draw_line(doc, 8, 8, 8, 23, 0, GAP_INK);
  canvas_draw_line(doc, 23, 8, 23, 23, 0, GAP_INK);
  canvas_draw_line(doc, 8, 23, 11, 23, 0, GAP_INK);
  canvas_draw_line(doc, 20, 23, 23, 23, 0, GAP_INK);

  canvas_flood_fill_with_gap(doc, 15, 15, GAP_FILL, IE_FILL_GAP_SMALL);
  ASSERT_EQUAL(canvas_get_pixel(doc, 2, 2), GAP_FILL);

  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

static canvas_doc_t *thin_pocket(int x1, uint32_t *paper_out) {
  canvas_doc_t *doc = create_document(NULL, 40, 32);
  if (!doc) return NULL;
  *paper_out = canvas_get_pixel(doc, 0, 0);
  canvas_draw_line(doc, 5, 10, x1, 10, 0, GAP_INK);
  canvas_draw_line(doc, 5, 12, x1, 12, 0, GAP_INK);
  canvas_draw_line(doc, 5, 10, 5, 12, 0, GAP_INK);
  return doc;
}

void test_gap_sliver_merged(void) {
  TEST("gap fill merges a tiny stitch-enclosed sliver");
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  uint32_t paper;
  canvas_doc_t *doc = thin_pocket(20, &paper);
  ASSERT_NOT_NULL(doc);

  int stitches = canvas_flood_fill_with_gap(doc, 25, 11, GAP_FILL, 3);
  ASSERT_TRUE(stitches >= 1);
  ASSERT_EQUAL(canvas_get_pixel(doc, 25, 11), GAP_FILL);
  ASSERT_EQUAL(canvas_get_pixel(doc, 10, 11), GAP_FILL);

  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_gap_large_pocket_kept(void) {
  TEST("gap fill leaves a large enclosed pocket unfilled");
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  uint32_t paper;
  canvas_doc_t *doc = thin_pocket(31, &paper);
  ASSERT_NOT_NULL(doc);

  canvas_flood_fill_with_gap(doc, 35, 11, GAP_FILL, 3);
  ASSERT_EQUAL(canvas_get_pixel(doc, 35, 11), GAP_FILL);
  ASSERT_EQUAL(canvas_get_pixel(doc, 10, 11), paper);

  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_gap_app_setting_wires_through(void) {
  TEST("canvas_flood_fill honors g_app fill.gap");
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  g_app->fill.gap = IE_FILL_GAP_MEDIUM;
  uint32_t paper;
  canvas_doc_t *doc = gapped_box(&paper);
  ASSERT_NOT_NULL(doc);

  canvas_flood_fill(doc, 15, 15, GAP_FILL);
  ASSERT_EQUAL(canvas_get_pixel(doc, 15, 15), GAP_FILL);
  ASSERT_EQUAL(canvas_get_pixel(doc, 2, 2), paper);

  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("Fill Gap Detection Tests");

  test_gap_detect_endpoints();
  test_gap_detect_corners();
  test_gap_stitch_contains_fill();
  test_gap_off_leaks();
  test_gap_too_large_still_leaks();
  test_gap_sliver_merged();
  test_gap_large_pocket_kept();
  test_gap_app_setting_wires_through();

  TEST_END();
}
