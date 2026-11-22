// Pencil Test's ribbon: the standalone application's whole command surface.
// The markup is <ribbon> in imageeditor.orion; this module keeps its buttons, sliders,
// swatches and readouts in step with the document, and turns their notifications into commands.

#include "imageeditor.h"

#if IMAGEEDITOR_RIBBON
typedef struct {
  canvas_doc_t *doc;
  uint64_t      palette_key;
  int           indices[256], count;   // the palette entries the swatch grid shows, in order
  bool          busy;                  // the ribbon is being brought up to date: its controls' echoes are not commands
} pencil_ribbon_state_t;
static pencil_ribbon_state_t s_ribbon;

// The Layers radio group's segments, front of the stack first, as its title in the .orion lists them.
static const struct { int layer; segment_glyph_t glyph; } kLayerSegments[] = {
  {IE_LAYER_FX,     {"phosphor-sparkle",       "phosphor-sparkle-fill"}},
  {IE_LAYER_COLOR,  {"phosphor-palette",       "phosphor-palette-fill"}},
  {IE_LAYER_PENCIL, {"phosphor-pencil-simple", "phosphor-pencil-simple-fill"}},
  {IE_LAYER_BG,     {"phosphor-image-square",  "phosphor-image-square-fill"}},
};

static void ribbon_state(window_t *rb, int ident, bool checked, bool disabled) {
  send_message(rb, rbSetState, ident, (void *)(uintptr_t)((checked ? TBSTATE_CHECKED : 0) | (disabled ? TBSTATE_DISABLED : 0)));
}

static void ribbon_slider(window_t *rb, int ident, int min, int max, int pos, bool disabled, const char *readout) {
  window_t *slider = (window_t *)send_message(rb, rbGetControl, ident, NULL);
  slider_range_t range = {min, max};
  if (slider) {
    send_message(slider, slSetRange, 0, &range);
    send_message(slider, slSetPos, 0, (void *)(intptr_t)pos);
  }
  ribbon_state(rb, ident, false, disabled);
  send_message(rb, rbSetValue, ident, (void *)readout);
}

// The swatches are the palette's visible colours; the grid is told only when they change, so its page stays put.
static void ribbon_colors(window_t *rb, canvas_doc_t *doc, bool pencil) {
  window_t *grid = (window_t *)send_message(rb, rbGetControl, ID_RIBBON_COLORS, NULL);
  uint64_t key = 1469598103934665603ull;
  for (int i = 0; doc && i < 256; i++) key = (key ^ doc->ipal.entries[i]) * 1099511628211ull;
  if (doc) key = (key ^ (uint32_t)doc->ipal.count ^ ((uint64_t)doc->ipal.transparent << 32)) * 1099511628211ull;
  if (!grid) return;
  if (doc != s_ribbon.doc || key != s_ribbon.palette_key) {
    uint32_t colors[256];
    s_ribbon.count = 0;
    for (int i = 0; doc && i < MIN(256, doc->ipal.count); i++)
      if (i != doc->ipal.transparent && COLOR_A(doc->ipal.entries[i])) {
        colors[s_ribbon.count] = doc->ipal.entries[i];
        s_ribbon.indices[s_ribbon.count++] = i;
      }
    send_message(grid, swSetColors, s_ribbon.count, colors);
    send_message(rb, rbUpdateLayout, 0, NULL);
    // A new film or palette: the layer takes up the colour it last used.
    if (doc && !pencil && !cmd_pencil_color(doc, doc->pencil_color_index[doc->layer.active]))
      for (int i = 0; i < s_ribbon.count; i++) if (cmd_pencil_color(doc, s_ribbon.indices[i])) break;
    s_ribbon.doc = doc; s_ribbon.palette_key = key;
  }
  int selected = -1;
  for (int i = 0; !pencil && i < s_ribbon.count; i++) if (s_ribbon.indices[i] == g_app->fg_palette_idx) selected = i;
  if ((int)send_message(grid, swGetSel, 0, NULL) != selected) send_message(grid, swSetSel, (uint32_t)selected, NULL);
  ribbon_state(rb, ID_RIBBON_COLORS, false, pencil || !doc);   // pencil is one colour: there is nothing to choose
}

