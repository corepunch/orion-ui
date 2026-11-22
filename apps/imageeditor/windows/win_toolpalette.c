#include "imageeditor.h"
#include <orion/user/rect.h>

#define ID_TOOL_SWATCH ID_IMAGE_TOOLS_SWATCH

#if IMAGEEDITOR_BW
typedef struct {
  canvas_doc_t *doc;
  uint64_t palette_key;
  int page, selected;
} pencil_palette_state_t;

// Half-size swatch for the single-column strip: two minis fill one button cell.
static void pencil_palette_draw(int swatch, const toolbar_draw_item_t *draw) {
  canvas_doc_t *doc = g_app ? g_app->active_doc : NULL;
  if (!doc || swatch >= doc->ipal.count) return;
  uint32_t color = doc->ipal.entries[swatch];
  bool selected = g_app->fg_palette_idx == swatch;
  irect16_t outer = draw->rect;
  uint32_t border = get_sys_color(selected ? brAccent : brDarkEdge);
  if (!selected && (draw->state & (CTRL_HOVER | CTRL_PRESSED)))
    border = get_sys_color(brTextNormal);
  fill_rounded_rect(border, outer, 3);
  fill_rounded_rect(color, rect_inset(outer, 1), 2);
  if (selected) {
    uint32_t mark = COLOR_R(color) * 299 + COLOR_G(color) * 587 + COLOR_B(color) * 114 > 140000
                    ? MAKE_COLOR(24, 24, 32, 255) : MAKE_COLOR(255, 255, 255, 255);
    fill_rounded_rect(mark, rect_center(outer, 4, 4), 2);
  }
}

// Front of the stack first, matching the toolbar order.
static const struct { int ident, layer; } kPencilLayerButtons[] = {
  {ID_PENCIL_TOOLS_LAYER_FX,     IE_LAYER_FX},
  {ID_PENCIL_TOOLS_LAYER_COLOR,  IE_LAYER_COLOR},
  {ID_PENCIL_TOOLS_LAYER_PENCIL, IE_LAYER_PENCIL},
  {ID_PENCIL_TOOLS_LAYER_BG,     IE_LAYER_BG},
};

static bool pencil_readout(int ident) {
  return ident == ID_PENCIL_TOOLS_SIZE_VALUE || ident == ID_PENCIL_TOOLS_OPACITY_VALUE ||
         ident == ID_PENCIL_TOOLS_FRAME || ident == ID_PENCIL_TOOLS_SPEED;
}

static void pencil_draw_caption(const toolbar_draw_item_t *draw, const char *text, uint32_t ink) {
  irect16_t r = rect_center(draw->rect, text_strwidth("Caption", text), text_char_height("Caption"));
  draw_text("Caption", text, r.x, r.y, ink);
}

// Layers above the editing cutoff are muted but remain selectable.
static void pencil_sync_layer_buttons(window_t *win, int layer) {
  uint32_t muted = get_sys_color(brTextDisabled);
  for (int i = 0; i < ARRAY_LEN(kPencilLayerButtons); i++) {
    int current = kPencilLayerButtons[i].layer;
    uint32_t ident = IE_PENCIL_LAYER_BASE + current;
    if (current == layer) muted = 0;
    send_message(win, tbCheckButton, ident, (void *)(intptr_t)(current == layer));
    send_message(win, tbSetItemColor, ident, &muted);
  }
}

