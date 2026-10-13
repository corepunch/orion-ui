#include "imageeditor.h"
#include <orion/user/rect.h>

#define ID_TOOL_SWATCH 0x7ffe

static const toolbar_item_t k_tools[] = {
#if IMAGEEDITOR_BW
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_BRUSH, .icon = "ie-pencil", .tooltip = "Pencil / Brush", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_ERASER, .icon = "ie-eraser", .tooltip = "Eraser", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_FILL, .icon = "ie-fill", .tooltip = "Fill", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_EYEDROPPER, .icon = "ie-eyedropper", .tooltip = "Eyedropper", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_SELECT, .icon = "ie-select", .tooltip = "Select", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_MOVE, .icon = "ie-move", .tooltip = "Move", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_HAND, .icon = "ie-hand", .tooltip = "Hand", .style = TBSTYLE_CHECKGROUP},
#else
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_SELECT, .icon = "ie-select", .tooltip = "Select", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_MOVE, .icon = "ie-move", .tooltip = "Move", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_MAGIC_WAND, .icon = "ie-magic-wand", .tooltip = "Magic Wand", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_CROP, .icon = "ie-crop", .tooltip = "Crop", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_HAND, .icon = "ie-hand", .tooltip = "Hand", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_EYEDROPPER, .icon = "ie-eyedropper", .tooltip = "Eyedropper", .style = TBSTYLE_CHECKGROUP},
#ifndef AX_PLATFORM_IOS
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_ZOOM, .icon = "ie-zoom-in", .tooltip = "Zoom", .style = TBSTYLE_CHECKGROUP},
#endif
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_BRUSH, .icon = "ie-brush", .tooltip = "Brush", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_FILL, .icon = "ie-fill", .tooltip = "Fill", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_ERASER, .icon = "ie-eraser", .tooltip = "Eraser", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_TEXT, .icon = "ie-text", .tooltip = "Text", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_RECT, .icon = "ie-rect", .tooltip = "Shapes", .style = TBSTYLE_CHECKGROUP},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_MAGNIFIER, .icon = "ie-zoom-out", .tooltip = "Magnifier", .style = TBSTYLE_CHECKGROUP},
#endif
  {.type = TOOLBAR_ITEM_CUSTOM, .ident = ID_TOOL_SWATCH, .tooltip = "Foreground / background colors"},
};

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

// Two-letter codes: mini cells hold a quarter of a normal button.
static const char *pencil_layer_code(int layer) {
  switch (layer) {
    case IE_LAYER_BG:     return "BG";
    case IE_LAYER_COLOR:  return "CO";
    case IE_LAYER_PENCIL: return "PE";
    case IE_LAYER_FX:     return "FX";
    default:              return "??";
  }
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
  int layer = pencil_has_layers(doc) ? doc->layer.active : IE_LAYER_PENCIL;
  static const int order[] = {IE_LAYER_BG, IE_LAYER_PENCIL, IE_LAYER_COLOR, IE_LAYER_FX};
  static const char *const tips[] = {"Background — shared by all frames", "Ink and color — above pencil", "Pencil — soft alpha strokes", "FX — above pencil"};
  pencil_palette_state_t *st = win->userdata;
  toolbar_item_t items[4 + 4 + ARRAY_LEN(k_tools) + IE_PENCIL_COLORS];
  char color_tips[IE_PENCIL_COLORS][40];
  int count = 0;
  for (int i = 0; i < 4; i++)
    items[count++] = (toolbar_item_t){.type = TOOLBAR_ITEM_CUSTOM,
      .ident = IE_PENCIL_LAYER_BASE + order[i], .tooltip = tips[order[i]],
      .style = TOOLBAR_ITEM_FLAG_SMALL};
  items[count++] = (toolbar_item_t){.type = TOOLBAR_ITEM_SEPARATOR};
  for (int i = 0; i < ARRAY_LEN(k_tools); i++) {
    if (k_tools[i].ident == ID_TOOL_SWATCH) continue;
    if (layer == IE_LAYER_PENCIL && (k_tools[i].ident == ID_TOOL_FILL || k_tools[i].ident == ID_TOOL_EYEDROPPER)) continue;
    toolbar_item_t tool = k_tools[i];
    tool.style |= CONTROL_SIZE_SMALL;
    tool.w = TOOL_PALETTE_BTN_SIZE;
    items[count++] = tool;
  }
  if (layer != IE_LAYER_PENCIL && doc) {
    int indices[255], colors = 0;
    for (int i = 0; i < MIN(256, doc->ipal.count); i++)
      if (i != doc->ipal.transparent && COLOR_A(doc->ipal.entries[i])) indices[colors++] = i;
    int pages = MAX(1, (colors + IE_PENCIL_COLORS - 1) / IE_PENCIL_COLORS);
    st->page = CLAMP(st->page, 0, pages - 1);
    items[count++] = (toolbar_item_t){.type = TOOLBAR_ITEM_SEPARATOR};
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
  send_message(win, tbCheckButton, g_app ? imageeditor_tool_group(g_app->current_tool) : ID_TOOL_BRUSH, (void *)(intptr_t)1);
  win->value = layer + 1;
}
#endif

void imageeditor_sync_tool_palette(void) {
#if IMAGEEDITOR_BW
  if (!g_app || !g_app->tool_win) return;
  canvas_doc_t *doc = g_app->active_doc;
  int layer = pencil_has_layers(doc) ? doc->layer.active : IE_LAYER_PENCIL;
  pencil_palette_state_t *st = g_app->tool_win->userdata;
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
  if (changed || g_app->tool_win->value != (uint32_t)(layer + 1)) pencil_toolbar_items(g_app->tool_win);
  invalidate_window(g_app->tool_win);
#endif
}

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
#if IMAGEEDITOR_BW
      if (!allocate_window_data(win, sizeof(pencil_palette_state_t))) return false;
#endif
      send_message(win, tbSetOrientation, TOOLBAR_VERTICAL, NULL);
      send_message(win, tbSetButtonSize, TOOL_PALETTE_BTN_SIZE, NULL);
#if IMAGEEDITOR_BW
      send_message(win, tbSetColumns, 1, NULL);
      pencil_toolbar_items(win);
#else
      send_message(win, tbSetItems, ARRAY_LEN(k_tools), (void *)k_tools);
#endif
      send_message(win, tbCheckButton, g_app ? imageeditor_tool_group(g_app->current_tool) : ID_TOOL_SELECT, (void *)(intptr_t)1);
      return true;
    }
    case tbDrawItem:
