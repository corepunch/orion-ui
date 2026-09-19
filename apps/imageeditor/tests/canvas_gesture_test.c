#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;

static void test_screen_aligned_shapes(void) {
  TEST("rotated shape previews follow screen axes across zoom, Retina, fill and Shift");
  uint8_t pixels[128 * 128 * DOC_BPP] = {0};
  window_t win = {0};
  canvas_doc_t doc = {.pixels = pixels, .canvas_w = 128, .canvas_h = 128, .canvas_win = &win};
  const int tools[] = {ID_TOOL_RECT, ID_TOOL_ELLIPSE, ID_TOOL_ROUNDED_RECT};
  const float angles[] = {0, 0.78539816339f, -0.52359877559f, 1.57079632679f};
  uint32_t ink = MAKE_COLOR(0, 0, 0, 255), unused_bg = MAKE_COLOR(200, 100, 50, 255);
  for (int retina = 1; retina <= 2; retina++) {
    g_bw_retina_scale = retina;
    for (int a = 0; a < ARRAY_LEN(angles); a++) {
      float c = cosf(angles[a]), s = sinf(angles[a]), scale = 1.5f / retina;
      win.view.enabled = true;
      win.view.matrix = (view_matrix_t){scale * c, scale * s, 17, -9};
      for (int t = 0; t < ARRAY_LEN(tools); t++) {
        for (int filled = 0; filled <= 1; filled++) {
          for (int shift = 0; shift <= 1; shift++) {
            memset(pixels, 0, sizeof(pixels));
            canvas_shape_begin(&doc, 64, 64);
            canvas_shape_preview(&doc, 64, 64, 100, 100, tools[t], filled, ink, unused_bg, false);
            canvas_shape_preview(&doc, 64, 64, 64 + (int)lroundf(c * 24 + s * 10),
                                 64 + (int)lroundf(-s * 24 + c * 10), tools[t], filled, ink, unused_bg, shift);
            float min_x = 128, min_y = 128, max_x = -128, max_y = -128;
            int painted = 0;
            for (int y = 0; y < 128; y++) {
              for (int x = 0; x < 128; x++) {
                uint32_t pixel = canvas_get_pixel(&doc, x, y);
                if (!pixel) continue;
                ASSERT_EQUAL(pixel, ink);
                painted++;
                float sx = c * (x - 64) - s * (y - 64), sy = s * (x - 64) + c * (y - 64);
                min_x = MIN(min_x, sx); max_x = MAX(max_x, sx);
                min_y = MIN(min_y, sy); max_y = MAX(max_y, sy);
              }
            }
            float exp_hw = shift ? 10 : 24, exp_hh = 10;
            if (tools[t] == ID_TOOL_ELLIPSE) {
              exp_hw = shift ? ceilf(hypotf(24, 10)) : ceilf(1.41421356237f * 24);
              exp_hh = shift ? exp_hw : ceilf(1.41421356237f * 10);
            }
            ASSERT_TRUE(painted > 0);
            ASSERT_TRUE(fabsf(min_x + exp_hw) <= 3 && fabsf(max_x - exp_hw) <= 3);
            ASSERT_TRUE(fabsf(min_y + exp_hh) <= 3 && fabsf(max_y - exp_hh) <= 3);
            ASSERT_EQUAL(canvas_get_pixel(&doc, 64, 64), filled ? ink : 0);
            canvas_shape_commit(&doc);
            ASSERT_EQUAL(canvas_get_pixel(&doc, 64, 64), filled ? ink : 0);
          }
        }
      }
    }
  }
  free(doc.shape.snapshot);
  g_bw_retina_scale = 1;
  PASS();
}

static int ink_near(canvas_doc_t *doc, int x, int y, uint32_t ink, int r) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (canvas_get_pixel(doc, x + dx, y + dy) == ink) return 1;
  return 0;
}

static void test_ellipse_drag_on_curve(void) {
  TEST("ellipse drag point sits on the curve, not the bounding-box corner");
  uint8_t pixels[160 * 160 * DOC_BPP] = {0};
  canvas_doc_t doc = {.pixels = pixels, .canvas_w = 160, .canvas_h = 160};
  uint32_t ink = MAKE_COLOR(0, 0, 0, 255), bg = MAKE_COLOR(200, 100, 50, 255);
  int cx = 80, cy = 80, dx = 30, dy = 20;
  canvas_shape_begin(&doc, cx, cy);
  canvas_shape_preview(&doc, cx, cy, cx + dx, cy + dy, ID_TOOL_ELLIPSE, true, ink, bg, false);
  ASSERT_EQUAL(canvas_get_pixel(&doc, cx, cy), ink);
  ASSERT_TRUE(ink_near(&doc, cx + dx, cy + dy, ink, 1));
  ASSERT_EQUAL(canvas_get_pixel(&doc, cx + dx * 3 / 2, cy + dy * 3 / 2), 0);
  memset(pixels, 0, sizeof(pixels));
  canvas_shape_preview(&doc, cx, cy, cx + dx, cy + dy, ID_TOOL_ELLIPSE, true, ink, bg, true);
  ASSERT_EQUAL(canvas_get_pixel(&doc, cx, cy), ink);
  ASSERT_TRUE(ink_near(&doc, cx + dx, cy + dy, ink, 1));
  ASSERT_EQUAL(canvas_get_pixel(&doc, cx + dx * 3 / 2, cy + dy * 3 / 2), 0);
  int min_x = 160, max_x = -1, min_y = 160, max_y = -1;
  for (int y = 0; y < 160; y++)
    for (int x = 0; x < 160; x++) {
      if (canvas_get_pixel(&doc, x, y) != ink) continue;
      min_x = MIN(min_x, x); max_x = MAX(max_x, x);
      min_y = MIN(min_y, y); max_y = MAX(max_y, y);
    }
  ASSERT_TRUE(abs((max_x - min_x) - (max_y - min_y)) <= 2);
  memset(pixels, 0, sizeof(pixels));
  canvas_shape_preview(&doc, cx, cy, cx + dx, cy + dy, ID_TOOL_RECT, true, ink, bg, false);
  ASSERT_EQUAL(canvas_get_pixel(&doc, cx + dx, cy + dy), ink);
  ASSERT_EQUAL(canvas_get_pixel(&doc, cx + dx, cy), ink);
  free(doc.shape.snapshot);
  PASS();
}

