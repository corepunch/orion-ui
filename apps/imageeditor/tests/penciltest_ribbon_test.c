#define IMAGEEDITOR_BW 1
#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

// Pencil Test as its own application: the ribbon is all the chrome there is.

app_state_t *g_app;
int g_bw_retina_scale = 1;

static canvas_doc_t *ribbon_setup(void) {
  test_env_init();
  ui_update_screen_size(1080 * UI_WINDOW_SCALE, 758 * UI_WINDOW_SCALE);
  register_commctl_classes();
  g_app = calloc(1, sizeof(*g_app));
  g_app->current_tool = ID_TOOL_PENCIL;
  g_app->fg_color = IE_INK_COLOR;
  g_app->brush_size = 2;
  g_app->fill.gap = IE_FILL_GAP_SMALL;
  g_app->accel = load_accelerators(imageeditor_default_accels, imageeditor_default_accel_count);
  create_pencil_ribbon(0);
  return create_document(NULL, 32, 32);
}

static void ribbon_teardown(void) {
  while (g_app->docs) close_document(g_app->docs);
  free_accelerators(g_app->accel);
  test_env_shutdown(); free(g_app); g_app = NULL;
}

static int state(int ident) { return (int)send_message(g_app->ribbon_win, rbGetState, ident, NULL); }
static window_t *hosted(int ident) { return (window_t *)send_message(g_app->ribbon_win, rbGetControl, ident, NULL); }

// Choose a segment of the Layers radio group as a tap on it does.
static void choose_layer(int segment) {
  window_t *layers = hosted(ID_RIBBON_LAYER);
  send_message(layers, sgSetSelection, (uint32_t)segment, NULL);
  send_message(g_app->ribbon_win, evCommand, MAKEDWORD(ID_RIBBON_LAYER, sgnSelChange), layers);
}
static int chosen_layer(void) { return (int)send_message(hosted(ID_RIBBON_LAYER), sgGetSelection, 0, NULL); }
enum { SEG_FX, SEG_COLOR, SEG_PENCIL, SEG_BACK };

static void click_item(int ident) {
  irect16_t r = {0};
  window_t *rb = g_app->ribbon_win;
  if (!send_message(rb, rbGetItemRect, ident, &r)) return;
  ui_event_t ev = {.message = kEventLeftButtonDown, .x = (window_screen_x(rb) + r.x + r.w / 2) * UI_WINDOW_SCALE,
                   .y = (window_screen_y(rb) + r.y + r.h / 2) * UI_WINDOW_SCALE};
  dispatch_message(&ev);
  ev.message = kEventLeftButtonUp;
  dispatch_message(&ev);
}

static void test_ribbon_is_the_only_chrome(void) {
  TEST("the ribbon is the only chrome: the drawing reaches both sides and the bottom");
  canvas_doc_t *doc = ribbon_setup();
  ASSERT_NOT_NULL(doc);
  window_t *rb = g_app->ribbon_win;
  ASSERT_NOT_NULL(rb);
  ASSERT(app_chrome_ribbon(g_app->chrome_win) == rb && g_app->menubar_win == rb, "the ribbon stands where the menu bar did");
  ASSERT(!g_app->tool_win && !g_app->color_win && !g_app->tool_options_win && !g_app->timeline_win, "no rail, colour strip, options strip or timeline");
  ASSERT_NOT_NULL(g_app->main_toolbar_win);   // the Quick Access Toolbar
  irect16_t area = imageeditor_document_workspace_rect();
  ASSERT_EQUAL(area.x, 0);
  ASSERT_EQUAL(area.w, 1080);
  ASSERT_EQUAL(area.y, rb->frame.y + rb->frame.h);
  ASSERT_EQUAL(area.y + area.h, 758);
  // One row by default; three rows and the fold are a click away, and the drawing follows.
  ASSERT_EQUAL(send_message(rb, rbGetLayout, 0, NULL), RIBBON_LAYOUT_SIMPLIFIED);
  int one_row = area.y;
  send_message(rb, rbSetLayout, RIBBON_LAYOUT_CLASSIC, NULL);
  ASSERT(imageeditor_document_workspace_rect().y > one_row, "three rows take more of the window");
  send_message(rb, rbSetMinimized, 1, NULL);
  ASSERT(imageeditor_document_workspace_rect().y < one_row, "folded, only the tab row stays");
  ASSERT_FALSE(window_has_state(hosted(ID_RIBBON_SIZE), WINDOW_STATE_VISIBLE));
  // The windows the rail build opens from its Window menu stay away.
  handle_menu_command(ID_WINDOW_TOOLS);
  handle_menu_command(ID_WINDOW_COLORS);
  ASSERT(!g_app->tool_win && !g_app->color_win, "the ribbon holds the tools and colours");
  ribbon_teardown();
  PASS();
}

