#define AX_PLATFORM_IOS 1
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app;

static void ie_layout_setup(void) {
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  g_app->current_tool = ID_TOOL_PENCIL;
  g_app->fg_color = MAKE_COLOR(0x00, 0x00, 0x00, 0xFF);
  g_app->bg_color = MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF);
  create_tool_palette_window();
  create_color_palette_window();
  create_layers_window();
}

static void ie_layout_teardown(void) {
  while (g_app && g_app->docs) close_document(g_app->docs);
  test_env_shutdown();
  free(g_app);
  g_app = NULL;
}

static void test_ipad_default_canvas_fills_client(void) {
  TEST("Image Editor default canvas fills the iPad document client");
  ie_layout_setup();
  int w = CANVAS_W, h = CANVAS_H;
  imageeditor_default_canvas_size(&w, &h);
  ASSERT_EQUAL(w, imageeditor_document_workspace_rect().w);
  ASSERT_EQUAL(h, imageeditor_document_workspace_rect().h);
  ASSERT_TRUE(w != CANVAS_W || h != CANVAS_H);
  canvas_doc_t *doc = create_document(NULL, w, h);
  ASSERT_NOT_NULL(doc);
  ASSERT_TRUE(doc->win->maximized);
  irect16_t area = imageeditor_document_workspace_rect();
  ASSERT_EQUAL(doc->win->frame.x, area.x);
  ASSERT_EQUAL(doc->win->frame.y, area.y);
  ASSERT_EQUAL(doc->win->frame.w, area.w);
  ASSERT_EQUAL(doc->win->frame.h, area.h);
  irect16_t cr = get_client_rect(doc->win);
  ASSERT_TRUE(cr.w > 0 && cr.h > 0);
  ASSERT_EQUAL(doc->canvas_win->frame.w, cr.w);
  ASSERT_EQUAL(doc->canvas_win->frame.h, cr.h);
  ASSERT_EQUAL(doc->canvas_w, cr.w);
  ASSERT_EQUAL(doc->canvas_h, cr.h);
  ASSERT_EQUAL(doc->canvas_w, w);
  ASSERT_EQUAL(doc->canvas_h, h);
  ASSERT_EQUAL(doc->background.color, MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF));
  ASSERT_TRUE(doc->background.show);
  frect_t bounds = window_view_bounds(doc->canvas_win);
  ASSERT_TRUE(fabsf(bounds.x) < 0.01f);
  ASSERT_TRUE(fabsf(bounds.y) < 0.01f);
  ASSERT_TRUE(fabsf(bounds.w - (float)cr.w) < 0.01f);
  ASSERT_TRUE(fabsf(bounds.h - (float)cr.h) < 0.01f);
  canvas_doc_t *vga = create_document(NULL, CANVAS_W, CANVAS_H);
  ASSERT_NOT_NULL(vga);
  ASSERT_EQUAL(vga->canvas_w, CANVAS_W);
  ASSERT_EQUAL(vga->canvas_h, CANVAS_H);
  canvas_doc_t *custom = create_document(NULL, 320, 200);
  ASSERT_NOT_NULL(custom);
  ASSERT_EQUAL(custom->canvas_w, 320);
  ASSERT_EQUAL(custom->canvas_h, 200);
  ie_layout_teardown();
  PASS();
}

int main(void) {
  TEST_START("Image Editor iPad layout");
  test_ipad_default_canvas_fills_client();
  TEST_END();
}
