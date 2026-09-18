#define IMAGEEDITOR_BW 1
#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"
#include "apps/imageeditor/tools/tools.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;

static void test_pencil_fill_scale(int scale, int gap) {
  TEST("pencil fill tool scales gap distance and supports exact undo/redo");
  test_env_init();
  g_bw_retina_scale = scale;
  g_app = calloc(1, sizeof(*g_app));
  g_app->fg_color = IE_INK_COLOR;
  g_app->fill.gap = gap;
  canvas_doc_t *doc = create_document(NULL, 64, 64);
  ASSERT_NOT_NULL(doc);
  int size = doc->canvas_w;
  uint32_t paper = canvas_get_pixel(doc, 0, 0), ink = g_app->fg_color;
  canvas_draw_pen_line(doc, 8*scale, 8*scale, 55*scale, 8*scale, ink);
  canvas_draw_pen_line(doc, 8*scale, 8*scale, 8*scale, 55*scale, ink);
  canvas_draw_pen_line(doc, 55*scale, 8*scale, 55*scale, 55*scale, ink);
  canvas_draw_pen_line(doc, 8*scale, 55*scale, 20*scale, 55*scale, ink);
  canvas_draw_pen_line(doc, (21+gap)*scale, 55*scale, 55*scale, 55*scale, ink);
  size_t bytes = (size_t)size * size * DOC_BPP;
  uint8_t *before = malloc(bytes), *after = malloc(bytes);
  ASSERT_NOT_NULL(before);
  ASSERT_NOT_NULL(after);
  memcpy(before, doc->pixels, bytes);
  register_builtin_tools();
  const tool_handler_t *tool = get_tool_handler(ID_TOOL_FILL);
  ASSERT_NOT_NULL(tool);
  tool->begin(doc, NULL, (ipoint16_t){32*scale, 32*scale});
  int mismatches = 0;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      int offset = (scale - 1) / 2;
      if (y >= 55*scale - offset && y < 56*scale - offset &&
          x >= 20*scale - offset && x < (22+gap)*scale - offset) continue;
      bool inside = x >= 9*scale - offset && x < 55*scale - offset &&
                    y >= 9*scale - offset && y < 55*scale - offset;
      bool filled = inside || before[y * size + x] != IMAGEEDITOR_TRANSPARENT_INDEX;
      if (canvas_get_pixel(doc, x, y) != (filled ? ink : paper)) {
        if (!mismatches) fprintf(stderr, "pencil scale=%d gap=%d mismatch=(%d,%d) expected=%u actual=%u\n",
                                 scale, gap, x, y, filled ? ink : paper, canvas_get_pixel(doc, x, y));
        mismatches++;
      }
    }
  memcpy(after, doc->pixels, bytes);
  bool undone = doc_undo(doc) && memcmp(before, doc->pixels, bytes) == 0;
  bool redone = doc_redo(doc) && memcmp(after, doc->pixels, bytes) == 0;
  free(before); free(after);
  close_document(doc);
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
  ASSERT_EQUAL(mismatches, 0);
  ASSERT_TRUE(undone);
  ASSERT_TRUE(redone);
  PASS();
}

static void test_binary_outline_fixture(void) {
  TEST("binary screenshot outline: gap fill matches ordinary fill at every pixel");
  FILE *f = fopen("apps/imageeditor/tests/fixtures/fill_outline_0042.pbm", "rb");
  ASSERT_NOT_NULL(f);
  int w, h;
  ASSERT_EQUAL(fscanf(f, "P4\n%d %d", &w, &h), 2);
  ASSERT_EQUAL(fgetc(f), '\n');
  test_env_init();
  g_bw_retina_scale = 1;
  g_app = calloc(1, sizeof(*g_app));
  canvas_doc_t *doc = create_document(NULL, w, h);
  ASSERT_NOT_NULL(doc);
  uint32_t ink = IE_INK_COLOR;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x += 8) {
      int bits = fgetc(f);
      ASSERT_TRUE(bits != EOF);
      for (int b = 0; b < 8 && x+b < w; b++)
        if (bits & (128 >> b)) canvas_set_pixel(doc, x+b, y, ink);
    }
  fclose(f);
  size_t bytes = (size_t)w * h;
  uint8_t *before = malloc(bytes), *expected = malloc(bytes);
  ASSERT_NOT_NULL(before);
  ASSERT_NOT_NULL(expected);
  memcpy(before, doc->pixels, bytes);
  canvas_flood_fill_with_gap(doc, 270, 270, ink, 0);
  ASSERT_TRUE(canvas_get_pixel(doc, 0, 0) != ink);
  memcpy(expected, doc->pixels, bytes);
  int failures = 0;
  const int gaps[] = {2, 4, 10, 20};
  for (int setting = 0; setting < 4; setting++) {
    memcpy(doc->pixels, before, bytes);
    canvas_flood_fill_with_gap(doc, 270, 270, ink, gaps[setting]);
    int missing = 0, spill = 0;
    for (size_t i = 0; i < bytes; i++) {
      if (doc->pixels[i] == expected[i]) continue;
      if (expected[i]) missing++; else spill++;
    }
    fprintf(stderr, "binary outline gap=%d missing=%d spill=%d\n", gaps[setting], missing, spill);
    if (missing || spill) failures++;
  }
  // Break the same irregular outline, then require containment and a clean interior.
  memcpy(doc->pixels, before, bytes);
  uint32_t paper = canvas_get_pixel(doc, 0, 0);
  for (int y = 200; y <= 202; y++)
    for (int x = 400; x < w; x++) canvas_set_pixel(doc, x, y, paper);
  uint8_t *opened = malloc(bytes);
  ASSERT_NOT_NULL(opened);
  memcpy(opened, doc->pixels, bytes);
  canvas_flood_fill_with_gap(doc, 270, 270, ink, 0);
  bool leaks_without_gap = canvas_get_pixel(doc, 0, 0) == ink;
  memcpy(doc->pixels, opened, bytes);
  canvas_flood_fill_with_gap(doc, 270, 270, ink, 10);
  int missing = 0, spill = 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      size_t i = (size_t)y * w + x;
      if (before[i] && !opened[i]) continue;
      if (doc->pixels[i] == expected[i]) continue;
      if (expected[i]) missing++; else spill++;
    }
  fprintf(stderr, "opened binary outline missing=%d spill=%d leaks_without_gap=%d\n",
          missing, spill, leaks_without_gap);
  if (missing || spill || !leaks_without_gap) failures++;
  free(opened);
  free(before); free(expected);
  close_document(doc);
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
  ASSERT_EQUAL(failures, 0);
  PASS();
}

int main(void) {
  TEST_START("Pencil Test fill integration");
  const int gaps[] = {IE_FILL_GAP_SMALL, IE_FILL_GAP_MEDIUM, IE_FILL_GAP_LARGE};
  for (int scale = 1; scale <= 4; scale++)
    for (int setting = 0; setting < 3; setting++) test_pencil_fill_scale(scale, gaps[setting]);
  test_binary_outline_fixture();
  TEST_END();
}