// A readout says in a word what the control under it holds: "5px", "50%", "3/12", "12fps".
static void pencil_draw_readout(int ident, const toolbar_draw_item_t *draw) {
  canvas_doc_t *doc = g_app ? g_app->active_doc : NULL;
  anim_timeline_t *anim = doc ? doc->anim : NULL;
  char a[16], b[16], text[40];
  switch (ident) {
    case ID_PENCIL_TOOLS_SIZE_VALUE:
      ui_format_number(2 * kBrushSizes[CLAMP(g_app->brush_size, 0, NUM_BRUSH_SIZES - 1)] + 1, 0, a, sizeof(a));
      snprintf(text, sizeof(text), ui_tr("%spx"), a);
      break;
    case ID_PENCIL_TOOLS_OPACITY_VALUE:
      ui_format_number((pencil_opacity() * 100 + 127) / 255, 0, a, sizeof(a));
      snprintf(text, sizeof(text), ui_tr("%s%%"), a);
      break;
    case ID_PENCIL_TOOLS_FRAME:
      ui_format_number(anim ? anim->active_frame + 1 : 0, 0, a, sizeof(a));
      ui_format_number(anim ? anim->frame_count : 0, 0, b, sizeof(b));
      snprintf(text, sizeof(text), ui_tr("%s/%s"), a, b);
      break;
    default:
      ui_format_number(anim ? anim->fps : 0, 0, a, sizeof(a));
      snprintf(text, sizeof(text), ui_tr("%sfps"), a);
      break;
  }
  // Frame and speed open their dialogs; size and opacity only report.
  bool opens = ident == ID_PENCIL_TOOLS_FRAME || ident == ID_PENCIL_TOOLS_SPEED;
  if (opens && !anim) snprintf(text, sizeof(text), "—");   // no film to count
  if (opens) theme_draw(THEME_PART_TOOLBAR_BUTTON, draw->rect, draw->state);
  pencil_draw_caption(draw, text, opens ? theme_foreground(THEME_PART_TOOLBAR_BUTTON, draw->state) : get_sys_color(brTextNormal));
}
#endif

static void palette_draw_swatches(irect16_t sw) {
  irect16_t inner_box = rect_inset(sw, 2);
  int chip_side = inner_box.w - inner_box.w / 3;
  int reset_side = inner_box.w / 3;
  irect16_t fg_outer = rect_split_left(rect_split_top(inner_box, chip_side), chip_side);
  irect16_t bg_outer = rect_split_right(rect_split_bottom(inner_box, chip_side), chip_side);
  irect16_t reset_outer = rect_split_left(rect_split_bottom(inner_box, reset_side), reset_side);
  irect16_t bg_inner = rect_inset(bg_outer, 1);
  irect16_t fg_inner = rect_inset(fg_outer, 1);
  irect16_t reset_inner = rect_inset(reset_outer, 1);
  irect16_t reset_black = rect_inset(rect_offset(reset_inner, 1, 1), 1);

  fill_rect(get_sys_color(brDarkEdge), bg_outer);
  fill_rect(g_app ? g_app->bg_color : 0xFF000000, bg_inner);

  fill_rect(get_sys_color(brDarkEdge), fg_outer);
  fill_rect(g_app ? g_app->fg_color : 0xFFFFFFFF, fg_inner);

  fill_rect(get_sys_color(brDarkEdge), reset_outer);
  fill_rect(IE_PAPER_COLOR, reset_inner);
  fill_rect(IE_INK_COLOR, reset_black);
}

#if !IMAGEEDITOR_BW
static void swatch_edit_fg(window_t *owner) {
  if (!g_app) return;
#if IMAGEEDITOR_INDEXED
  canvas_doc_t *doc = g_app->active_doc;
  int idx = g_app->fg_palette_idx;
  if (doc && idx >= 0 && idx < 256 && idx != doc->ipal.transparent) {
    uint32_t out;
    if (show_color_picker(owner, doc->ipal.entries[idx], &out)) {
      doc->ipal.entries[idx] = out;
      g_app->fg_color = out;
      doc->canvas_dirty = true;
      if (doc->canvas_win) invalidate_window(doc->canvas_win);
      if (g_app->tool_win)  invalidate_window(g_app->tool_win);
      if (g_app->color_win) invalidate_window(g_app->color_win);
    }
    return;
  }
#endif
  uint32_t out;
  if (show_color_picker(owner, g_app->fg_color, &out)) {
    g_app->fg_color = out;
    if (g_app->tool_win)  invalidate_window(g_app->tool_win);
    if (g_app->color_win) invalidate_window(g_app->color_win);
  }
}
#endif

