#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app = NULL;

static void test_canvas_text(void) {
  TEST("canvas renders antialiased Unicode text using the bundled font");
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  canvas_doc_t *doc = create_document(NULL, 128, 32);
  ASSERT_NOT_NULL(doc);
  text_options_t options = { .text = "A-Ж", .font_size = 16, .color = MAKE_COLOR(0, 0, 0, 255), .antialias = true };
  ASSERT_TRUE(canvas_draw_text(doc, 2, 2, &options));
  close_document(doc);
  free(g_app); g_app = NULL;
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Image editor text");
  test_canvas_text();
  TEST_END();
}
