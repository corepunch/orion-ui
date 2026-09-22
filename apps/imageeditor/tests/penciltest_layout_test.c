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

static void penciltest_default_size(int *out_w, int *out_h) {
  int w = CANVAS_W, h = CANVAS_H;
  imageeditor_default_canvas_size(&w, &h);
  if (out_w) *out_w = w;
  if (out_h) *out_h = h;
}

static void test_ipad_options_keep_toolbar_geometry(void) {
  TEST("iPad display layout preserves options toolbar width and user position");
  penciltest_setup();
  int w, h;
  penciltest_default_size(&w, &h);
  canvas_doc_t *doc = create_document(NULL, w, h);
  ASSERT_NOT_NULL(doc);
  window_t *options = g_app->tool_options_win;
  ASSERT_EQUAL(options->frame.w, PALETTE_WIN_W);
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
  int w, h;
  penciltest_default_size(&w, &h);
  ASSERT_EQUAL(w, imageeditor_document_workspace_rect().w);
  ASSERT_EQUAL(h, imageeditor_document_workspace_rect().h);
  ASSERT_TRUE(w != CANVAS_W || h != CANVAS_H);
  canvas_doc_t *doc = create_document(NULL, w, h);
  ASSERT_NOT_NULL(doc);
  irect16_t area = imageeditor_document_workspace_rect();
  irect16_t override = R(0, 0, 1, 1);
  ASSERT_TRUE(send_message(doc->win, evGetWorkspaceRect, 0, &override));
  ASSERT_EQUAL(override.x, area.x);
  ASSERT_EQUAL(override.y, area.y);
  ASSERT_EQUAL(override.w, area.w);
  ASSERT_EQUAL(override.h, area.h);
  ASSERT_TRUE(doc->win->maximized);
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

static void test_pencil_paper_and_ink(void) {
  TEST("Pencil Test paper is cool off-white and ink is dark purple, not brown/black");
  penciltest_setup();
  g_app->fg_color = IE_INK_COLOR;
  g_app->bg_color = IE_PAPER_COLOR;
  canvas_doc_t *doc = create_document(NULL, 64, 64);
  ASSERT_NOT_NULL(doc);
  ASSERT_EQUAL(doc->background.color, IE_PAPER_COLOR);
  ASSERT_EQUAL(doc->ipal.entries[1], IE_INK_COLOR);
  ASSERT_EQUAL(doc->ipal.entries[2], IE_PAPER_COLOR);
  ASSERT_NOT_EQUAL(IE_INK_COLOR, MAKE_COLOR(0x00, 0x00, 0x00, 0xFF));
  ASSERT_NOT_EQUAL(IE_PAPER_COLOR, MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF));
  // Purple, not brown: G is lowest, B leads R, and chroma is visible in a stroke.
  ASSERT_TRUE(COLOR_G(IE_INK_COLOR) < COLOR_R(IE_INK_COLOR));
  ASSERT_TRUE(COLOR_R(IE_INK_COLOR) < COLOR_B(IE_INK_COLOR));
  ASSERT_TRUE((int)COLOR_B(IE_INK_COLOR) - (int)COLOR_G(IE_INK_COLOR) >= 40);
  canvas_set_pixel(doc, 4, 4, g_app->fg_color);
  ASSERT_EQUAL(canvas_get_pixel(doc, 4, 4), IE_INK_COLOR);
  penciltest_teardown();
  PASS();
}

static void test_onion_tint_is_not_gray(void) {
  TEST("onion-skin replaces black with blue/pink and keeps coverage");
  uint8_t prev[12] = {
    0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x80, 0xFB, 0xFC, 0xFF, 0xFF
  };
  uint8_t next[8] = { 0x00, 0x00, 0x00, 0xFF, 0xFB, 0xFC, 0xFF, 0xFF };
  anim_onion_tint_rgba(prev, 3, IE_ONION_PREV_COLOR);
  anim_onion_tint_rgba(next, 2, IE_ONION_NEXT_COLOR);
  ASSERT_EQUAL(prev[0], COLOR_R(IE_ONION_PREV_COLOR));
  ASSERT_EQUAL(prev[1], COLOR_G(IE_ONION_PREV_COLOR));
  ASSERT_EQUAL(prev[2], COLOR_B(IE_ONION_PREV_COLOR));
  ASSERT_EQUAL(prev[3], 0xFF);
  ASSERT_EQUAL(prev[4], COLOR_R(IE_ONION_PREV_COLOR));
  ASSERT_EQUAL(prev[7], 0x80);
  ASSERT_EQUAL(prev[11], 0);
  ASSERT_EQUAL(next[0], COLOR_R(IE_ONION_NEXT_COLOR));
  ASSERT_EQUAL(next[1], COLOR_G(IE_ONION_NEXT_COLOR));
  ASSERT_EQUAL(next[2], COLOR_B(IE_ONION_NEXT_COLOR));
  ASSERT_EQUAL(next[3], 0xFF);
  ASSERT_EQUAL(next[7], 0);
  PASS();
}