static void test_filled_shape_is_solid_ink(void) {
  TEST("filled shapes are a single ink color with no outline");
  uint8_t pixels[64 * 64 * DOC_BPP] = {0};
  canvas_doc_t doc = {.pixels = pixels, .canvas_w = 64, .canvas_h = 64};
  uint32_t ink = MAKE_COLOR(10, 20, 30, 255), bg = MAKE_COLOR(200, 100, 50, 255);
  const int tools[] = {ID_TOOL_RECT, ID_TOOL_ELLIPSE, ID_TOOL_ROUNDED_RECT};
  for (int t = 0; t < ARRAY_LEN(tools); t++) {
    memset(pixels, 0, sizeof(pixels));
    canvas_shape_begin(&doc, 24, 24);
    canvas_shape_preview(&doc, 24, 24, 40, 36, tools[t], true, ink, bg, false);
    ASSERT_EQUAL(canvas_get_pixel(&doc, 24, 24), ink);
    ASSERT_EQUAL(canvas_get_pixel(&doc, 40, 24), ink);
    ASSERT_EQUAL(canvas_get_pixel(&doc, 24, 36), ink);
    for (int y = 0; y < 64; y++)
      for (int x = 0; x < 64; x++) {
        uint32_t pixel = canvas_get_pixel(&doc, x, y);
        ASSERT_TRUE(pixel == 0 || pixel == ink);
      }
  }
  free(doc.shape.snapshot);
  PASS();
}

static void test_gesture_anchor(void) {
  TEST("pinch/rotation preserves the document point beneath the moving centroid");
  canvas_doc_t doc = {.canvas_w = 1000, .canvas_h = 800};
  window_t win = {.frame = {0, 0, 640, 480}, .flags = WINDOW_NOTITLE};
  for (int retina = 1; retina <= 2; retina++) {
    g_bw_retina_scale = retina;
    window_view_init(&win, doc.canvas_w, doc.canvas_h, retina, true);
    window_view_set_zoom(&win, 1.25f, NULL);
    ipoint16_t before = window_client_to_content(&win, (ipoint16_t){260, 170});
    ax_gesture_t gesture = {AX_GESTURE_UPDATE, 280, 190, 260, 170, 1.7f, 0.8f};
    window_view_apply_gesture(&win, &gesture);
    ipoint16_t after = window_client_to_content(&win, (ipoint16_t){280, 190});
    ASSERT_TRUE(abs(before.x - after.x) <= 1);
    ASSERT_TRUE(abs(before.y - after.y) <= 1);
    ASSERT_TRUE(fabsf(window_view_zoom(&win) - 2.125f) < 0.0001f);
    ASSERT_FALSE(doc.modified);
    gesture = (ax_gesture_t){AX_GESTURE_UPDATE, 300, 200, 280, 190, 0.00001f, -1.2f};
    window_view_apply_gesture(&win, &gesture);
    ASSERT_TRUE(fabsf(window_view_zoom(&win) - 0.05f) < 0.0001f);
    after = window_client_to_content(&win, (ipoint16_t){300, 200});
    ASSERT_TRUE(abs(before.x - after.x) <= 1);
    ASSERT_TRUE(abs(before.y - after.y) <= 1);
  }
  g_bw_retina_scale = 1;
  PASS();
}

static void test_cancel_stroke(void) {
  TEST("second finger cancels pencil pixels without leaving an undo or redo entry");
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  g_app->current_tool = ID_TOOL_PENCIL;
  g_app->fg_color = MAKE_COLOR(0, 0, 0, 255);
  canvas_doc_t *doc = create_document(NULL, 64, 64);
  ASSERT_NOT_NULL(doc);
  doc->canvas_win->view.free_pan = true;
  ax_gesture_t rotation = {AX_GESTURE_UPDATE, 32, 32, 32, 32, 2, 1.57079632679f};
  window_view_apply_gesture(doc->canvas_win, &rotation);
  ipoint16_t point = window_content_to_client(doc->canvas_win, (ipoint16_t){20, 20});
  uint32_t original = canvas_get_pixel(doc, 20, 20);
  int undo_count = doc->undo.count;
  send_pointer_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(point.x, point.y), NULL);
  ASSERT_TRUE(doc->drawing);
  ASSERT_EQUAL(canvas_get_pixel(doc, 20, 20), g_app->fg_color);
  send_message(doc->canvas_win, evPointerCancel, 0, NULL);
  ASSERT_FALSE(doc->drawing);
  ASSERT_FALSE(doc->modified);
  ASSERT_EQUAL(canvas_get_pixel(doc, 20, 20), original);
  ASSERT_EQUAL(doc->undo.count, undo_count);
  ASSERT_EQUAL(doc->redo.count, 0);
  close_document(doc);
  test_env_shutdown();
  free(g_app); g_app = NULL;
  PASS();
}

int main(void) {
  TEST_START("Canvas gestures");
  test_screen_aligned_shapes();
  test_ellipse_drag_on_curve();
  test_filled_shape_is_solid_ink();
  test_gesture_anchor();
  test_cancel_stroke();
  TEST_END();
}