void pencil_ribbon_sync(void) {
  window_t *rb = g_app ? g_app->ribbon_win : NULL;
  if (!rb || s_ribbon.busy) return;
  s_ribbon.busy = true;
  canvas_doc_t *doc = g_app->active_doc;
  anim_timeline_t *anim = doc ? doc->anim : NULL;
  bool layered = pencil_has_layers(doc), ready = anim && anim->frame_count > 0;
  int layer = layered ? doc->layer.active : IE_LAYER_PENCIL;
  bool pencil = layer == IE_LAYER_PENCIL;
  int tool = imageeditor_layer_tool(g_app->current_tool);
  if (tool != g_app->current_tool) handle_menu_command((uint16_t)tool);
  tool = imageeditor_tool_group(g_app->current_tool);

  window_t *layers = (window_t *)send_message(rb, rbGetControl, ID_RIBBON_LAYER, NULL);
  for (int i = 0; layers && i < ARRAY_LEN(kLayerSegments); i++)
    if (kLayerSegments[i].layer == layer) send_message(layers, sgSetSelection, (uint32_t)i, NULL);
  ribbon_state(rb, ID_RIBBON_LAYER, false, !layered);

  // The pencil layer takes graphite only; the colour layers take paint.
  static const struct { int ident; bool paint, graphite; } kTools[] = {
    {ID_TOOL_PENCIL, false, true}, {ID_TOOL_BRUSH, true, false}, {ID_TOOL_ERASER, true, true}, {ID_TOOL_FILL, true, false},
    {ID_TOOL_EYEDROPPER, true, false}, {ID_TOOL_SELECT, true, true}, {ID_TOOL_MOVE, true, true},
#ifndef AX_PLATFORM_IOS
    {ID_TOOL_HAND, true, true},
#endif
  };
  for (int i = 0; i < ARRAY_LEN(kTools); i++)
    ribbon_state(rb, kTools[i].ident, kTools[i].ident == tool, !doc || !(pencil ? kTools[i].graphite : kTools[i].paint));

  char a[16], b[16], text[40];
  ui_format_number(2 * kBrushSizes[CLAMP(g_app->brush_size, 0, NUM_BRUSH_SIZES - 1)] + 1, 0, a, sizeof(a));
  snprintf(text, sizeof(text), ui_tr("%spx"), a);
  ribbon_slider(rb, ID_RIBBON_SIZE, 0, NUM_BRUSH_SIZES - 1, g_app->brush_size, false, text);
  ui_format_number((pencil_opacity() * 100 + 127) / 255, 0, a, sizeof(a));
  snprintf(text, sizeof(text), ui_tr("%s%%"), a);
  ribbon_slider(rb, ID_RIBBON_OPACITY, 1, 255, pencil_opacity(), !pencil, text);
  int gap = fill_gap_to_index(g_app->fill.gap);
  ribbon_slider(rb, ID_RIBBON_GAPS, 0, 3, gap, pencil, ui_tr(fill_gap_name(gap)));
  ribbon_colors(rb, doc, pencil);

  const int frame_commands[] = {ID_ANIM_NEW_FRAME, ID_ANIM_DUPLICATE_FRAME, ID_ANIM_DELETE_FRAME, ID_ANIM_REUSE_CEL, ID_ANIM_MAKE_UNIQUE,
                                ID_ANIM_FIRST_FRAME, ID_ANIM_PREV_FRAME, ID_ANIM_NEXT_FRAME, ID_ANIM_LAST_FRAME, ID_ANIM_PLAY, ID_ANIM_LOOP,
                                ID_ANIM_SET_FPS, ID_ANIM_FRAME_DURATION, ID_ANIM_EXPOSURE_SHEET,
                                ID_ANIM_EXPORT_GIF, ID_ANIM_EXPORT_APNG, ID_ANIM_EXPORT_SPRITESHEET};
  for (int i = 0; i < ARRAY_LEN(frame_commands); i++) {
    int id = frame_commands[i];
    bool enabled = ready, checked = false;
    if (ready && (id == ID_ANIM_PREV_FRAME || id == ID_ANIM_FIRST_FRAME)) enabled = anim->active_frame > 0 || anim->playing;
    if (ready && (id == ID_ANIM_NEXT_FRAME || id == ID_ANIM_LAST_FRAME)) enabled = anim->active_frame < anim->frame_count - 1 || anim->playing;
    if (ready && id == ID_ANIM_DELETE_FRAME) enabled = anim->frame_count > 1;
    if (ready && id == ID_ANIM_PLAY) checked = anim->playing;
    if (ready && id == ID_ANIM_LOOP) checked = anim->loop;
    ribbon_state(rb, id, checked, !enabled);
    // The Quick Access Toolbar repeats the transport.
    if (g_app->main_toolbar_win && (id == ID_ANIM_PREV_FRAME || id == ID_ANIM_NEXT_FRAME || id == ID_ANIM_PLAY)) {
      send_message(g_app->main_toolbar_win, tbEnableItem, id, (void *)(intptr_t)enabled);
      if (id == ID_ANIM_PLAY) send_message(g_app->main_toolbar_win, tbCheckButton, id, (void *)(intptr_t)checked);
    }
  }
  ribbon_state(rb, ID_ANIM_TRACE, g_app->anim_trace_enabled, false);
  ui_format_number(ready ? anim->active_frame + 1 : 0, 0, a, sizeof(a));
  ui_format_number(ready ? anim->frame_count : 0, 0, b, sizeof(b));
  snprintf(text, sizeof(text), ui_tr("%s/%s"), a, b);
  send_message(rb, rbSetValue, ID_RIBBON_FRAME, ready ? text : "—");
  ui_format_number(ready ? anim->fps : 0, 0, a, sizeof(a));
  snprintf(text, sizeof(text), ui_tr("%sfps"), a);
  send_message(rb, rbSetValue, ID_RIBBON_SPEED, ready ? text : "—");

  ribbon_state(rb, ID_VIEW_SHOW_GRID, g_app->grid.visible, false);
  ribbon_state(rb, ID_VIEW_SNAP_GRID, g_app->grid.snap, false);
  ribbon_state(rb, ID_VIEW_SHOW_BACKGROUND, !doc || doc->background.show, !doc);
  s_ribbon.busy = false;
}

