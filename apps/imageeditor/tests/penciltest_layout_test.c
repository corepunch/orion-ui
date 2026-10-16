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
  ASSERT_FALSE(window_has_state(options, WINDOW_STATE_VISIBLE));
  handle_menu_command(ID_TOOL_RECT);
  ASSERT_TRUE(window_has_state(options, WINDOW_STATE_VISIBLE));
  ASSERT_EQUAL(options->frame.w, PALETTE_WIN_W);
  ASSERT_EQUAL(options->frame.w, g_app->tool_win->frame.w);
  ASSERT_EQUAL(options->frame.x - (g_app->tool_win->frame.x + g_app->tool_win->frame.w), DOCK_FLOAT_GAP);
  ASSERT_EQUAL(window_screen_y(options), TOOL_OPTIONS_WIN_Y);
  handle_menu_command(ID_TOOL_ELLIPSE);
  ASSERT_EQUAL(options->frame.x - g_app->tool_win->frame.w, DOCK_FLOAT_GAP);
  send_message(g_app->chrome_win, evDisplayChange, MAKEDWORD(758, 1080), NULL);
  ASSERT_NULL(g_app->timeline_win);
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_COLOR));
  window_t *colors = g_app->color_win;
  irect16_t area = dock_content_rect(g_app->chrome_win);
  ASSERT_EQUAL(area.x + area.w - (colors->frame.x + colors->frame.w), DOCK_FLOAT_GAP);
  ASSERT_EQUAL(colors->frame.y - area.y, DOCK_FLOAT_GAP);
  ASSERT_EQUAL(options->frame.x - g_app->tool_win->frame.w, DOCK_FLOAT_GAP);
  send_message(g_app->chrome_win, evDisplayChange, MAKEDWORD(1080, 758), NULL);
  ASSERT_TRUE(dock_float(options, R(100, 160, options->frame.w, options->frame.h)));
  send_message(doc->win, evDisplayChange, MAKEDWORD(1080, 758), NULL);
  send_message(options, evDisplayChange, MAKEDWORD(1080, 758), NULL);
  ASSERT_EQUAL(options->frame.x, 100);
  ASSERT_EQUAL(options->frame.y, 160);
  ASSERT_EQUAL(options->frame.w, PALETTE_WIN_W);
  ASSERT_TRUE(dock_float(options, R(1000, 700, options->frame.w, options->frame.h)));
  send_message(g_app->chrome_win, evDisplayChange, MAKEDWORD(758, 1080), NULL);
  ASSERT_TRUE(options->frame.x + options->frame.w <= 758);
  ASSERT_TRUE(options->frame.y + options->frame.h <= 1080);
  ASSERT_EQUAL(options->frame.w, PALETTE_WIN_W);
  penciltest_teardown();
  PASS();
}

static void test_layer_tools_and_size(void) {
  TEST("Pencil layer uses pencil; painting layers use brush with toolbar size and no drawing popup");
  penciltest_setup();
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  const int layers[] = {IE_LAYER_PENCIL, IE_LAYER_BG, IE_LAYER_COLOR, IE_LAYER_FX, IE_LAYER_PENCIL};
  for (int i = 0; i < ARRAY_LEN(layers); i++) {
    ASSERT_TRUE(cmd_pencil_layer(doc, layers[i]));
    int tool = layers[i] == IE_LAYER_PENCIL ? ID_TOOL_PENCIL : ID_TOOL_BRUSH;
    ASSERT_EQUAL(g_app->current_tool, tool);
    ASSERT_FALSE(window_has_state(g_app->tool_options_win, WINDOW_STATE_VISIBLE));
    toolbar_state_t *tb = window_toolbar_state(g_app->tool_win);
    bool found = false;
    for (int j = 0; j < tb->item_count; j++) {
      ASSERT_TRUE(tb->items[j].ident != ID_TOOL_SPRAY);
      ASSERT_TRUE(tb->items[j].ident != (tool == ID_TOOL_PENCIL ? ID_TOOL_BRUSH : ID_TOOL_PENCIL));
      if (tb->items[j].ident == tool) {
        found = true;
        ASSERT_TRUE(tb->items[j].state & TBSTATE_CHECKED);
        if (tool == ID_TOOL_BRUSH) {
          ASSERT_STR_EQUAL(tb->items[j].icon, "phosphor-circle-fill");
          ASSERT_EQUAL(tb->items[j].color, g_app->fg_color);
        }
      }
    }
    ASSERT_TRUE(found);
    window_t *slider = get_window_item(g_app->tool_win, IE_OPT_SIZE);
    ASSERT_NOT_NULL(slider);
    ASSERT_TRUE(slider->flags & SLIDER_VERTICAL);
    ASSERT_EQUAL((int)send_message(slider, slGetPos, 0, NULL), i ? NUM_BRUSH_SIZES - 1 : 0);
    send_message(slider, evLeftButtonDown, MAKEDWORD(slider->frame.w / 2, slider->frame.h - SLIDER_TRACK_PAD), NULL);
    send_message(slider, evMouseMove, MAKEDWORD(slider->frame.w / 2, SLIDER_TRACK_PAD), NULL);
    send_message(slider, evLeftButtonUp, 0, NULL);
    ASSERT_EQUAL(g_app->brush_size, NUM_BRUSH_SIZES - 1);
    handle_menu_command(ID_TOOL_SPRAY);
    ASSERT_EQUAL(g_app->current_tool, tool);
    handle_menu_command(tool == ID_TOOL_PENCIL ? ID_TOOL_BRUSH : ID_TOOL_PENCIL);
    ASSERT_EQUAL(g_app->current_tool, tool);
    if (tool == ID_TOOL_BRUSH) {
      ASSERT_TRUE(cmd_pencil_color(doc, 4));
      toolbar_button_info_t info = {.mask = TBIF_COLOR, .ident = ID_TOOL_BRUSH};
      ASSERT_TRUE(send_message(g_app->tool_win, tbGetButtonInfo, 0, &info));
      ASSERT_EQUAL(info.color, doc->ipal.entries[4]);
    }
  }
  penciltest_teardown(); PASS();
}

