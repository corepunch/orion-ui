// tests/fill_gap_test.c — Tests for gap-closing flood fill (pencil test).
//
// Covers the Gangnet/Van Thong pipeline: dangling-endpoint and curvature
// maxima detection, invisible stitching, gap-size selectivity, and sliver
// merging.

#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

app_state_t *g_app = NULL;

#define GAP_INK  MAKE_COLOR(0x00, 0x00, 0x00, 0xFF)
#define GAP_FILL MAKE_COLOR(0xFF, 0x00, 0x00, 0xFF)
#define GAP_BRIDGE UINT32_C(0x12345678)

static bool gap_pixel_matches(uint32_t actual, uint32_t expected, uint32_t paper) {
  return expected == GAP_BRIDGE ? actual == paper || actual == GAP_FILL : actual == expected;
}

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
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
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
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
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
  ASSERT_EQUAL(canvas_get_pixel(doc, 15, 23), GAP_FILL);
  ASSERT_EQUAL(canvas_get_pixel(doc, 16, 23), GAP_FILL);

  close_document(doc);
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
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
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
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
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
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
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
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
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
  PASS();
}

static canvas_doc_t *thick_gapped_box(uint32_t *paper_out) {
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  if (!doc) return NULL;
  *paper_out = canvas_get_pixel(doc, 0, 0);
  canvas_draw_line(doc, 8, 8, 23, 8, 0, GAP_INK);
  canvas_draw_line(doc, 8, 9, 23, 9, 0, GAP_INK);
  canvas_draw_line(doc, 8, 8, 8, 23, 0, GAP_INK);
  canvas_draw_line(doc, 9, 8, 9, 23, 0, GAP_INK);
  canvas_draw_line(doc, 22, 8, 22, 23, 0, GAP_INK);
  canvas_draw_line(doc, 23, 8, 23, 23, 0, GAP_INK);
  canvas_draw_line(doc, 8, 22, 14, 22, 0, GAP_INK);
  canvas_draw_line(doc, 8, 23, 14, 23, 0, GAP_INK);
  canvas_draw_line(doc, 17, 22, 23, 22, 0, GAP_INK);
  canvas_draw_line(doc, 17, 23, 23, 23, 0, GAP_INK);
  return doc;
}

void test_gap_thick_stroke_contained(void) {
  TEST("gap fill stitches a 2px opening in a 2px-wide stroke");
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  uint32_t paper;
  canvas_doc_t *doc = thick_gapped_box(&paper);
  ASSERT_NOT_NULL(doc);

  ipoint16_t ends[32];
  int found = canvas_gap_detect_endpoints(doc, paper, ends, 32);
  ASSERT_TRUE(found >= 2);

  int stitches = canvas_flood_fill_with_gap(doc, 15, 15, GAP_FILL, IE_FILL_GAP_SMALL);
  ASSERT_TRUE(stitches >= 1);
  ASSERT_EQUAL(canvas_get_pixel(doc, 15, 15), GAP_FILL);
  ASSERT_EQUAL(canvas_get_pixel(doc, 2, 2), paper);

  close_document(doc);
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
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
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
  PASS();
}

// Check every pixel, including ink and the invisible bridge, in all orientations.
static void test_gap_box_raster(void) {
  TEST("gap fill has no leaks or white pixels at any box orientation or scale");
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  int failures = 0;
  const int gaps[] = {IE_FILL_GAP_SMALL, IE_FILL_GAP_MEDIUM, IE_FILL_GAP_LARGE};
  for (int setting = 0; setting < 3; setting++)
    for (int scale = 1; scale <= 3; scale++)
      for (int rotation = 0; rotation < 4; rotation++) {
        int size = 32 * scale;
        canvas_doc_t *doc = create_document(NULL, size, size);
        ASSERT_NOT_NULL(doc);
        uint32_t paper = canvas_get_pixel(doc, 0, 0);
        uint32_t *expected = malloc((size_t)size * size * sizeof(*expected));
        ASSERT_NOT_NULL(expected);
        for (int y = 0; y < size; y++)
          for (int x = 0; x < size; x++) {
            int u = x, v = y;
            for (int r = 0; r < rotation; r++) { int t = u; u = size - 1 - v; v = t; }
            int bx = u / scale, by = v / scale;
            bool inside = bx > 8 && bx < 23 && by > 8 && by < 23;
            bool ink = (bx >= 8 && bx <= 23 && (by == 8 || by == 23)) ||
                       (by >= 8 && by <= 23 && (bx == 8 || bx == 23));
            if (by == 23 && (bx == 15 || bx == 16)) ink = false;
            if (ink) canvas_set_pixel(doc, x, y, GAP_INK);
            // A thick gap can contain paper or merged slivers; its edges must not leak.
            expected[y * size + x] = by == 23 && (bx == 15 || bx == 16) ? GAP_BRIDGE :
                                    ink ? GAP_INK : inside ? GAP_FILL : paper;
          }
        canvas_flood_fill_with_gap(doc, size / 2, size / 2, GAP_FILL, gaps[setting] * scale);
        int mismatches = 0;
        for (int y = 0; y < size; y++)
          for (int x = 0; x < size; x++)
            if (!gap_pixel_matches(canvas_get_pixel(doc, x, y), expected[y * size + x], paper)) {
              if (!mismatches) fprintf(stderr, "raster gap=%d scale=%d rotation=%d first mismatch=(%d,%d)\n", gaps[setting], scale, rotation, x, y);
              mismatches++;
            }
        if (mismatches) { fprintf(stderr, "raster mismatches=%d\n", mismatches); failures++; }
        free(expected);
        close_document(doc);
      }
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
  ASSERT_EQUAL(failures, 0);
  PASS();
}