static void test_swatch_tap_swaps_colors(void) {
  TEST("Pencil Test swatch tap swaps foreground and background");
  penciltest_setup();
  g_app->fg_color = MAKE_COLOR(0x00, 0x00, 0x00, 0xFF);
  g_app->bg_color = MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF);
  window_t *tools = g_app->tool_win;
  ASSERT_NOT_NULL(tools);
  ASSERT_NOT_NULL(tools->toolbar);
  toolbar_state_t *tb = window_toolbar_state(tools);
  ASSERT_NOT_NULL(tb);
  int swatch = -1;
  for (int i = 0; i < tb->item_count; i++)
    if (tb->items[i].type == TOOLBAR_ITEM_CUSTOM && tb->items[i].ident >= IE_PENCIL_PALETTE_BASE + IE_PENCIL_COLORS) swatch = i;
  ASSERT_TRUE(swatch >= 0);
  irect16_t r = tb->item_rects[swatch];
  uint32_t pt = MAKEDWORD(r.x + r.w / 2, r.y + r.h / 2);
  send_message(tools->toolbar, evLeftButtonDown, pt, NULL);
  send_message(tools->toolbar, evLeftButtonUp, pt, NULL);
  ASSERT_EQUAL(g_app->fg_color, MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF));
  ASSERT_EQUAL(g_app->bg_color, MAKE_COLOR(0x00, 0x00, 0x00, 0xFF));
  penciltest_teardown();
  PASS();
}

