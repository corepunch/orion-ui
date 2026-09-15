#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;

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
  test_gesture_anchor();
  test_cancel_stroke();
  TEST_END();
}