#if IMAGEEDITOR_BW
static void pencil_toolbar_items(window_t *win) {
  canvas_doc_t *doc = g_app ? g_app->active_doc : NULL;
  int layer = pencil_has_layers(doc) ? doc->layer.active : -1;
  bool pencil = layer < 0 || layer == IE_LAYER_PENCIL;
  toolbar_item_t items[ARRAY_LEN(imageeditor_pencil_tools_toolbar)];
  int count = 0;
  for (int i = 0; i < ARRAY_LEN(imageeditor_pencil_tools_toolbar); i++) {
    toolbar_item_t item = imageeditor_pencil_tools_toolbar[i];
    if (pencil && (item.ident == ID_TOOL_BRUSH || item.ident == ID_TOOL_FILL || item.ident == ID_TOOL_EYEDROPPER)) continue;
    if (!pencil && (item.ident == ID_TOOL_PENCIL || item.ident == ID_PENCIL_TOOLS_OPACITY ||
                    item.ident == ID_PENCIL_TOOLS_OPACITY_VALUE)) continue;
    for (int r = 0; r < ARRAY_LEN(kPencilLayerButtons); r++)
      if (item.ident == kPencilLayerButtons[r].ident)
        item.ident = IE_PENCIL_LAYER_BASE + kPencilLayerButtons[r].layer;
    if (pencil_readout(item.ident)) {
      item.type = TOOLBAR_ITEM_CUSTOM;
      item.style |= TOOLBAR_ITEM_FLAG_SLIM;
    }
    if (g_app && item.ident == imageeditor_tool_group(imageeditor_layer_tool(g_app->current_tool))) item.state |= TBSTATE_CHECKED;
    items[count++] = item;
  }
  send_message(win, tbSetItems, count, items);
  pencil_sync_layer_buttons(win, layer);
  imageeditor_sync_brush_sliders(win);
  win->value = layer + 1;
}

static void pencil_color_items(window_t *win) {
  canvas_doc_t *doc = g_app ? g_app->active_doc : NULL;
  int layer = pencil_has_layers(doc) ? doc->layer.active : IE_LAYER_PENCIL;
  pencil_palette_state_t *st = win->userdata;
  toolbar_item_t items[IE_PENCIL_COLORS + 2];
  char color_tips[IE_PENCIL_COLORS][40];
  int count = 0;
  if (layer != IE_LAYER_PENCIL && doc) {
    int indices[255], colors = 0;
    for (int i = 0; i < MIN(256, doc->ipal.count); i++)
      if (i != doc->ipal.transparent && COLOR_A(doc->ipal.entries[i])) indices[colors++] = i;
    int pages = MAX(1, (colors + IE_PENCIL_COLORS - 1) / IE_PENCIL_COLORS);
    st->page = CLAMP(st->page, 0, pages - 1);
    for (int n = st->page * IE_PENCIL_COLORS; n < MIN(colors, (st->page + 1) * IE_PENCIL_COLORS); n++) {
      int i = indices[n], slot = n % IE_PENCIL_COLORS;
      uint32_t c = doc->ipal.entries[i];
      snprintf(color_tips[slot], sizeof(color_tips[slot]), "Color %d: #%02X%02X%02X", i, COLOR_R(c), COLOR_G(c), COLOR_B(c));
      items[count++] = (toolbar_item_t){.type = TOOLBAR_ITEM_CUSTOM,
        .ident = IE_PENCIL_PALETTE_BASE + i, .tooltip = color_tips[slot],
        .style = TOOLBAR_ITEM_FLAG_SMALL};
    }
    if (pages > 1) {
      items[count++] = (toolbar_item_t){.type = TOOLBAR_ITEM_CUSTOM, .ident = IE_PENCIL_PALETTE_PREV,
        .tooltip = "Previous palette colors", .style = TOOLBAR_ITEM_FLAG_SMALL,
        .state = st->page == 0 ? TBSTATE_DISABLED : 0};
      items[count++] = (toolbar_item_t){.type = TOOLBAR_ITEM_CUSTOM, .ident = IE_PENCIL_PALETTE_NEXT,
        .tooltip = "Next palette colors", .style = TOOLBAR_ITEM_FLAG_SMALL,
        .state = st->page == pages - 1 ? TBSTATE_DISABLED : 0};
    }
  }
  send_message(win, tbSetItems, count, items);
  if (count && dock_is_floating(win)) {
    isize16_t size = {0};
    if (send_message(win, tbGetIdealSize, 0, &size)) dock_resize_float(win, MAX(PALETTE_WIN_W, size.w), size.h);
  }
  win->value = layer + 1;
}
#endif