result_t pencil_ribbon_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evTimer) {   // playback ticks here: the ribbon is the window that is always there
    if (g_app && g_app->active_doc) anim_tick(g_app->active_doc);
    return true;
  }
  if (msg == evDestroy && g_app && g_app->ribbon_win == win) {
    g_app->ribbon_win = NULL;
    s_ribbon = (pencil_ribbon_state_t){0};
  }
  if (msg == evCommand && g_app) {
    uint16_t id = LOWORD(wparam), code = HIWORD(wparam);
    if (code == sliderValueChanged && lparam) {
      int value = (int)send_message(lparam, slGetPos, 0, NULL);
      if (s_ribbon.busy) return true;
      if (id == ID_RIBBON_SIZE)    g_app->brush_size     = CLAMP(value, 0, NUM_BRUSH_SIZES - 1);
      if (id == ID_RIBBON_OPACITY) g_app->pencil_opacity = CLAMP(value, 1, 255);
      if (id == ID_RIBBON_GAPS)    g_app->fill.gap       = fill_gap_from_index(value);
      pencil_ribbon_sync();
      return true;
    }
    if (code == sgnSelChange && id == ID_RIBBON_LAYER && lparam) {
      int chosen = (int)send_message(lparam, sgGetSelection, 0, NULL);
      if (!s_ribbon.busy && chosen >= 0 && chosen < ARRAY_LEN(kLayerSegments))
        cmd_pencil_layer(g_app->active_doc, kLayerSegments[chosen].layer);
      pencil_ribbon_sync();   // a refused change (a selection being moved) returns the group to the working layer
      return true;
    }
    if (code == swnSelChange && lparam) {
      int chosen = (int)send_message(lparam, swGetSel, 0, NULL);
      if (!s_ribbon.busy && chosen >= 0 && chosen < s_ribbon.count) cmd_pencil_color(g_app->active_doc, s_ribbon.indices[chosen]);
      return true;
    }
    if (code == btnClicked || code == kMenuBarNotificationItemClick || code == kAcceleratorNotification) {
      handle_menu_command(id);
      pencil_ribbon_sync();
      return true;
    }
  }
  return win_ribbon(win, msg, wparam, lparam);
}

window_t *create_pencil_ribbon(hinstance_t hinstance) {
  if (!g_app || g_app->chrome_win) return g_app ? g_app->ribbon_win : NULL;
  g_app->chrome_win = create_ribbon_chrome("Pencil Test Chrome", pencil_ribbon_proc, &imageeditor_ribbon,
                                           main_toolbar_proc, hinstance);
  g_app->ribbon_win = g_app->menubar_win = app_chrome_ribbon(g_app->chrome_win);
  g_app->main_toolbar_win = app_chrome_toolbar(g_app->chrome_win);
  // In a row the layers are their icons, named by tooltips; given the group's height, each icon stands over its name.
  window_t *layers = (window_t *)send_message(g_app->ribbon_win, rbGetControl, ID_RIBBON_LAYER, NULL);
  if (layers) send_message(layers, sgSetStyle, SEGMENTED_STYLE_ICONS_ONLY, NULL);
  for (int i = 0; layers && i < ARRAY_LEN(kLayerSegments); i++)
    send_message(layers, sgSetSegmentGlyph, (uint32_t)i, (void *)&kLayerSegments[i].glyph);
  send_message(g_app->ribbon_win, rbUpdateLayout, 0, NULL);
  imageeditor_sync_main_toolbar();   // publishes the menus and brings the ribbon up to date
  return g_app->ribbon_win;
}
#endif
