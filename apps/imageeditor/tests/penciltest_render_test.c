#define IMAGEEDITOR_BW 1
#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"
#include <orion/user/toolbar.h>
#include <time.h>
#include "anim_shortcut_helpers.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;
static int r8_uploads, color_uploads, frame_labels;
static uint32_t drawn_layers[IE_LAYER_COUNT];
static int drawn_layer_count;
static uint32_t render_r8(int w, int h, const void *pixels, R_TextureFilter filter, R_TextureWrap wrap) {
  return ++r8_uploads;
}
static bool render_update_r8(uint32_t tex, int x, int y, int w, int h, const void *pixels) { return true; }
static uint32_t render_color(int w, int h, const void *pixels, R_TextureFilter filter, R_TextureWrap wrap) {
  color_uploads++;
  return 1;
}
static void render_delete(uint32_t tex) { (void)tex; }
static void render_theme(theme_part_t part, irect16_t rect, ctrl_state_t state) { (void)part; }
static void render_text(ui_font_t font, const char *text, int x, int y, uint32_t color) { frame_labels++; }
static void render_indexed(uint32_t tex, irect16_t rect, const uint32_t palette[256], int transparent, float alpha) {
  if (drawn_layer_count < IE_LAYER_COUNT) drawn_layers[drawn_layer_count] = tex;
  drawn_layer_count++;
}
static void render_sprite(int tex, irect16_t rect, const frect_t *uv, uint32_t color, uint32_t flags) {
  render_indexed(tex, rect, NULL, 0, 1.0f);
}
#define R_CreateTextureR8 render_r8
#define R_UpdateTextureR8 render_update_r8
#define R_CreateTextureSRGBA8 render_color
#define R_DeleteTexture render_delete
#define theme_draw render_theme
#define draw_text render_text
#define draw_indexed_rect render_indexed
#define draw_sprite_region render_sprite

static void render_timeline(void) {
  toolbar_state_t *tb = window_toolbar_state(g_app->timeline_win);
  for (int i = 0; i < tb->item_count; i++) if (tb->items[i].type == TOOLBAR_ITEM_CUSTOM) {
    toolbar_draw_item_t draw = {tb->item_rects[i], 0, i};
    g_app->timeline_win->proc(g_app->timeline_win, tbDrawItem, tb->items[i].ident, &draw);
  }
}

static void test_indexed_render(void) {
  TEST("indexed canvas uploads byte planes; timeline paints numbers without RGBA previews");
  canvas_doc_t *doc = create_document(NULL, 64, 48); ASSERT_NOT_NULL(doc);
  toolbar_state_t *tb = window_toolbar_state(g_app->timeline_win);
  int frames = 0;
  for (int i = 0; i < tb->item_count; i++) if (tb->items[i].type == TOOLBAR_ITEM_CUSTOM) {
    frames++;
    ASSERT_TRUE(tb->items[i].state & TBSTATE_CHECKED);
    ASSERT_TRUE(tb->item_rects[i].w > 0);
  }
  ASSERT_EQUAL(frames, 1);
  render_timeline();
  ASSERT_EQUAL(frame_labels, 1);
  doc->pixels[3] = 128;
  for (int i = 1; i < 20; i++) cmd_frame_add(doc, true);
  r8_uploads = color_uploads = frame_labels = 0;
  canvas_upload(doc);
  ASSERT_EQUAL(r8_uploads, 4); ASSERT_EQUAL(color_uploads, 0);
  render_timeline();
  ASSERT_TRUE(frame_labels > 0); ASSERT_EQUAL(color_uploads, 0);
  for (int i = 0; i < 20; i++) {
    ASSERT_TRUE(cmd_frame_select(doc, i));
    render_timeline();
    ASSERT_EQUAL(doc->pixels[3], 128);
  }
  ASSERT_EQUAL(color_uploads, 0);
  close_document(doc); PASS();
}

static void test_layer_view(void) {
  TEST("canvas shows the selected stage and layers beneath it without changing export visibility");
  canvas_doc_t *doc = create_document(NULL, 64, 48); ASSERT_NOT_NULL(doc);
  ASSERT_EQUAL(doc->layer.active, IE_LAYER_PENCIL);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_BG]->pixels[0], 1);
  ASSERT_EQUAL(doc->ipal.entries[1], IE_PAPER_COLOR);
  drawn_layer_count = 0;
  send_message(doc->canvas_win, evPaint, 0, NULL);
  ASSERT_EQUAL(drawn_layer_count, 2);
  ASSERT_EQUAL(drawn_layers[0], doc->layer.stack[IE_LAYER_BG]->tex);
  ASSERT_EQUAL(drawn_layers[1], doc->layer.stack[IE_LAYER_PENCIL]->tex);
  const int stages[] = {IE_LAYER_FX, IE_LAYER_BG, IE_LAYER_COLOR, IE_LAYER_PENCIL};
  const int counts[] = {4, 1, 3, 2};
  const int order[] = {IE_LAYER_BG, IE_LAYER_PENCIL, IE_LAYER_COLOR, IE_LAYER_FX};
  for (int i = 0; i < 4; i++) {
    ASSERT_TRUE(cmd_pencil_layer(doc, stages[i]));
    drawn_layer_count = 0;
    send_message(doc->canvas_win, evPaint, 0, NULL);
    ASSERT_EQUAL(drawn_layer_count, counts[i]);
    for (int j = 0; j < counts[i]; j++)
      ASSERT_EQUAL(drawn_layers[j], doc->layer.stack[order[j]]->tex);
    for (int j = 0; j < IE_LAYER_COUNT; j++) ASSERT_TRUE(doc->layer.stack[j]->visible);
  }
  doc->layer.stack[IE_LAYER_PENCIL]->visible = false;
  drawn_layer_count = 0;
  send_message(doc->canvas_win, evPaint, 0, NULL);
  ASSERT_EQUAL(drawn_layer_count, 1);
  ASSERT_EQUAL(drawn_layers[0], doc->layer.stack[IE_LAYER_BG]->tex);
  close_document(doc); PASS();
}