void imageeditor_sync_tool_palette(void) {
#if IMAGEEDITOR_BW
  pencil_ribbon_sync();
  if (!g_app || !g_app->tool_win) return;
  canvas_doc_t *doc = g_app->active_doc;
  bool has_layers = pencil_has_layers(doc);
  int layer = has_layers ? doc->layer.active : IE_LAYER_PENCIL;
  int tool = imageeditor_layer_tool(g_app->current_tool);
  if (tool != g_app->current_tool) handle_menu_command(tool);
  if (g_app->tool_win->value != (uint32_t)(has_layers ? layer + 1 : 0)) pencil_toolbar_items(g_app->tool_win);
  if (layer != IE_LAYER_PENCIL) send_message(g_app->tool_win, tbSetItemColor, ID_TOOL_BRUSH, &g_app->fg_color);
  imageeditor_sync_frame_controls();
  window_t *colors = g_app->color_win;
  pencil_palette_state_t *st = colors ? colors->userdata : NULL;
  if (!st) return;
  uint64_t key = 1469598103934665603ull;
  if (doc) for (int i = 0; i < 256; i++) key = (key ^ doc->ipal.entries[i]) * 1099511628211ull;
  if (doc) key = (key ^ (uint32_t)doc->ipal.count ^ ((uint64_t)doc->ipal.transparent << 32)) * 1099511628211ull;
  bool changed = st->doc != doc || st->palette_key != key;
  if (changed) {
    st->page = 0;
    if (doc && layer != IE_LAYER_PENCIL) {
      if (!cmd_pencil_color(doc, doc->pencil_color_index[layer]))
        for (int i = 0; i < MIN(256, doc->ipal.count); i++) if (cmd_pencil_color(doc, i)) break;
    }
  }
  if (doc && layer != IE_LAYER_PENCIL && (changed || st->selected != g_app->fg_palette_idx)) {
    int ordinal = 0;
    for (int i = 0; i < g_app->fg_palette_idx; i++)
      if (i != doc->ipal.transparent && COLOR_A(doc->ipal.entries[i])) ordinal++;
    int page = ordinal / IE_PENCIL_COLORS;
    changed |= page != st->page;
    st->page = page;
  }
  st->doc = doc; st->palette_key = key; st->selected = g_app->fg_palette_idx;
  bool layer_changed = colors->value != (uint32_t)(layer + 1);
  if (changed || layer_changed) pencil_color_items(colors);
  if (changed || layer_changed) show_window(colors, doc && layer != IE_LAYER_PENCIL);
  invalidate_window(colors);
  invalidate_window(g_app->tool_win);
#endif
}

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      send_message(win, tbSetOrientation, TOOLBAR_VERTICAL, NULL);
      send_message(win, tbSetButtonSize, TOOL_PALETTE_BTN_SIZE, NULL);
#if IMAGEEDITOR_BW
      send_message(win, tbSetColumns, 1, NULL);
      pencil_toolbar_items(win);
#else
      toolbar_item_t items[ARRAY_LEN(imageeditor_image_tools_toolbar)];
      memcpy(items, imageeditor_image_tools_toolbar, sizeof(items));
      for (int i = 0; i < ARRAY_LEN(items); i++)
        if (items[i].ident == ID_TOOL_SWATCH) items[i].type = TOOLBAR_ITEM_CUSTOM;
      send_message(win, tbSetItems, ARRAY_LEN(items), items);
      imageeditor_sync_brush_sliders(win);
#endif
#if !IMAGEEDITOR_BW
      send_message(win, tbCheckButton, g_app ? imageeditor_tool_group(g_app->current_tool) : ID_TOOL_SELECT, (void *)(intptr_t)1);
#endif
      return true;
    }
#if IMAGEEDITOR_BW
    case evThemeChanged:
      pencil_sync_layer_buttons(win, (int)win->value - 1);
      return false;