static void test_pencil_canvas_without_timeline(void) {
  TEST("Pencil Test uses the full canvas height with frame controls in the left sidebar");
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
  ASSERT_TRUE(window_is_maximized(doc->win));
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
  ASSERT_NULL(g_app->timeline_win);
  ASSERT_NULL(create_timeline_window());
  ASSERT_EQUAL(area.y + area.h, 758);
  toolbar_state_t *tb = window_toolbar_state(g_app->tool_win);
  for (int i = 0; i < tb->item_count; i++) {
    ASSERT_FALSE(tb->items[i].style & TOOLBAR_ITEM_FLAG_REORDERABLE);
    ASSERT_TRUE(tb->item_rects[i].x + tb->item_rects[i].w <= g_app->tool_win->frame.w);
    ASSERT_TRUE(tb->item_rects[i].y + tb->item_rects[i].h <= g_app->tool_win->frame.h);
    if ((tb->items[i].style & CONTROL_SIZE_MASK) == CONTROL_SIZE_SMALL) {
      irect16_t r = tb->item_rects[i];
      ASSERT_TRUE(abs(r.x - (g_app->tool_win->frame.w - r.x - r.w)) <= 1);
    }
  }
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_COLOR));
  ASSERT_TRUE(dock_is_floating(g_app->color_win));
  ASSERT_EQUAL(doc->win->frame.y + doc->win->frame.h, 758);
  ASSERT_EQUAL(imageeditor_document_workspace_rect().h, area.h);
  canvas_doc_t *loaded = create_document("image.pcx", CANVAS_W, CANVAS_H);
  ASSERT_NOT_NULL(loaded);
  ASSERT_EQUAL(loaded->canvas_w, CANVAS_W * g_bw_retina_scale);
  ASSERT_EQUAL(loaded->canvas_h, CANVAS_H * g_bw_retina_scale);
  penciltest_teardown();
  PASS();
}

static bool penciltest_click_frame_control(int ident) {
  irect16_t r = {0};
  if (!send_message(g_app->tool_win, tbGetItemRect, ident, &r)) return false;
  uint32_t point = MAKEDWORD(r.x + r.w / 2, r.y + r.h / 2);
  send_message(g_app->tool_win->toolbar, evLeftButtonDown, point, NULL);
  send_message(g_app->tool_win->toolbar, evLeftButtonUp, point, NULL);
  return true;
}