static void test_frame_shortcuts(void) {
  TEST("canvas-focused frame shortcuts navigate, pause/resume and edit frames with undo");
  canvas_doc_t *doc = create_document(NULL, 64, 48); ASSERT_NOT_NULL(doc);
  set_focus(doc->canvas_win);
  doc->pixels[5] = 123;
  ASSERT_TRUE(test_anim_shortcut(AX_KEY_D, 0));
  ASSERT_EQUAL(doc->anim->frame_count, 2); ASSERT_EQUAL(doc->pixels[5], 123);
  ASSERT_TRUE(test_anim_shortcut(AX_KEY_N, 0));
  ASSERT_EQUAL(doc->anim->frame_count, 3); ASSERT_EQUAL(doc->pixels[5], 0);
  ASSERT_TRUE(test_anim_shortcut(AX_KEY_LEFTARROW, 0)); ASSERT_EQUAL(doc->anim->active_frame, 1);
  ASSERT_EQUAL(doc->pixels[5], 123);
  ASSERT_TRUE(test_anim_shortcut(AX_KEY_RIGHTARROW, 0)); ASSERT_EQUAL(doc->anim->active_frame, 2);
  ASSERT_TRUE(test_anim_shortcut(AX_KEY_HOME, 0)); ASSERT_EQUAL(doc->anim->active_frame, 0);
  ASSERT_TRUE(test_anim_shortcut(AX_KEY_SPACE, 0)); ASSERT_TRUE(doc->anim->playing);
  anim_tick(doc); ASSERT_EQUAL(doc->anim->active_frame, 1);
  ASSERT_TRUE(test_anim_shortcut(AX_KEY_SPACE, 0)); ASSERT_FALSE(doc->anim->playing);
  ASSERT_EQUAL(doc->anim->active_frame, 1); ASSERT_EQUAL(g_app->anim_timer_id, 0);
  anim_tick(doc); ASSERT_EQUAL(doc->anim->active_frame, 1);
  ASSERT_TRUE(test_anim_shortcut(AX_KEY_SPACE, 0)); ASSERT_TRUE(doc->anim->playing);
  ASSERT_EQUAL(doc->anim->playback_start_frame, 1);
  anim_tick(doc); ASSERT_EQUAL(doc->anim->active_frame, 2);
  ASSERT_TRUE(test_anim_shortcut(AX_KEY_SPACE, 0)); ASSERT_FALSE(doc->anim->playing);
  ASSERT_EQUAL(doc->anim->active_frame, 2);
  ASSERT_TRUE(test_anim_shortcut(AX_KEY_DEL, AX_MOD_SHIFT >> 16));
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  cmd_undo(doc); ASSERT_EQUAL(doc->anim->frame_count, 3); ASSERT_EQUAL(doc->anim->active_frame, 2);
  ASSERT_TRUE(test_anim_shortcut(AX_KEY_BACKSPACE, AX_MOD_SHIFT >> 16));
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  window_t *edit = create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 100, 20), doc->win,
                                 "TextBox", 0, NULL);
  ASSERT_NOT_NULL(edit);
  set_focus(edit);
  window_set_state(edit, WINDOW_STATE_EDITING, true);
  ASSERT_FALSE(test_anim_shortcut(AX_KEY_N, 0));
  ASSERT_FALSE(test_anim_shortcut(AX_KEY_D, 0));
  ASSERT_FALSE(test_anim_shortcut(AX_KEY_SPACE, 0));
  ASSERT_FALSE(test_anim_shortcut(AX_KEY_LEFTARROW, 0));
  ASSERT_FALSE(test_anim_shortcut(AX_KEY_DEL, AX_MOD_SHIFT >> 16));
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  destroy_window(edit);
  close_document(doc); PASS();
}

static void benchmark_example(const char *path) {
  TEST("full-resolution video opens and selects arbitrary frames without preview uploads");
  clock_t start = clock();
  ASSERT_TRUE(imageeditor_open_file_path(path));
  canvas_doc_t *doc = g_app->active_doc;
  render_timeline();
  double open_seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
  start = clock();
  for (int i = 0; i < 100; i++) {
    ASSERT_TRUE(cmd_frame_select(doc, (i * 73) % doc->anim->frame_count));
    render_timeline();
  }
  double switch_ms = (double)(clock() - start) * 1000 / CLOCKS_PER_SEC / 100;
  ASSERT_EQUAL(color_uploads, 0);
  printf("\n    %d frames at %dx%d: open %.3f s; select and timeline paint %.3f ms/frame\n",
         doc->anim->frame_count, doc->canvas_w, doc->canvas_h, open_seconds, switch_ms);
  close_document(doc); PASS();
}

int main(void) {
  TEST_START("Penciltest render work");
  test_env_init(); g_app = calloc(1, sizeof(*g_app));
  if (!g_app) return 1;
  g_app->current_tool = g_app->brush_tool = ID_TOOL_PENCIL;
  create_tool_palette_window();
  resize_window(g_app->chrome_win, 1080, 758 - window_screen_y(g_app->chrome_win));
  create_timeline_window();
  test_indexed_render();
  test_layer_view();
  test_frame_shortcuts();
  const char *example = getenv("PENCILTEST_RENDER_EXAMPLE");
  if (example) benchmark_example(example);
  while (g_app->docs) close_document(g_app->docs);
  free_accelerators(g_app->accel);
  free(g_app); g_app = NULL; test_env_shutdown();
  TEST_END();
}