#endif
    case tbDrawItem:
#if IMAGEEDITOR_BW
      if (g_app && pencil_readout((int)wparam) && lparam) {
        pencil_draw_readout((int)wparam, lparam);
        return true;
      }
#endif
      if (wparam != ID_TOOL_SWATCH || !lparam) return false;
      palette_draw_swatches(((toolbar_draw_item_t *)lparam)->rect);
      return true;
    case evCommand:
      if (g_app && HIWORD(wparam) == sliderValueChanged && lparam &&
          (LOWORD(wparam) == IE_OPT_SIZE || LOWORD(wparam) == IE_OPT_OPACITY)) {
        int v = (int)send_message(lparam, slGetPos, 0, NULL);
        if (LOWORD(wparam) == IE_OPT_SIZE) g_app->brush_size     = CLAMP(v, 0, NUM_BRUSH_SIZES - 1);
        else                               g_app->pencil_opacity = CLAMP(v, 1, 255);
        imageeditor_sync_brush_sliders(win);
        invalidate_window(win);   // the readout above the slider follows it
        return true;
      }
      if (HIWORD(wparam) != btnClicked) return false;
      wparam = LOWORD(wparam);
      if (!g_app) return true;
#if IMAGEEDITOR_BW
      if (wparam >= IE_PENCIL_LAYER_BASE && wparam < IE_PENCIL_LAYER_BASE + IE_LAYER_COUNT) {
        cmd_pencil_layer(g_app->active_doc, wparam - IE_PENCIL_LAYER_BASE);
        return true;
      }
      if (pencil_readout((int)wparam)) {   // the number is the way to its dialog
        if (wparam == ID_PENCIL_TOOLS_FRAME) handle_menu_command(ID_ANIM_EXPOSURE_SHEET);
        if (wparam == ID_PENCIL_TOOLS_SPEED) handle_menu_command(ID_ANIM_SET_FPS);
        return true;
      }
#endif
      if (wparam == ID_TOOL_SWATCH) {
#if IMAGEEDITOR_BW

        swap_foreground_background_colors();
#else
        swatch_edit_fg(win);
#endif
        return true;
      }
      if (wparam == ID_TOOL_RECT && g_app->shape_tool) wparam = g_app->shape_tool;
      handle_menu_command((uint16_t)wparam);
      return true;
#if IMAGEEDITOR_BW
    case evTimer:
      if (g_app && g_app->active_doc) anim_tick(g_app->active_doc);
      return true;
#endif
    case evPaint:
      return true;
    case evDestroy:
      if (g_app && g_app->tool_win == win) g_app->tool_win = NULL;
      return true;
    default:
      return false;
  }
}

#if IMAGEEDITOR_BW
void imageeditor_sync_frame_controls(void) {
  pencil_ribbon_sync();
  if (!g_app || !g_app->tool_win) return;
  window_t *win = g_app->tool_win;
  canvas_doc_t *doc = g_app->active_doc;
  anim_timeline_t *anim = doc ? doc->anim : NULL;
  bool ready = anim && anim->frame_count > 0;
  const int commands[] = {ID_ANIM_PREV_FRAME, ID_ANIM_NEXT_FRAME, ID_ANIM_PLAY, ID_ANIM_TRACE,
                          ID_ANIM_NEW_FRAME, ID_ANIM_DUPLICATE_FRAME, ID_ANIM_DELETE_FRAME};
  for (int i = 0; i < ARRAY_LEN(commands); i++) {
    bool enabled = ready;
    if (ready && commands[i] == ID_ANIM_PREV_FRAME) enabled = anim->active_frame > 0 || anim->playing;
    if (ready && commands[i] == ID_ANIM_NEXT_FRAME) enabled = anim->active_frame < anim->frame_count - 1 || anim->playing;
    if (ready && commands[i] == ID_ANIM_DELETE_FRAME) enabled = anim->frame_count > 1;
    send_message(win, tbEnableItem, commands[i], (void *)(intptr_t)enabled);
  }
  send_message(win, tbCheckButton, ID_ANIM_PLAY, (void *)(intptr_t)(ready && anim->playing));
  send_message(win, tbCheckButton, ID_ANIM_TRACE, (void *)(intptr_t)g_app->anim_trace_enabled);
  char tip[128];
  if (ready) snprintf(tip, sizeof(tip), ui_tr("Frame %d of %d · Cel %u · %d ms: click for the exposure sheet"), anim->active_frame + 1,
    anim->frame_count, anim->frames[anim->active_frame].cel->id, anim->frames[anim->active_frame].delay_ms);
  else snprintf(tip, sizeof(tip), "%s", ui_tr("No animation"));
  toolbar_button_info_t info = {.ident = ID_PENCIL_TOOLS_FRAME, .mask = TBIF_TOOLTIP, .tooltip = tip};
  send_message(win, tbSetButtonInfo, 0, &info);
  invalidate_window(win);
}