static void test_sidebar_frame_controls(void) {
  TEST("sidebar frame buttons navigate and edit; playback ticks without a timeline window");
  penciltest_setup();
  ASSERT_TRUE(send_message(g_app->tool_win, tbGetState, ID_ANIM_PLAY, NULL) & TBSTATE_DISABLED);
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  ASSERT_TRUE(send_message(g_app->tool_win, tbGetState, ID_ANIM_PREV_FRAME, NULL) & TBSTATE_DISABLED);
  ASSERT_TRUE(send_message(g_app->tool_win, tbGetState, ID_ANIM_NEXT_FRAME, NULL) & TBSTATE_DISABLED);
  ASSERT_TRUE(penciltest_click_frame_control(ID_ANIM_NEW_FRAME));
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  ASSERT_EQUAL(doc->anim->active_frame, 1);
  ASSERT_TRUE(penciltest_click_frame_control(ID_ANIM_DUPLICATE_FRAME));
  ASSERT_EQUAL(doc->anim->frame_count, 3);
  ASSERT_TRUE(penciltest_click_frame_control(ID_ANIM_PREV_FRAME));
  ASSERT_EQUAL(doc->anim->active_frame, 1);
  ASSERT_TRUE(penciltest_click_frame_control(ID_ANIM_NEXT_FRAME));
  ASSERT_EQUAL(doc->anim->active_frame, 2);
  ASSERT_TRUE(penciltest_click_frame_control(ID_ANIM_PREV_FRAME));
  doc->anim->frames[2].delay_ms = doc->anim->frames[1].delay_ms + 20;
  ASSERT_TRUE(penciltest_click_frame_control(ID_ANIM_PLAY));
  ASSERT_TRUE(doc->anim->playing);
  ASSERT_TRUE(g_app->anim_timer_id != 0);
  ASSERT_TRUE(imageeditor_animation_timer_window() == g_app->tool_win);
  send_message(g_app->tool_win, evTimer, 0, NULL);
  ASSERT_EQUAL(doc->anim->active_frame, 2);
  ASSERT_TRUE(doc->anim->playing);
  ASSERT_TRUE(g_app->anim_timer_id != 0);
  ASSERT_TRUE(penciltest_click_frame_control(ID_ANIM_PLAY));
  ASSERT_FALSE(doc->anim->playing);
  ASSERT_EQUAL(g_app->anim_timer_id, 0);
  bool trace = g_app->anim_trace_enabled;
  ASSERT_TRUE(penciltest_click_frame_control(ID_ANIM_TRACE));
  ASSERT_EQUAL(g_app->anim_trace_enabled, !trace);
  ASSERT_TRUE(penciltest_click_frame_control(ID_ANIM_DELETE_FRAME));
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  cmd_undo(doc);
  ASSERT_EQUAL(doc->anim->frame_count, 3);
  ASSERT_NULL(g_app->timeline_win);
  close_document(doc);
  ASSERT_TRUE(send_message(g_app->tool_win, tbGetState, ID_ANIM_PLAY, NULL) & TBSTATE_DISABLED);
  ASSERT_FALSE(window_has_state(g_app->color_win, WINDOW_STATE_VISIBLE));
  penciltest_teardown(); PASS();
}

static void test_floating_color_position(void) {
  TEST("right palette keeps its dragged position across layer changes and display resize");
  penciltest_setup();
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_COLOR));
  window_t *colors = g_app->color_win;
  ASSERT_TRUE(dock_float(colors, R(620, 180, colors->frame.w, colors->frame.h)));
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_PENCIL));
  ASSERT_FALSE(window_has_state(colors, WINDOW_STATE_VISIBLE));
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_FX));
  ASSERT_TRUE(window_has_state(colors, WINDOW_STATE_VISIBLE));
  ASSERT_EQUAL(colors->frame.x, 620); ASSERT_EQUAL(colors->frame.y, 180);
  send_message(g_app->chrome_win, evDisplayChange, MAKEDWORD(758, 1080), NULL);
  send_message(doc->win, evDisplayChange, MAKEDWORD(758, 1080), NULL);
  ASSERT_EQUAL(colors->frame.x, 620); ASSERT_EQUAL(colors->frame.y, 180);
  ASSERT_EQUAL(colors->frame.w, PALETTE_WIN_W);
  penciltest_teardown(); PASS();
}

