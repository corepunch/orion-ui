#define IMAGEEDITOR_BW 1
#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

// Exercise the iPad application layout against the headless host framework.
#define AX_PLATFORM_IOS 1

app_state_t *g_app;
int g_bw_retina_scale = 2;

static int penciltest_metrics(ui_system_metrics_t metric) {
  return metric == kSystemMetricScreenWidth ? 1080 : metric == kSystemMetricScreenHeight ? 758 : 0;
}
#define ui_get_system_metrics penciltest_metrics

static void penciltest_setup(void) {
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  g_app->current_tool = ID_TOOL_PENCIL;
  create_tool_palette_window();
  resize_window(g_app->chrome_win, 1080, 758 - window_screen_y(g_app->chrome_win));
  create_tool_options_window();
  create_timeline_window();
}

static void penciltest_teardown(void) {
  while (g_app->docs) close_document(g_app->docs);
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
}

static void test_ipad_options_keep_toolbar_geometry(void) {
  TEST("iPad display layout preserves options toolbar width and user position");
  penciltest_setup();
  canvas_doc_t *doc = create_document(NULL, CANVAS_W, CANVAS_H);
  ASSERT_NOT_NULL(doc);
  window_t *options = g_app->tool_options_win;
  ASSERT_EQUAL(options->frame.w, g_app->tool_win->frame.w);
  ASSERT_EQUAL(options->frame.x, TOOL_OPTIONS_WIN_X);
  ASSERT_EQUAL(options->frame.y, TOOL_OPTIONS_WIN_Y);
  move_window(options, 100, 160);
  send_message(doc->win, evDisplayChange, MAKEDWORD(1080, 758), NULL);
  send_message(options, evDisplayChange, MAKEDWORD(1080, 758), NULL);
  ASSERT_EQUAL(options->frame.x, 100);
  ASSERT_EQUAL(options->frame.y, 160);
  ASSERT_EQUAL(options->frame.w, PALETTE_WIN_W);
  move_window(options, 1000, 700);
  send_message(options, evDisplayChange, MAKEDWORD(758, 1080), NULL);
  ASSERT_TRUE(options->frame.x + options->frame.w <= 758);
  ASSERT_TRUE(options->frame.y + options->frame.h <= 1080);
  ASSERT_EQUAL(options->frame.w, PALETTE_WIN_W);
  penciltest_teardown();
  PASS();
}

static void test_pencil_canvas_extends_behind_timeline(void) {
  TEST("Pencil Test maximized canvas covers the client behind the timeline");
  penciltest_setup();
  canvas_doc_t *doc = create_document(NULL, CANVAS_W, CANVAS_H);
  ASSERT_NOT_NULL(doc);
  irect16_t override = R(0, 0, 1, 1);
  ASSERT_FALSE(send_message(doc->win, evGetWorkspaceRect, 0, &override));
  irect16_t area = imageeditor_document_workspace_rect();
  ASSERT_EQUAL(doc->win->frame.x, area.x);
  ASSERT_EQUAL(doc->win->frame.y, area.y);
  ASSERT_EQUAL(doc->win->frame.w, area.w);
  ASSERT_EQUAL(doc->win->frame.h, area.h);
  ASSERT_EQUAL(doc->canvas_w, area.w * g_bw_retina_scale);
  int visible_h = area.h;
  ASSERT_EQUAL(doc->canvas_h, visible_h * g_bw_retina_scale);
  frect_t bounds = window_view_bounds(doc->canvas_win);
  ASSERT_TRUE(fabsf(bounds.x) < 0.01f);
  ASSERT_TRUE(fabsf(bounds.y) < 0.01f);
  ASSERT_TRUE(fabsf(bounds.w - doc->canvas_win->frame.w) < 0.01f);
  ASSERT_TRUE(fabsf(bounds.h - visible_h) < 0.01f);
  ASSERT_EQUAL(area.x, g_app->tool_win->frame.w);
  ASSERT_EQUAL(area.y, window_screen_y(g_app->main_toolbar_win) + g_app->main_toolbar_win->frame.h);
  ASSERT_EQUAL(area.y + area.h, 758);
  ASSERT_TRUE(g_app->timeline_win->frame.y < doc->win->frame.y + doc->win->frame.h);
  canvas_doc_t *loaded = create_document("image.pcx", CANVAS_W, CANVAS_H);
  ASSERT_NOT_NULL(loaded);
  ASSERT_EQUAL(loaded->canvas_w, CANVAS_W * g_bw_retina_scale);
  ASSERT_EQUAL(loaded->canvas_h, CANVAS_H * g_bw_retina_scale);
  penciltest_teardown();
  PASS();
}

int main(void) {
  TEST_START("Pencil Test iPad layout");
  test_ipad_options_keep_toolbar_geometry();
  test_pencil_canvas_extends_behind_timeline();
  TEST_END();
}
