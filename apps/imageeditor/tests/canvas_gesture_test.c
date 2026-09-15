#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;

static void test_gesture_anchor(void) {
  TEST("pinch/rotation preserves the document point beneath the moving centroid");
  canvas_doc_t doc = {.canvas_w = 1000, .canvas_h = 800};
  window_t win = {.frame = {0, 0, 640, 480}};
  for (int retina = 1; retina <= 2; retina++) {
    g_bw_retina_scale = retina;
    canvas_win_state_t state = {.doc = &doc, .scale = 1.25f, .rotation = 0.3f,
                                .translate_x = 21, .translate_y = -15, .pan = {.x = 17, .y = 31}};
    int before_x, before_y, after_x, after_y;
    canvas_view_to_doc(&win, &state, 260, 170, &before_x, &before_y);
    ax_gesture_t gesture = {AX_GESTURE_UPDATE, 280, 190, 260, 170, 1.7f, 0.8f};
    canvas_apply_gesture(&win, &state, &gesture);
    canvas_view_to_doc(&win, &state, 280, 190, &after_x, &after_y);
    ASSERT_EQUAL(before_x, after_x);
    ASSERT_EQUAL(before_y, after_y);
    ASSERT_TRUE(fabsf(state.scale - 2.125f) < 0.0001f);
    ASSERT_FALSE(doc.modified);
    float x = 97.25f, y = -23.75f;
    canvas_transform_point(&win, &state, &x, &y, false);
    canvas_transform_point(&win, &state, &x, &y, true);
    ASSERT_TRUE(fabsf(x - 97.25f) < 0.0001f);
    ASSERT_TRUE(fabsf(y + 23.75f) < 0.0001f);
    gesture = (ax_gesture_t){AX_GESTURE_UPDATE, 300, 200, 280, 190, 0.00001f, -1.2f};
    canvas_apply_gesture(&win, &state, &gesture);
    ASSERT_TRUE(fabsf(state.scale - 0.05f) < 0.0001f);
    canvas_view_to_doc(&win, &state, 300, 200, &after_x, &after_y);
    ASSERT_TRUE(abs(before_x - after_x) <= 1);
    ASSERT_TRUE(abs(before_y - after_y) <= 1);
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
  canvas_win_state_t *state = doc->canvas_win->userdata;
  int x, y;
  canvas_doc_to_view(doc->canvas_win, state, 20, 20, &x, &y);
  uint32_t original = canvas_get_pixel(doc, 20, 20);
  int undo_count = doc->undo.count;
  send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(x, y), NULL);
  ASSERT_TRUE(doc->drawing);
  send_message(doc->canvas_win, evPointerCancel, MAKEDWORD(x, y), NULL);
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