static void test_pencil_paper_and_ink(void) {
  TEST("Pencil Test defaults to black ink, white paper and the Disney extended palette");
  penciltest_setup();
  g_app->fg_color = IE_INK_COLOR;
  g_app->bg_color = IE_PAPER_COLOR;
  canvas_doc_t *doc = create_document(NULL, 64, 64);
  ASSERT_NOT_NULL(doc);
  ASSERT_EQUAL(doc->background.color, IE_PAPER_COLOR);
  ASSERT_EQUAL(doc->ipal.entries[0], IE_INK_COLOR);
  ASSERT_EQUAL(doc->ipal.entries[1], IE_PAPER_COLOR);
  ASSERT_EQUAL(doc->ipal.entries[2], MAKE_COLOR(0xFF, 0xC3, 0xA2, 0xFF));
  ASSERT_EQUAL(doc->ipal.entries[16], MAKE_COLOR(0xC4, 0xC4, 0xC4, 0xFF));
  ASSERT_EQUAL(doc->ipal.entries[30], MAKE_COLOR(0x88, 0x38, 0xC2, 0xFF));
  ASSERT_EQUAL(IE_INK_COLOR, MAKE_COLOR(0x00, 0x00, 0x00, 0xFF));
  ASSERT_EQUAL(pencil_configured_color(), IE_INK_COLOR);
  ASSERT_EQUAL(IE_PAPER_COLOR, MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF));
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

static void test_no_fg_bg_swatch(void) {
  TEST("Pencil Test tool strip has no foreground/background swatch on any layer");
  penciltest_setup();
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  const int layers[] = {IE_LAYER_BG, IE_LAYER_PENCIL, IE_LAYER_COLOR, IE_LAYER_FX};
  for (int layer = 0; layer < 4; layer++) {
    ASSERT_TRUE(cmd_pencil_layer(doc, layers[layer]));
    toolbar_state_t *tb = window_toolbar_state(g_app->tool_win);
    ASSERT_NOT_NULL(tb);
    for (int i = 0; i < tb->item_count; i++) {
      if (tb->items[i].type != TOOLBAR_ITEM_CUSTOM) continue;
      uint32_t ident = tb->items[i].ident;
      bool layer_btn = ident >= IE_PENCIL_LAYER_BASE && ident < IE_PENCIL_LAYER_BASE + IE_LAYER_COUNT;
      ASSERT_TRUE(layer_btn || ident == ID_PENCIL_TOOLS_FRAME);
    }
  }
  penciltest_teardown();
  PASS();
}

static void test_coloring_palette(void) {
  TEST("floating right palette selects all 32 colors; drawing survives frame switches and undo");
  penciltest_setup();
  canvas_doc_t *doc = create_document(NULL, 64, 64);
  ASSERT_NOT_NULL(doc);
  ASSERT_EQUAL(doc->ipal.count, IE_PENCIL_COLORS);
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_COLOR));
  toolbar_state_t *tb = window_toolbar_state(g_app->tool_win);
  ASSERT_EQUAL(tb->columns, 1);
  ASSERT_EQUAL(g_app->tool_win->frame.w, PALETTE_WIN_W);
  int mini = (TOOL_PALETTE_BTN_SIZE - TOOLBAR_SPACING) / 2;
  for (int i = 0; i < 4; i++) {
    ASSERT_TRUE(tb->items[i].style & TOOLBAR_ITEM_FLAG_SMALL);
    ASSERT_EQUAL(tb->item_rects[i].w, mini);
    ASSERT_EQUAL(tb->item_rects[i].h, mini);
  }
  ASSERT_EQUAL(tb->item_rects[0].y, tb->item_rects[1].y);
  ASSERT_EQUAL(tb->item_rects[2].y, tb->item_rects[3].y);
  ASSERT_TRUE(tb->item_rects[1].x > tb->item_rects[0].x);
  ASSERT_EQUAL(tb->item_rects[3].y + tb->item_rects[3].h - tb->item_rects[0].y,
               2 * mini + TOOLBAR_SPACING);
  ASSERT_TRUE(2 * mini + TOOLBAR_SPACING <= TOOL_PALETTE_BTN_SIZE);
  tb = window_toolbar_state(g_app->color_win);
  ASSERT_TRUE(window_has_state(g_app->color_win, WINDOW_STATE_VISIBLE));
  ASSERT_EQUAL(tb->columns, 1);
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
    ASSERT_TRUE(r.x + r.w <= g_app->color_win->frame.w);
    ASSERT_TRUE(r.y + r.h <= g_app->color_win->frame.h);
    if (swatch % 2) {
      ASSERT_EQUAL(r.y, previous.y);
      ASSERT_TRUE(r.x > previous.x + previous.w);
    }
    uint32_t point = MAKEDWORD(r.x + r.w / 2, r.y + r.h / 2);
    send_message(g_app->color_win->toolbar, evLeftButtonDown, point, NULL);
    send_message(g_app->color_win->toolbar, evLeftButtonUp, point, NULL);
    ASSERT_EQUAL(g_app->fg_color, pencil_default_colors()[swatch]);
    ASSERT_EQUAL(doc->ipal.entries[g_app->fg_palette_idx], pencil_default_colors()[swatch]);
    ASSERT_TRUE(ie_doc_begin_op(doc, "Color Stroke"));
    canvas_set_pixel(doc, swatch, 0, g_app->fg_color);
    ie_doc_commit_op(doc, true);
    previous = r;
    found++;
  }
  ASSERT_EQUAL(found, IE_PENCIL_COLORS);
  cmd_frame_add(doc, false);
  ASSERT_TRUE(cmd_frame_select(doc, 0));
  for (int i = 0; i < IE_PENCIL_COLORS; i++) ASSERT_EQUAL(canvas_get_pixel(doc, i, 0), pencil_default_colors()[i]);
  ASSERT_TRUE(cmd_pencil_color(doc, 4));
  ASSERT_TRUE(ie_doc_begin_op(doc, "Color Fill"));
  canvas_flood_fill(doc, 0, 1, g_app->fg_color);
  ie_doc_commit_op(doc, true);
  ASSERT_EQUAL(canvas_get_pixel(doc, 0, 1), pencil_default_colors()[4]);
  cmd_undo(doc);
  ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 0, 1)), 0);
  cmd_redo(doc);
  ASSERT_EQUAL(canvas_get_pixel(doc, 0, 1), pencil_default_colors()[4]);
  penciltest_teardown();
  PASS();
}

