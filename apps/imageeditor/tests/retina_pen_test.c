#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app = NULL;
int g_bw_retina_scale = 2;

static void test_pen_diameter_and_position(void) {
  TEST("retina pen: diameter 2, odd-coordinate position, single-pixel movement");
  uint8_t pixels[32 * 32 * 4] = {0};
  canvas_doc_t doc = {.canvas_w = 32, .canvas_h = 32, .pixels = pixels};
  uint32_t ink = MAKE_COLOR(0, 0, 0, 255);
  canvas_draw_pen_line(&doc, 11, 11, 11, 11, ink);
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++)
      ASSERT_EQUAL(canvas_get_pixel(&doc, x, y),
                   (x >= 11 && x <= 12 && y >= 11 && y <= 12) ? ink : 0);
  canvas_draw_pen_line(&doc, 11, 11, 12, 11, ink);
  ASSERT_EQUAL(canvas_get_pixel(&doc, 13, 11), ink);
  ASSERT_EQUAL(canvas_get_pixel(&doc, 14, 11), 0);
  g_bw_retina_scale = 1;
  canvas_clear(&doc);
  canvas_draw_pen(&doc, 11, 11, ink);
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++)
      ASSERT_EQUAL(canvas_get_pixel(&doc, x, y), (x == 11 && y == 11) ? ink : 0);
  g_bw_retina_scale = 2;
  PASS();
}

static void test_minimum_brush_uses_logical_pixel(void) {
  TEST("retina brush: minimum size paints one logical pixel");
  uint8_t pixels[32 * 32 * 4] = {0};
  canvas_doc_t doc = {.canvas_w = 32, .canvas_h = 32, .pixels = pixels};
  uint32_t ink = MAKE_COLOR(0, 0, 0, 255);
  canvas_draw_scaled_circle(&doc, 11, 11, 0, ink);
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++)
      ASSERT_EQUAL(canvas_get_pixel(&doc, x, y),
                   (x >= 11 && x <= 12 && y >= 11 && y <= 12) ? ink : 0);
  canvas_clear(&doc);
  canvas_draw_scaled_soft_line(&doc, 11, 11, 12, 11, 0, ink);
  ASSERT_EQUAL(canvas_get_pixel(&doc, 11, 11), ink);
  ASSERT_EQUAL(canvas_get_pixel(&doc, 13, 12), ink);
  ASSERT_EQUAL(canvas_get_pixel(&doc, 14, 11), 0);
  PASS();
}

static void render_shape(canvas_doc_t *doc, int tool, bool filled, int offset) {
  uint32_t ink = MAKE_COLOR(0, 0, 0, 255), fill = MAKE_COLOR(255, 255, 255, 255);
  if (tool == ID_TOOL_POLYGON) {
    ipoint16_t pts[] = {{10 + offset, 10}, {24 + offset, 13}, {18 + offset, 24}};
    canvas_draw_polygon_scaled(doc, pts, 3, filled, ink, fill);
  } else {
    canvas_shape_preview(doc, 16 + offset, 16, 23 + offset, 22, tool, filled, ink, fill, false);
  }
}

static void test_shape_pixel_translation(void) {
  TEST("retina shapes: moving one backing pixel preserves geometry and fill");
  uint8_t a[40 * 40 * 4], b[40 * 40 * 4];
  canvas_doc_t first = {.canvas_w = 40, .canvas_h = 40, .pixels = a};
  canvas_doc_t second = {.canvas_w = 40, .canvas_h = 40, .pixels = b};
  int tools[] = {ID_TOOL_LINE, ID_TOOL_RECT, ID_TOOL_ELLIPSE, ID_TOOL_ROUNDED_RECT, ID_TOOL_POLYGON};
  for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) {
    for (int filled = 0; filled <= 1; filled++) {
      memset(a, 0, sizeof(a)); memset(b, 0, sizeof(b));
      render_shape(&first, tools[i], filled, 0);
      render_shape(&second, tools[i], filled, 1);
      ASSERT_TRUE(first.modified);
      for (int y = 0; y < 40; y++)
        for (int x = 0; x < 39; x++)
          ASSERT_EQUAL(canvas_get_pixel(&first, x, y), canvas_get_pixel(&second, x + 1, y));
    }
  }
  PASS();
}

int main(void) {
  TEST_START("Retina raster precision");
  test_pen_diameter_and_position();
  test_minimum_brush_uses_logical_pixel();
  test_shape_pixel_translation();
  TEST_END();
}