static void test_gap_dense_drawing(void) {
  TEST("gap fill handles more than 4096 endpoints without truncation");
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  canvas_doc_t *doc = create_document(NULL, 256, 256);
  ASSERT_NOT_NULL(doc);
  uint32_t paper = canvas_get_pixel(doc, 0, 0);
  for (int y = 4; y < 180; y += 4)
    for (int x = 4; x < 250; x += 4) {
      canvas_set_pixel(doc, x, y, GAP_INK);
      canvas_set_pixel(doc, x + 1, y, GAP_INK);
    }
  canvas_draw_line(doc, 80, 210, 120, 210, 0, GAP_INK);
  canvas_draw_line(doc, 80, 210, 80, 240, 0, GAP_INK);
  canvas_draw_line(doc, 120, 210, 120, 240, 0, GAP_INK);
  canvas_draw_line(doc, 80, 240, 99, 240, 0, GAP_INK);
  canvas_draw_line(doc, 102, 240, 120, 240, 0, GAP_INK);
  ipoint16_t point;
  ASSERT_TRUE(canvas_gap_detect_endpoints(doc, paper, &point, 1) > 4096);
  canvas_flood_fill_with_gap(doc, 100, 220, GAP_FILL, 2);
  int mismatches = 0;
  for (int y = 200; y < 256; y++)
    for (int x = 0; x < 256; x++) {
      bool inside = x > 80 && x < 120 && y > 210 && y < 240;
      bool ink = (x >= 80 && x <= 120 && (y == 210 || y == 240)) ||
                 (y >= 210 && y <= 240 && (x == 80 || x == 120));
      bool bridge = y == 240 && (x == 100 || x == 101);
      if (bridge) ink = false;
      if (canvas_get_pixel(doc, x, y) != (ink ? GAP_INK : inside || bridge ? GAP_FILL : paper)) mismatches++;
    }
  close_document(doc);
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
  ASSERT_EQUAL(mismatches, 0);
  PASS();
}

static void test_gap_diagonal(int hole) {
  TEST("gap closing preserves a diagonal region with or without an opening");
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  canvas_doc_t *doc = create_document(NULL, 48, 48);
  uint32_t paper = canvas_get_pixel(doc, 0, 0);
  canvas_draw_line(doc, 24, 6, 42, 24, 0, GAP_INK);
  canvas_draw_line(doc, 42, 24, 24, 42, 0, GAP_INK);
  canvas_draw_line(doc, 24, 42, 6, 24, 0, GAP_INK);
  canvas_draw_line(doc, 6, 24, 24, 6, 0, GAP_INK);
  ipoint16_t holes[] = {{15,15}, {33,15}, {15,33}, {33,33}};
  if (hole >= 0) canvas_set_pixel(doc, holes[hole].x, holes[hole].y, paper);
  canvas_flood_fill_with_gap(doc, 24, 24, GAP_FILL, IE_FILL_GAP_MEDIUM);
  int mismatches = 0;
  for (int y = 0; y < 48; y++)
    for (int x = 0; x < 48; x++) {
      int distance = abs(x - 24) + abs(y - 24);
      uint32_t expected = distance == 18 ? GAP_INK : distance < 18 ? GAP_FILL : paper;
      if (hole >= 0 && x == holes[hole].x && y == holes[hole].y) expected = GAP_FILL;
      if (canvas_get_pixel(doc, x, y) != expected) mismatches++;
    }
  close_document(doc);
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
  ASSERT_EQUAL(mismatches, 0);
  PASS();
}

