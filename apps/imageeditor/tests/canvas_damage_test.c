#include "test_framework.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app = NULL;
static uint8_t gpu[8 * 8 * 4];
static int uploads;
static irect16_t uploaded;
static bool reject_upload;

static bool test_update(uint32_t tex, int x, int y, int w, int h, const void *rgba) {
  (void)tex;
  uploads++;
  uploaded = R(x, y, w, h);
  if (reject_upload) return false;
  for (int row = 0; row < h; row++)
    memcpy(gpu + ((y + row) * 8 + x) * 4, (const uint8_t *)rgba + row * w * 4, w * 4);
  return true;
}

static uint32_t test_create(int w, int h, const void *rgba,
                            R_TextureFilter filter, R_TextureWrap wrap) {
  (void)filter; (void)wrap;
  test_update(1, 0, 0, w, h, rgba);
  return 1;
}

#define R_UpdateTextureRGBA test_update
#define R_CreateTextureRGBA test_create

static void test_damage(void) {
  TEST("texture damage: partial rows, transparent erase, full override, retry and new texture");
  uint8_t pixels[8 * 8 * DOC_BPP] = {0}, scratch[8 * 8 * 4];
  layer_t layer = {.pixels = pixels, .visible = true, .opacity = 255};
  layer_t *stack[] = {&layer};
  canvas_doc_t doc = {.pixels = pixels, .canvas_w = 8, .canvas_h = 8};
  doc.layer.stack = stack;
  doc.layer.count = 1;
  doc.layer.composite_buf = scratch;
#if IMAGEEDITOR_INDEXED
  doc.ipal.count = 3;
  doc.ipal.transparent = 0;
  doc.ipal.entries[1] = MAKE_COLOR(255, 0, 0, 255);
  doc.ipal.entries[2] = MAKE_COLOR(0, 255, 0, 255);
#endif
  canvas_upload(&doc);
  ASSERT_EQUAL(uploaded.w, 8);
  uploads = 0;
  memset(scratch, 0xAB, sizeof(scratch));
  canvas_set_pixel(&doc, 3, 2, MAKE_COLOR(255, 0, 0, 255));
  canvas_set_pixel(&doc, 4, 3, MAKE_COLOR(0, 255, 0, 255));
  canvas_upload(&doc);
  ASSERT_EQUAL(uploads, 1);
  ASSERT_EQUAL(uploaded.x, 3);
  ASSERT_EQUAL(uploaded.y, 2);
  ASSERT_EQUAL(uploaded.w, 2);
  ASSERT_EQUAL(uploaded.h, 2);
  ASSERT_EQUAL(gpu[(2 * 8 + 3) * 4], 255);
  ASSERT_EQUAL(gpu[(3 * 8 + 4) * 4 + 1], 255);
  ASSERT_EQUAL(scratch[16], 0xAB);
  canvas_upload(&doc);
  ASSERT_EQUAL(uploads, 1);
  canvas_set_pixel(&doc, 3, 2, 0);
  reject_upload = true;
  canvas_upload(&doc);
  ASSERT_EQUAL(layer.dirty_rect.w, 1);
  reject_upload = false;
  canvas_upload(&doc);
  ASSERT_EQUAL(gpu[(2 * 8 + 3) * 4 + 3], 0);

  // A snapshot restore after partial damage must supersede the small rectangle.
  canvas_set_pixel(&doc, 1, 1, MAKE_COLOR(255, 0, 0, 255));
  memset(pixels, 0, sizeof(pixels));
  doc.canvas_dirty = true;
  doc.drawing = true;
  canvas_set_pixel(&doc, 2, 2, MAKE_COLOR(255, 0, 0, 255));
  canvas_upload(&doc);
  ASSERT_EQUAL(uploaded.w, 8);
  ASSERT_EQUAL(gpu[(3 * 8 + 4) * 4 + 3], 0);
  ASSERT_EQUAL(gpu[(2 * 8 + 2) * 4 + 3], 255);
  ASSERT_TRUE(!doc.canvas_dirty);
  layer.tex = 0;
  canvas_upload(&doc);
  ASSERT_EQUAL(uploaded.w, 8);
  canvas_clear(&doc);
  canvas_upload(&doc);
  canvas_shape_begin(&doc, 3, 3);
  canvas_shape_preview(&doc, 3, 3, 6, 3, ID_TOOL_LINE, false,
                       MAKE_COLOR(255, 0, 0, 255), 0, false);
  canvas_upload(&doc);
  ASSERT_EQUAL(gpu[(3 * 8 + 6) * 4 + 3], 255);
  canvas_shape_preview(&doc, 3, 3, 4, 3, ID_TOOL_LINE, false,
                       MAKE_COLOR(255, 0, 0, 255), 0, false);
  canvas_upload(&doc);
  ASSERT_EQUAL(gpu[(3 * 8 + 6) * 4 + 3], 0);
  ASSERT_EQUAL(gpu[(3 * 8 + 4) * 4 + 3], 255);
  free(doc.shape.snapshot);
#if IMAGEEDITOR_INDEXED
  doc.ipal.entries[1] = MAKE_COLOR(0, 0, 255, 255);
  doc.canvas_dirty = true;
  canvas_upload(&doc);
  ASSERT_EQUAL(gpu[(3 * 8 + 4) * 4 + 2], 255);
#else
  canvas_set_pixel(&doc, 7, 7, MAKE_COLOR(11, 22, 33, 128));
  canvas_upload(&doc);
  ASSERT_EQUAL(gpu[(7 * 8 + 7) * 4 + 3], 128);
#endif
  uint8_t other_pixels[8 * 8 * DOC_BPP] = {0};
  layer_t other = {.pixels = other_pixels, .tex = 2};
  layer_t *two[] = {&layer, &other};
  doc.layer.stack = two;
  doc.layer.count = 2;
  uploads = 0;
  canvas_set_pixel(&doc, 0, 0, MAKE_COLOR(255, 0, 0, 255));
  canvas_upload(&doc);
  ASSERT_EQUAL(uploads, 1);
  PASS();
}

int main(void) {
  TEST_START("Canvas texture damage");
  test_damage();
  TEST_END();
}