static void test_document_palette(void) {
  TEST("document colors select exact indices without inserting, recoloring or history; larger palettes page");
  penciltest_setup();
  canvas_doc_t *doc = create_document(NULL, 64, 64); ASSERT_NOT_NULL(doc);
  memset(doc->ipal.entries, 0, sizeof(doc->ipal.entries));
  doc->ipal.count = 256;
  for (int i = 0; i < 80; i++) doc->ipal.entries[i] = MAKE_COLOR(i * 3, 255 - i * 2, i, 255);
  doc->ipal.entries[79] = doc->ipal.entries[70];
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_COLOR));
  uint32_t before[256]; memcpy(before, doc->ipal.entries, sizeof(before));
  int history = doc->undo.count;
  ASSERT_TRUE(cmd_pencil_color(doc, 79));
  ASSERT_EQUAL(g_app->fg_palette_idx, 79); ASSERT_EQUAL(g_app->fg_color, doc->ipal.entries[79]);
  ASSERT_EQUAL(doc->undo.count, history); ASSERT_FALSE(doc->modified);
  ASSERT_EQUAL(memcmp(before, doc->ipal.entries, sizeof(before)), 0);
  canvas_set_pixel(doc, 0, 0, g_app->fg_color); ASSERT_EQUAL(doc->pixels[0], 79);
  imageeditor_sync_tool_palette();
  toolbar_state_t *tb = window_toolbar_state(g_app->color_win);
  bool found = false;
  for (int i = 0; i < tb->item_count; i++) if (tb->items[i].ident == IE_PENCIL_PALETTE_BASE + 79) found = true;
  ASSERT_TRUE(found);
  ASSERT_FALSE(cmd_pencil_color(doc, 80)); ASSERT_FALSE(cmd_pencil_color(doc, 255));
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_PENCIL));
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_COLOR)); ASSERT_EQUAL(g_app->fg_palette_idx, 79);
  canvas_doc_t *other = create_document(NULL, 64, 64); ASSERT_NOT_NULL(other);
  ASSERT_TRUE(cmd_pencil_layer(other, IE_LAYER_COLOR)); ASSERT_TRUE(cmd_pencil_color(other, 4));
  set_focus(doc->canvas_win);
  send_message(doc->canvas_win, evSetFocus, 0, NULL);
  ASSERT_EQUAL(g_app->fg_palette_idx, 79);
  ASSERT_EQUAL(g_app->fg_color, doc->ipal.entries[79]);
  penciltest_teardown(); PASS();
}

int main(void) {
  TEST_START("Pencil Test iPad layout");
  test_ipad_options_keep_toolbar_geometry();
  test_layer_tools_and_size();
  test_pencil_canvas_without_timeline();
  test_sidebar_frame_controls();
  test_floating_color_position();
  test_pencil_paper_and_ink();
  test_onion_tint_is_not_gray();
  test_no_fg_bg_swatch();
  test_coloring_palette();
  test_document_palette();
  TEST_END();
}
