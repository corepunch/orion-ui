#ifndef __CANVAS_PAN_CASES_H__
#define __CANVAS_PAN_CASES_H__
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app;
#if IMAGEEDITOR_BW
int g_bw_retina_scale = 1;
#endif

static void pan_pointer(window_t *win, uint32_t msg, ipoint16_t point) {
  int x = (window_screen_x(win) + point.x) * UI_WINDOW_SCALE;
  int y = (window_screen_y(win) + point.y) * UI_WINDOW_SCALE;
  ui_event_t event = {.message = msg, .wParam = MAKEDWORD(x, y)};
  dispatch_message(&event);
}

static void test_middle_pan(void) {
  TEST("framework middle drag pans with drawing/navigation tools and leaves pixels/history unchanged");
  const int tools[] = {ID_TOOL_PENCIL, ID_TOOL_SELECT, ID_TOOL_ZOOM, ID_TOOL_EYEDROPPER, ID_TOOL_HAND};
  canvas_doc_t *doc = create_document(NULL, 128, 128);
  ASSERT_NOT_NULL(doc);
  window_t *win = doc->canvas_win;
  canvas_win_state_t *state = win->userdata;
  size_t size = (size_t)doc->canvas_w * doc->canvas_h * DOC_BPP;
  uint8_t *before = malloc(size);
  ASSERT_NOT_NULL(before);
  memcpy(before, doc->pixels, size);
  int undo_count = doc->undo.count;
  for (int ratio = 1; ratio <= 2; ratio++) {
    for (int zoom = 1; zoom <= 8; zoom *= 8) {
      for (int tool = 0; tool < ARRAY_LEN(tools); tool++) {
        g_app->current_tool = tools[tool];
        window_view_init(win, doc->canvas_w, doc->canvas_h, ratio, true);
        ax_gesture_t gesture = {AX_GESTURE_UPDATE, 32, 32, 32, 32, zoom, 1.57079632679f};
        window_view_apply_gesture(win, &gesture);
        float tx = win->view->matrix.tx, ty = win->view->matrix.ty;
        pan_pointer(win, kEventOtherButtonDown, (ipoint16_t){32, 32});
        ASSERT_TRUE(state->gesture_active);
        ASSERT_FALSE(state->pan.active);
        ASSERT_TRUE(g_ui_runtime.captured == win);
        pan_pointer(win, kEventLeftButtonDown, (ipoint16_t){32, 32});
        pan_pointer(win, kEventLeftButtonUp, (ipoint16_t){32, 32});
        pan_pointer(win, kEventRightButtonUp, (ipoint16_t){32, 32});
        ASSERT_TRUE(state->gesture_active);
        ASSERT_TRUE(g_ui_runtime.captured == win);
        pan_pointer(win, kEventOtherButtonDragged, (ipoint16_t){48, 56});
        ASSERT_TRUE(fabsf(win->view->matrix.tx - tx - 16) < 0.01f);
        ASSERT_TRUE(fabsf(win->view->matrix.ty - ty - 24) < 0.01f);
        pan_pointer(win, kEventOtherButtonDragged, (ipoint16_t){-16, -24});
        ASSERT_TRUE(fabsf(win->view->matrix.tx - tx + 48) < 0.01f);
        ASSERT_TRUE(fabsf(win->view->matrix.ty - ty + 56) < 0.01f);
        pan_pointer(win, kEventOtherButtonUp, (ipoint16_t){-16, -24});
        ASSERT_FALSE(state->gesture_active);
        ASSERT_NULL(g_ui_runtime.captured);
        ASSERT_EQUAL(g_app->current_tool, tools[tool]);
        ASSERT_FALSE(doc->drawing);
        ASSERT_FALSE(doc->modified);
        ASSERT_NULL(doc->command.before);
        ASSERT_EQUAL(doc->undo.count, undo_count);
        ASSERT_EQUAL(doc->redo.count, 0);
        ASSERT_TRUE(memcmp(before, doc->pixels, size) == 0);
      }
    }
  }
  free(before);
  close_document(doc);
  PASS();
}

static void test_pan_release_and_cancel(void) {
  TEST("Hand drag ends only on left release; pointer cancellation ends middle drag");
  canvas_doc_t *doc = create_document(NULL, 128, 128);
  ASSERT_NOT_NULL(doc);
  window_t *win = doc->canvas_win;
  canvas_win_state_t *state = win->userdata;
  g_app->current_tool = ID_TOOL_HAND;
  pan_pointer(win, kEventLeftButtonDown, (ipoint16_t){32, 32});
  ASSERT_TRUE(state->pan.active);
  pan_pointer(win, kEventOtherButtonUp, (ipoint16_t){32, 32});
  ASSERT_TRUE(state->pan.active);
  ASSERT_TRUE(g_ui_runtime.captured == win);
  pan_pointer(win, kEventLeftButtonUp, (ipoint16_t){32, 32});
  ASSERT_FALSE(state->pan.active);
  ASSERT_NULL(g_ui_runtime.captured);
  g_app->current_tool = ID_TOOL_PENCIL;
  pan_pointer(win, kEventOtherButtonDown, (ipoint16_t){32, 32});
  ASSERT_TRUE(state->gesture_active);
  ui_event_t cancel = {.message = kEventPointerCancel};
  dispatch_message(&cancel);
  ASSERT_FALSE(state->gesture_active);
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_FALSE(doc->modified);
  close_document(doc);
  PASS();
}

int main(void) {
  TEST_START("Canvas mouse panning");
  void (*cases[])(void) = {test_middle_pan, test_pan_release_and_cancel};
  for (int i = 0; i < ARRAY_LEN(cases); i++) {
    test_env_init();
    g_app = calloc(1, sizeof(*g_app));
    g_app->fg_color = MAKE_COLOR(0, 0, 0, 255);
    cases[i]();
    test_env_shutdown();
    free(g_app); g_app = NULL;
  }
  TEST_END();
}
#endif