static result_t pencil_color_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      if (!allocate_window_data(win, sizeof(pencil_palette_state_t))) return false;
      send_message(win, tbSetOrientation, TOOLBAR_VERTICAL, NULL);
      send_message(win, tbSetButtonSize, TOOL_PALETTE_BTN_SIZE, NULL);
      send_message(win, tbSetColumns, 1, NULL);
      send_message(win, tbSetStyle, TOOLBAR_STYLE_GRIP, NULL);
      pencil_color_items(win);
      return true;
    case evPaint: return true;
    case evDestroy:
      if (g_app && g_app->color_win == win) g_app->color_win = NULL;
      return true;
    case evClose:
      show_window(win, false);
      return true;
    case tbDrawItem:
      if ((wparam == IE_PENCIL_PALETTE_PREV || wparam == IE_PENCIL_PALETTE_NEXT) && lparam) {
        toolbar_draw_item_t *draw = lparam;
        theme_draw(THEME_PART_TOOLBAR_BUTTON, draw->rect, draw->state);
        const char *label = wparam == IE_PENCIL_PALETTE_PREV ? "<" : ">";
        irect16_t r = rect_center(draw->rect, text_strwidth("Text", label), text_char_height("Text"));
        draw_text("Text", label, r.x, r.y, theme_foreground(THEME_PART_TOOLBAR_BUTTON, draw->state));
        return true;
      }
      if (wparam >= IE_PENCIL_PALETTE_BASE && wparam < IE_PENCIL_PALETTE_BASE + 256 && lparam) {
        pencil_palette_draw(wparam - IE_PENCIL_PALETTE_BASE, lparam);
        return true;
      }
      return false;
    case evCommand:
      if (HIWORD(wparam) != btnClicked || !g_app) return false;
      wparam = LOWORD(wparam);
      if (wparam == IE_PENCIL_PALETTE_PREV || wparam == IE_PENCIL_PALETTE_NEXT) {
        pencil_palette_state_t *st = win->userdata;
        st->page += wparam == IE_PENCIL_PALETTE_PREV ? -1 : 1;
        pencil_color_items(win);
        invalidate_window(win);
        return true;
      }
      if (wparam >= IE_PENCIL_PALETTE_BASE && wparam < IE_PENCIL_PALETTE_BASE + 256) {
        cmd_pencil_color(g_app->active_doc, wparam - IE_PENCIL_PALETTE_BASE);
        return true;
      }
      return false;
    default: return false;
  }
}

window_t *create_pencil_color_toolbar(void) {
  if (!g_app) return NULL;
  if (g_app->color_win) return g_app->color_win;
  window_t *win = create_window("Colors", WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE | WINDOW_NOTITLE | WINDOW_TOOLBAR,
      MAKERECT(0, 0, PALETTE_WIN_W, 400), g_app->chrome_win, pencil_color_proc, g_app->hinstance, NULL);
  if (!win) return NULL;
  g_app->color_win = win;
  dock_window(win, DOCK_FLOAT, DOCK_EDGE(DOCK_LEFT) | DOCK_EDGE(DOCK_RIGHT), DOCK_TOOLBAR, 0, 0);
  dock_float_aligned(win, DOCK_ALIGN_END, DOCK_ALIGN_START);
  imageeditor_sync_tool_palette();
  return win;
}
#endif