static void test_layers_and_tools(void) {
  TEST("the Layers group chooses the layer; tools, sliders and swatches follow what the layer takes");
  canvas_doc_t *doc = ribbon_setup();
  window_t *grid = hosted(ID_RIBBON_COLORS);
  ASSERT_NOT_NULL(grid);
  ASSERT_EQUAL(doc->layer.active, IE_LAYER_PENCIL);
  window_t *layers = hosted(ID_RIBBON_LAYER);
  ASSERT_NOT_NULL(layers);
  ASSERT_EQUAL(send_message(layers, sgGetCount, 0, NULL), IE_LAYER_COUNT);   // one of four, always
  ASSERT_EQUAL(chosen_layer(), SEG_PENCIL);
  ASSERT_EQUAL(state(ID_TOOL_PENCIL), TBSTATE_CHECKED);
  ASSERT(state(ID_TOOL_BRUSH) == TBSTATE_DISABLED && state(ID_TOOL_FILL) == TBSTATE_DISABLED && state(ID_TOOL_EYEDROPPER) == TBSTATE_DISABLED,
         "the pencil layer takes no paint");
  ASSERT(state(ID_RIBBON_OPACITY) == 0 && state(ID_RIBBON_GAPS) == TBSTATE_DISABLED && state(ID_RIBBON_COLORS) == TBSTATE_DISABLED,
         "pencil has an opacity, and neither gaps to close nor colours to choose");
  ASSERT_TRUE(window_has_state(grid, WINDOW_STATE_DISABLED));

  // A tap on a segment, through the window system.
  irect16_t seg = {0};
  send_message(layers, sgGetSegmentRect, SEG_COLOR, &seg);
  ui_event_t tap = {.message = kEventLeftButtonDown, .x = (window_screen_x(layers) + seg.x + seg.w / 2) * UI_WINDOW_SCALE,
                    .y = (window_screen_y(layers) + seg.y + seg.h / 2) * UI_WINDOW_SCALE};
  dispatch_message(&tap);
  tap.message = kEventLeftButtonUp;
  dispatch_message(&tap);
  ASSERT_EQUAL(doc->layer.active, IE_LAYER_COLOR);
  ASSERT_EQUAL(chosen_layer(), SEG_COLOR);
  ASSERT(g_ui_runtime.focused != layers, "the group does not take the keyboard from the drawing");
  ASSERT(g_app->current_tool == ID_TOOL_BRUSH && state(ID_TOOL_BRUSH) == TBSTATE_CHECKED && state(ID_TOOL_PENCIL) == TBSTATE_DISABLED,
         "the drawing tool becomes the brush");
  ASSERT(state(ID_RIBBON_OPACITY) == TBSTATE_DISABLED && state(ID_RIBBON_GAPS) == 0 && state(ID_RIBBON_COLORS) == 0, "paint has gaps and colours");
  ASSERT_FALSE(window_has_state(grid, WINDOW_STATE_DISABLED));
  int chosen = (int)send_message(grid, swGetSel, 0, NULL);
  ASSERT(chosen >= 0 && g_app->fg_color == doc->ipal.entries[g_app->fg_palette_idx], "the grid shows the colour in hand");

  click_item(ID_TOOL_ERASER);
  ASSERT(g_app->current_tool == ID_TOOL_ERASER && state(ID_TOOL_ERASER) == TBSTATE_CHECKED && state(ID_TOOL_BRUSH) == 0, "one tool is checked");
  click_item(ID_TOOL_PENCIL);   // disabled on this layer
  ASSERT_EQUAL(g_app->current_tool, ID_TOOL_ERASER);

  // A swatch picks the colour; picking it elsewhere (the eyedropper) moves the grid's selection.
  int other = chosen == 3 ? 4 : 3;
  send_message(grid, swSetSel, (uint32_t)other, NULL);
  send_message(g_app->ribbon_win, evCommand, MAKEDWORD(ID_RIBBON_COLORS, swnSelChange), grid);
  int picked = g_app->fg_palette_idx;
  ASSERT(g_app->fg_color == doc->ipal.entries[picked] && doc->pencil_color_index[IE_LAYER_COLOR] == picked, "the swatch is the colour in hand");
  ASSERT_EQUAL(send_message(grid, swGetSel, 0, NULL), other);
  for (int i = 0; i < doc->ipal.count; i++)
    if (i != picked && cmd_pencil_color(doc, i)) break;
  ASSERT(g_app->fg_palette_idx != picked && (int)send_message(grid, swGetSel, 0, NULL) != other, "the grid follows a colour picked elsewhere");

  // The layer remembers its colour.
  int color = g_app->fg_palette_idx;
  choose_layer(SEG_FX);
  ASSERT_EQUAL(doc->layer.active, IE_LAYER_FX);
  choose_layer(SEG_COLOR);
  ASSERT_EQUAL(g_app->fg_palette_idx, color);
  // The group follows a layer chosen elsewhere, and Background is the fourth.
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_BG));
  ASSERT_EQUAL(chosen_layer(), SEG_BACK);
  ribbon_teardown();
  PASS();
}

