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
  uint32_t ink = MAKE_COLOR(0, 0, 0, 255), fill = MAKE_COLOR(200, 100, 50, 255);
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
            canvas_shape_preview(&doc, 64, 64, 100, 100, tools[t], filled, ink, fill, false);
            canvas_shape_preview(&doc, 64, 64, 64 + (int)lroundf(c * 24 + s * 10),
                                 64 + (int)lroundf(-s * 24 + c * 10), tools[t], filled, ink, fill, shift);
            float min_x = 128, min_y = 128, max_x = -128, max_y = -128;
            for (int y = 0; y < 128; y++) {
              for (int x = 0; x < 128; x++) {
                if (!canvas_get_pixel(&doc, x, y)) continue;
                float sx = c * (x - 64) - s * (y - 64), sy = s * (x - 64) + c * (y - 64);
                min_x = MIN(min_x, sx); max_x = MAX(max_x, sx);
                min_y = MIN(min_y, sy); max_y = MAX(max_y, sy);
              }
            }
            int hw = shift ? 10 : 24;
            ASSERT_TRUE(fabsf(min_x + hw) <= 3 && fabsf(max_x - hw) <= 3);
            ASSERT_TRUE(fabsf(min_y + 10) <= 3 && fabsf(max_y - 10) <= 3);
            ASSERT_EQUAL(canvas_get_pixel(&doc, 64, 64), filled ? fill : 0);
            canvas_shape_commit(&doc);
            ASSERT_EQUAL(canvas_get_pixel(&doc, 64, 64), filled ? fill : 0);
          }
        }
      }
    }
  }
  free(doc.shape.snapshot);
  g_bw_retina_scale = 1;
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
  test_gesture_anchor();
  test_cancel_stroke();
  TEST_END();
}