static void test_coloring_palette(void) {
  TEST("single-column half-size palette selects 16 colors; drawing survives frame switches and undo");
  penciltest_setup();
  canvas_doc_t *doc = create_document(NULL, 64, 64);
  ASSERT_NOT_NULL(doc);
  ASSERT_EQUAL(doc->ipal.count, IE_PENCIL_COLORS + 1);
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_COLOR));
  toolbar_state_t *tb = window_toolbar_state(g_app->tool_win);
  ASSERT_EQUAL(tb->columns, 1);
  ASSERT_EQUAL(g_app->tool_win->frame.w, PALETTE_WIN_W);
  int mini = (TOOL_PALETTE_BTN_SIZE - TOOLBAR_SPACING) / 2;
  for (int i = 0; i < 4; i++) {
    ASSERT_TRUE(tb->items[i].flags & TOOLBAR_ITEM_FLAG_SMALL);
    ASSERT_EQUAL(tb->item_rects[i].w, mini);
    ASSERT_EQUAL(tb->item_rects[i].h, mini);
  }
  ASSERT_EQUAL(tb->item_rects[0].y, tb->item_rects[1].y);
  ASSERT_EQUAL(tb->item_rects[2].y, tb->item_rects[3].y);
  ASSERT_TRUE(tb->item_rects[1].x > tb->item_rects[0].x);
  ASSERT_EQUAL(tb->item_rects[3].y + tb->item_rects[3].h - tb->item_rects[0].y,
               2 * mini + TOOLBAR_SPACING);
  ASSERT_TRUE(2 * mini + TOOLBAR_SPACING <= TOOL_PALETTE_BTN_SIZE);
  int found = 0;
  irect16_t previous = {0};
  for (int i = 0; i < tb->item_count; i++) {
    ASSERT_TRUE(tb->items[i].ident != ID_TOOL_TEXT && tb->items[i].ident != ID_TOOL_CROP &&
                tb->items[i].ident != ID_TOOL_MAGIC_WAND && tb->items[i].ident != ID_TOOL_MAGNIFIER);
    int swatch = tb->items[i].ident - IE_PENCIL_PALETTE_BASE;
    if (swatch < 0 || swatch >= IE_PENCIL_COLORS) continue;
    irect16_t r = tb->item_rects[i];
    ASSERT_EQUAL(r.w, mini);
    ASSERT_EQUAL(r.h, mini);
    ASSERT_TRUE(r.x + r.w <= g_app->tool_win->frame.w);
    ASSERT_TRUE(r.y + r.h <= g_app->tool_win->frame.h);
    if (swatch % 2) {
      ASSERT_EQUAL(r.y, previous.y);
      ASSERT_TRUE(r.x > previous.x + previous.w);
    }
    uint32_t point = MAKEDWORD(r.x + r.w / 2, r.y + r.h / 2);
    send_message(g_app->tool_win->toolbar, evLeftButtonDown, point, NULL);
    send_message(g_app->tool_win->toolbar, evLeftButtonUp, point, NULL);
    ASSERT_EQUAL(g_app->fg_color, k_pencil_palette[swatch]);
    ASSERT_EQUAL(doc->ipal.entries[g_app->fg_palette_idx], k_pencil_palette[swatch]);
    ASSERT_TRUE(ie_doc_begin_op(doc, "Color Stroke"));
    canvas_set_pixel(doc, swatch, 0, g_app->fg_color);
    ie_doc_commit_op(doc, true);
    previous = r;
    found++;
  }
  ASSERT_EQUAL(found, IE_PENCIL_COLORS);
  cmd_frame_add(doc, false);
  ASSERT_TRUE(cmd_frame_select(doc, 0));
  for (int i = 0; i < IE_PENCIL_COLORS; i++) ASSERT_EQUAL(canvas_get_pixel(doc, i, 0), k_pencil_palette[i]);
  ASSERT_TRUE(cmd_pencil_color(doc, 4));
  ASSERT_TRUE(ie_doc_begin_op(doc, "Color Fill"));
  canvas_flood_fill(doc, 0, 1, g_app->fg_color);
  ie_doc_commit_op(doc, true);
  ASSERT_EQUAL(canvas_get_pixel(doc, 0, 1), k_pencil_palette[4]);
  cmd_undo(doc);
  ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 0, 1)), 0);
  cmd_redo(doc);
  ASSERT_EQUAL(canvas_get_pixel(doc, 0, 1), k_pencil_palette[4]);
  penciltest_teardown();
  PASS();
}

static void test_legacy_palette_extension(void) {
  TEST("legacy palette gains colors without recoloring stored frames; extension is undoable");
  penciltest_setup();
  canvas_doc_t *doc = create_document(NULL, 8, 8);
  ASSERT_NOT_NULL(doc);
  memset(doc->ipal.entries + 3, 0, 253 * sizeof(uint32_t));
  doc->ipal.count = 256; // FLC reader retains the full palette, including unused entries.
  doc->pixels[0] = 3;
  cmd_frame_add(doc, false);
  ASSERT_TRUE(cmd_pencil_color(doc, 4));
  ASSERT_EQUAL(g_app->fg_palette_idx, 4); // Slot 3 is still referenced by frame 0.
  ASSERT_EQUAL(doc->ipal.entries[3], 0);
  ASSERT_EQUAL(doc->ipal.entries[4], k_pencil_palette[4]);
  cmd_undo(doc);
  ASSERT_EQUAL(doc->ipal.entries[4], 0);
  cmd_redo(doc);
  ASSERT_EQUAL(doc->ipal.entries[4], k_pencil_palette[4]);
  ASSERT_TRUE(cmd_frame_select(doc, 0));
  ASSERT_EQUAL(doc->pixels[0], 3);
  penciltest_teardown();
  PASS();
}

int main(void) {
  TEST_START("Pencil Test iPad layout");
  test_ipad_options_keep_toolbar_geometry();
  test_pencil_canvas_extends_behind_timeline();
  test_pencil_paper_and_ink();
  test_onion_tint_is_not_gray();
  test_swatch_tap_swaps_colors();
  test_coloring_palette();
  test_legacy_palette_extension();
  TEST_END();
}