#if IMAGEEDITOR_BW
      if (wparam >= IE_PENCIL_LAYER_BASE && wparam < IE_PENCIL_LAYER_BASE + IE_LAYER_COUNT && lparam) {
        int layer = wparam - IE_PENCIL_LAYER_BASE;
        toolbar_draw_item_t *draw = lparam;
        bool active = g_app && pencil_has_layers(g_app->active_doc) && g_app->active_doc->layer.active == layer;
        theme_draw(THEME_PART_TOOLBAR_BUTTON, draw->rect, draw->state | (active ? CTRL_SELECTED : 0));
        const char *code = pencil_layer_code(layer);
        int width = text_strwidth(FONT_SMALLEST, code);
        irect16_t text = rect_center(draw->rect, width, text_char_height(FONT_SMALLEST));
        draw_text(FONT_SMALLEST, code, text.x, text.y, get_sys_color(brTextNormal));
        return true;
      }
      if ((wparam == IE_PENCIL_PALETTE_PREV || wparam == IE_PENCIL_PALETTE_NEXT) && lparam) {
        toolbar_draw_item_t *draw = lparam;
        theme_draw(THEME_PART_TOOLBAR_BUTTON, draw->rect, draw->state);
        const char *label = wparam == IE_PENCIL_PALETTE_PREV ? "<" : ">";
        irect16_t r = rect_center(draw->rect, text_strwidth(FONT_SMALL, label), text_char_height(FONT_SMALL));
        draw_text(FONT_SMALL, label, r.x, r.y, theme_foreground(THEME_PART_TOOLBAR_BUTTON, draw->state));
        return true;
      }
      if (wparam >= IE_PENCIL_PALETTE_BASE && wparam < IE_PENCIL_PALETTE_BASE + 256 && lparam) {
        pencil_palette_draw(wparam - IE_PENCIL_PALETTE_BASE, lparam);
        return true;
      }
#endif
      if (wparam != ID_TOOL_SWATCH || !lparam) return false;
      palette_draw_swatches(((toolbar_draw_item_t *)lparam)->rect);
      return true;
    case evCommand:
      if (HIWORD(wparam) != btnClicked) return false;
      wparam = LOWORD(wparam);
      if (!g_app) return true;
#if IMAGEEDITOR_BW
      if (wparam >= IE_PENCIL_LAYER_BASE && wparam < IE_PENCIL_LAYER_BASE + IE_LAYER_COUNT) {
        cmd_pencil_layer(g_app->active_doc, wparam - IE_PENCIL_LAYER_BASE);
        return true;
      }
      if (wparam == IE_PENCIL_PALETTE_PREV || wparam == IE_PENCIL_PALETTE_NEXT) {
        pencil_palette_state_t *st = win->userdata;
        st->page += wparam == IE_PENCIL_PALETTE_PREV ? -1 : 1;
        pencil_toolbar_items(win);
        invalidate_window(win);
        return true;
      }
      if (wparam >= IE_PENCIL_PALETTE_BASE && wparam < IE_PENCIL_PALETTE_BASE + 256) {
        cmd_pencil_color(g_app->active_doc, wparam - IE_PENCIL_PALETTE_BASE);
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
      if (wparam == ID_TOOL_BRUSH && g_app->brush_tool) wparam = g_app->brush_tool;
      if (wparam == ID_TOOL_RECT && g_app->shape_tool) wparam = g_app->shape_tool;
      handle_menu_command((uint16_t)wparam);
      return true;
    case evPaint:
      return true;
    case evDestroy:
      if (g_app && g_app->tool_win == win) g_app->tool_win = NULL;
      return true;
    default:
      return false;
  }
}