static void test_large_circle_outline_is_closed(void) {
  TEST("256px circle outline is closed (no 32-bit ellipse overflow)");
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  canvas_doc_t *doc = create_document(NULL, 544, 544);
  ASSERT_NOT_NULL(doc);
  canvas_draw_ellipse_outline(doc, 272, 272, 256, 256, GAP_INK);
  ASSERT_EQUAL(canvas_get_pixel(doc, 528, 272), GAP_INK);
  ASSERT_EQUAL(canvas_get_pixel(doc, 16, 272), GAP_INK);
  ASSERT_EQUAL(canvas_get_pixel(doc, 272, 528), GAP_INK);
  ASSERT_EQUAL(canvas_get_pixel(doc, 272, 16), GAP_INK);
  canvas_flood_fill_with_gap(doc, 272, 272, GAP_FILL, 0);
  ASSERT_TRUE(canvas_get_pixel(doc, 0, 0) != GAP_FILL);
  close_document(doc);
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
  PASS();
}

static void test_gap_circle(int scale, int degrees, int direction) {
  TEST("circle with a 1-2 degree break fills without leaks or white speckles");
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  int size = 272 * scale, center = size / 2;
  canvas_doc_t *doc = create_document(NULL, size, size);
  ASSERT_NOT_NULL(doc);
  uint32_t paper = canvas_get_pixel(doc, 0, 0);
  canvas_draw_ellipse_outline(doc, center, center, 128*scale, 128*scale, GAP_INK);
  size_t bytes = (size_t)size * size * DOC_BPP;
  uint8_t *before = malloc(bytes);
  uint32_t *expected = malloc((size_t)size * size * sizeof(*expected));
  ASSERT_NOT_NULL(before);
  ASSERT_NOT_NULL(expected);
  memcpy(before, doc->pixels, bytes);
  canvas_flood_fill_with_gap(doc, center, center, GAP_FILL, 0);
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) expected[y * size + x] = canvas_get_pixel(doc, x, y);
  memcpy(doc->pixels, before, bytes);
  double angle = direction * M_PI / 4;
  int erased = 0;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      if (canvas_get_pixel(doc, x, y) != GAP_INK) continue;
      double delta = atan2(y - center, x - center) - angle;
      delta -= 2 * M_PI * floor((delta + M_PI) / (2 * M_PI));
      if (fabs(delta) > degrees * M_PI / 360) continue;
      canvas_set_pixel(doc, x, y, paper);
      expected[y * size + x] = GAP_BRIDGE;
      erased++;
    }
  memcpy(before, doc->pixels, bytes);
  canvas_flood_fill_with_gap(doc, center, center, GAP_FILL, 0);
  bool leaks_without_gap = canvas_get_pixel(doc, 0, 0) == GAP_FILL;
  memcpy(doc->pixels, before, bytes);
  canvas_flood_fill_with_gap(doc, center, center, GAP_FILL, IE_FILL_GAP_MEDIUM * scale);
  int mismatches = 0;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++)
      if (!gap_pixel_matches(canvas_get_pixel(doc, x, y), expected[y * size + x], paper)) mismatches++;
  if (mismatches) fprintf(stderr, "circle scale=%d degrees=%d direction=%d mismatches=%d\n",
                          scale, degrees, direction, mismatches);
  free(before); free(expected);
  close_document(doc);
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
  ASSERT_TRUE(erased > 0);
  ASSERT_TRUE(leaks_without_gap);
  ASSERT_EQUAL(mismatches, 0);
  PASS();
}

static void test_gap_does_not_fill_remote_sliver(void) {
  TEST("gap cleanup does not paint a disconnected pocket elsewhere in the drawing");
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  uint32_t paper;
  canvas_doc_t *doc = thin_pocket(20, &paper);
  ASSERT_NOT_NULL(doc);
  canvas_draw_line(doc, 25, 20, 35, 20, 0, GAP_INK);
  canvas_draw_line(doc, 25, 20, 25, 28, 0, GAP_INK);
  canvas_draw_line(doc, 35, 20, 35, 28, 0, GAP_INK);
  canvas_draw_line(doc, 25, 28, 35, 28, 0, GAP_INK);
  canvas_flood_fill_with_gap(doc, 30, 24, GAP_FILL, 3);
  ASSERT_EQUAL(canvas_get_pixel(doc, 30, 24), GAP_FILL);
  for (int x = 6; x <= 21; x++) ASSERT_EQUAL(canvas_get_pixel(doc, x, 11), paper);
  close_document(doc);
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
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
  test_gap_thick_stroke_contained();
  test_gap_app_setting_wires_through();
  test_gap_does_not_fill_remote_sliver();
  test_gap_box_raster();
  test_gap_dense_drawing();
  for (int hole = -1; hole < 4; hole++) test_gap_diagonal(hole);

  test_large_circle_outline_is_closed();
  for (int scale = 1; scale <= 2; scale++)
    for (int degrees = 1; degrees <= 2; degrees++)
      for (int direction = 0; direction < 8; direction++) test_gap_circle(scale, degrees, direction);

  TEST_END();
}
