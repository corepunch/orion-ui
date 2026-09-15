#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;

static void stroke_test_palette(canvas_doc_t *doc) {
#if IMAGEEDITOR_INDEXED
  doc->ipal.count = 3;
  doc->ipal.transparent = 0;
  doc->ipal.entries[1] = MAKE_COLOR(0, 0, 0, 255);
  doc->ipal.entries[2] = MAKE_COLOR(0, 0, 0, 100);
#else
  (void)doc;
#endif
}

static void test_stroke_geometry(void) {
  TEST("freehand rounds corners, fills sparse samples and preserves endpoints");
  uint8_t pixels[64 * 64 * DOC_BPP] = {0};
  canvas_doc_t doc = {.pixels = pixels, .canvas_w = 64, .canvas_h = 64};
  stroke_test_palette(&doc);
  uint32_t ink = MAKE_COLOR(0, 0, 0, 255);
  canvas_stroke_begin(&doc, (ipoint16_t){8, 8}, 0, ink, false);
  canvas_stroke_drag(&doc, (ipoint16_t){40, 8});
  canvas_stroke_drag(&doc, (ipoint16_t){40, 40});
  canvas_stroke_end(&doc, (ipoint16_t){40, 48});
  ASSERT_EQUAL(canvas_get_pixel(&doc, 8, 8), ink);
  ASSERT_EQUAL(canvas_get_pixel(&doc, 36, 12), ink);
  ASSERT_EQUAL(canvas_get_pixel(&doc, 40, 8), 0);
  for (int x = 8; x <= 24; x++) ASSERT_EQUAL(canvas_get_pixel(&doc, x, 8), ink);
  for (int y = 24; y <= 48; y++) ASSERT_EQUAL(canvas_get_pixel(&doc, 40, y), ink);
  ASSERT_FALSE(doc.stroke.active);
  PASS();
}

static void test_stroke_short(void) {
  TEST("tap, duplicates and release without mouse move do not darken soft stamps");
  uint8_t pixels[64 * 64 * DOC_BPP] = {0}, snapshot[sizeof(pixels)];
  canvas_doc_t doc = {.pixels = pixels, .canvas_w = 64, .canvas_h = 64};
  stroke_test_palette(&doc);
  uint32_t ink = MAKE_COLOR(0, 0, 0, 100);
  canvas_stroke_begin(&doc, (ipoint16_t){8, 8}, 2, ink, true);
  memcpy(snapshot, pixels, sizeof(pixels));
  canvas_stroke_drag(&doc, (ipoint16_t){8, 8});
  canvas_stroke_end(&doc, (ipoint16_t){8, 8});
  ASSERT_EQUAL(memcmp(snapshot, pixels, sizeof(pixels)), 0);
  canvas_stroke_begin(&doc, (ipoint16_t){20, 20}, 0, ink, false);
  canvas_stroke_end(&doc, (ipoint16_t){50, 20});
  for (int x = 20; x <= 50; x++) ASSERT_EQUAL(canvas_get_pixel(&doc, x, 20), ink);
  canvas_stroke_begin(&doc, (ipoint16_t){10, 30}, 0, ink, false);
  canvas_stroke_drag(&doc, (ipoint16_t){30, 30});
  canvas_stroke_cancel(&doc);
  memcpy(snapshot, pixels, sizeof(pixels));
  canvas_stroke_end(&doc, (ipoint16_t){50, 30});
  ASSERT_EQUAL(memcmp(snapshot, pixels, sizeof(pixels)), 0);
  PASS();
}

static void test_stroke_retina_selection(void) {
  TEST("curve stamps retain Retina sizing and selection clipping");
  uint8_t pixels[64 * 64 * DOC_BPP] = {0};
  canvas_doc_t doc = {.pixels = pixels, .canvas_w = 64, .canvas_h = 64};
  stroke_test_palette(&doc);
  uint32_t ink = MAKE_COLOR(0, 0, 0, 255);
  g_bw_retina_scale = 2;
  canvas_stroke_begin(&doc, (ipoint16_t){20, 20}, 2, ink, false);
  canvas_stroke_end(&doc, (ipoint16_t){20, 20});
  ASSERT_EQUAL(canvas_get_pixel(&doc, 24, 20), ink);
  ASSERT_EQUAL(canvas_get_pixel(&doc, 25, 20), 0);
  g_bw_retina_scale = 1;
  canvas_select_rect(&doc, 18, 18, 22, 22);
  canvas_stroke_begin(&doc, (ipoint16_t){10, 20}, 0, 0, false);
  canvas_stroke_end(&doc, (ipoint16_t){30, 20});
  ASSERT_EQUAL(canvas_get_pixel(&doc, 20, 20), 0);
  ASSERT_EQUAL(canvas_get_pixel(&doc, 24, 20), ink);
  canvas_clear_selection_mask(&doc);
  PASS();
}

static void test_stroke_window(void) {
  TEST("canvas messages smooth all freehand tools and release completes one undoable stroke");
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  g_app->fg_color = MAKE_COLOR(0, 0, 0, 255);
  int tools[] = {ID_TOOL_PENCIL, ID_TOOL_BRUSH, ID_TOOL_ERASER};
  for (int i = 0; i < 3; i++) {
    g_app->current_tool = tools[i];
    canvas_doc_t *doc = create_document(NULL, 64, 64);
    ASSERT_NOT_NULL(doc);
    uint32_t original = canvas_get_pixel(doc, 36, 12);
    send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(8, 8), NULL);
    send_message(doc->canvas_win, evMouseMove, MAKEDWORD(40, 8), NULL);
    send_message(doc->canvas_win, evMouseMove, MAKEDWORD(40, 40), NULL);
    send_message(doc->canvas_win, evLeftButtonUp, MAKEDWORD(40, 48), NULL);
    uint32_t ink = tools[i] == ID_TOOL_ERASER ? 0 : g_app->fg_color;
    ASSERT_EQUAL(canvas_get_pixel(doc, 36, 12), ink);
    ASSERT_EQUAL(canvas_get_pixel(doc, 40, 48), ink);
    ASSERT_EQUAL(canvas_get_pixel(doc, 40, 8), original);
    ASSERT_FALSE(doc->stroke.active);
    ASSERT_EQUAL(doc->undo.count, 1);
    ASSERT_TRUE(doc_cancel_undo(doc));
    ASSERT_EQUAL(canvas_get_pixel(doc, 36, 12), original);
    close_document(doc);
  }
  test_env_shutdown();
  free(g_app); g_app = NULL;
  PASS();
}

int main(void) {
  TEST_START("Canvas stroke smoothing");
  test_stroke_geometry();
  test_stroke_short();
  test_stroke_retina_selection();
  test_stroke_window();
  TEST_END();
}