static void test_sliders(void) {
  TEST("the Brush sliders set size, opacity and gap closing");
  canvas_doc_t *doc = ribbon_setup();
  window_t *rb = g_app->ribbon_win, *size = hosted(ID_RIBBON_SIZE), *opacity = hosted(ID_RIBBON_OPACITY), *gaps = hosted(ID_RIBBON_GAPS);
  ASSERT(size && opacity && gaps, "three sliders");
  ASSERT_EQUAL(send_message(size, slGetPos, 0, NULL), 2);
  send_message(size, slSetPos, 0, (void *)(intptr_t)4);
  send_message(rb, evCommand, MAKEDWORD(ID_RIBBON_SIZE, sliderValueChanged), size);
  ASSERT_EQUAL(g_app->brush_size, 4);
  send_message(opacity, slSetPos, 0, (void *)(intptr_t)200);
  send_message(rb, evCommand, MAKEDWORD(ID_RIBBON_OPACITY, sliderValueChanged), opacity);
  ASSERT_EQUAL(pencil_opacity(), 200);
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_COLOR));
  send_message(gaps, slSetPos, 0, (void *)(intptr_t)3);
  send_message(rb, evCommand, MAKEDWORD(ID_RIBBON_GAPS, sliderValueChanged), gaps);
  ASSERT_EQUAL(g_app->fill.gap, IE_FILL_GAP_LARGE);
  ASSERT_EQUAL(send_message(size, slGetPos, 0, NULL), 4);   // the size is the brush's too
  ribbon_teardown();
  PASS();
}

static void test_frames(void) {
  TEST("frame commands follow the film, on the ribbon and on the Quick Access Toolbar; playback ticks on the ribbon");
  canvas_doc_t *doc = ribbon_setup();
  ASSERT_NOT_NULL(doc->anim);
  ASSERT(state(ID_ANIM_PREV_FRAME) == TBSTATE_DISABLED && state(ID_ANIM_NEXT_FRAME) == TBSTATE_DISABLED && state(ID_ANIM_DELETE_FRAME) == TBSTATE_DISABLED,
         "one frame: nowhere to step, nothing to delete");
  click_item(ID_ANIM_NEW_FRAME);
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  ASSERT_EQUAL(doc->anim->active_frame, 1);
  ASSERT(state(ID_ANIM_PREV_FRAME) == 0 && state(ID_ANIM_NEXT_FRAME) == TBSTATE_DISABLED && state(ID_ANIM_DELETE_FRAME) == 0, "the last of two frames");
  // The Quick Access Toolbar's transport is the same commands.
  toolbar_state_t *quick = window_toolbar_state(g_app->main_toolbar_win);
  ASSERT_NOT_NULL(quick);
  for (int i = 0; i < quick->item_count; i++) {
    if (quick->items[i].ident == ID_ANIM_PREV_FRAME) ASSERT_FALSE(quick->items[i].state & TBSTATE_DISABLED);
    if (quick->items[i].ident == ID_ANIM_NEXT_FRAME) ASSERT_TRUE(quick->items[i].state & TBSTATE_DISABLED);
  }
  send_message(g_app->main_toolbar_win, evCommand, MAKEDWORD(ID_ANIM_PREV_FRAME, btnClicked), NULL);
  ASSERT_EQUAL(doc->anim->active_frame, 0);
  ASSERT(state(ID_ANIM_PREV_FRAME) == TBSTATE_DISABLED && state(ID_ANIM_NEXT_FRAME) == 0, "back on the first frame");

  ASSERT(imageeditor_animation_timer_window() == g_app->ribbon_win, "the ribbon is the window playback ticks on");
  handle_menu_command(ID_ANIM_PLAY);
  ASSERT_TRUE(doc->anim->playing);
  ASSERT_NULL(g_app->tool_win);   // playing no longer needs the rail
  ASSERT_EQUAL(state(ID_ANIM_PLAY), TBSTATE_CHECKED);
  send_message(g_app->ribbon_win, evTimer, 0, NULL);
  ASSERT_EQUAL(doc->anim->active_frame, 1);
  handle_menu_command(ID_ANIM_PLAY);
  ASSERT(!doc->anim->playing && state(ID_ANIM_PLAY) == 0, "and stops");

  handle_menu_command(ID_ANIM_TRACE);
  ASSERT_EQUAL(state(ID_ANIM_TRACE), g_app->anim_trace_enabled ? TBSTATE_CHECKED : 0);
  ribbon_teardown();
  PASS();
}

int main(void) {
  TEST_START("Pencil Test ribbon");
  test_ribbon_is_the_only_chrome();
  test_layers_and_tools();
  test_sliders();
  test_frames();
  TEST_END();
}
