#include "imageeditor.h"
#include <orion/user/rect.h>

#define ID_TOOL_SWATCH 0x7ffe

static const toolbar_item_t k_tools[] = {
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_SELECT, .icon = "ie-select", .tooltip = "Select"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_MOVE, .icon = "ie-move", .tooltip = "Move"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_MAGIC_WAND, .icon = "ie-magic-wand", .tooltip = "Magic Wand"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_CROP, .icon = "ie-crop", .tooltip = "Crop"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_HAND, .icon = "ie-hand", .tooltip = "Hand"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_EYEDROPPER, .icon = "ie-eyedropper", .tooltip = "Eyedropper"},
#ifndef AX_PLATFORM_IOS
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_ZOOM, .icon = "ie-zoom-in", .tooltip = "Zoom"},
#endif
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_BRUSH, .icon = "ie-brush", .tooltip = "Brush"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_FILL, .icon = "ie-fill", .tooltip = "Fill"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_ERASER, .icon = "ie-eraser", .tooltip = "Eraser"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_TEXT, .icon = "ie-text", .tooltip = "Text"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_RECT, .icon = "ie-rect", .tooltip = "Shapes"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_MAGNIFIER, .icon = "ie-zoom-out", .tooltip = "Magnifier"},
  {.type = TOOLBAR_ITEM_CUSTOM, .ident = ID_TOOL_SWATCH, .tooltip = "Foreground / background colors"},
};

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
    IE_TRACE("swatch fg picker pal_idx=%d", idx);
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
  IE_TRACE("swatch fg picker");
  if (show_color_picker(owner, g_app->fg_color, &out)) {
    g_app->fg_color = out;
    if (g_app->tool_win)  invalidate_window(g_app->tool_win);
    if (g_app->color_win) invalidate_window(g_app->color_win);
  }
}
#endif

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      send_message(win, tbSetOrientation, TOOLBAR_VERTICAL, NULL);
      send_message(win, tbSetButtonSize, TOOL_PALETTE_BTN_SIZE, NULL);
      send_message(win, tbSetItems, ARRAY_LEN(k_tools), (void *)k_tools);
      send_message(win, tbSetActiveButton, g_app ? imageeditor_tool_group(g_app->current_tool) : ID_TOOL_SELECT, NULL);
      return true;
    case tbDrawItem:
      if (wparam != ID_TOOL_SWATCH || !lparam) return false;
      palette_draw_swatches(((toolbar_draw_item_t *)lparam)->rect);
      return true;
    case tbButtonClick:
      IE_TRACE("tool click win=%p ident=%u current=%d", (void *)win, wparam,
               g_app ? g_app->current_tool : -1);
      if (!g_app) return true;
      if (wparam == ID_TOOL_SWATCH) {
#if IMAGEEDITOR_BW
        IE_TRACE("swatch click win=%p swap fg=%08x bg=%08x", (void *)win,
                 g_app->fg_color, g_app->bg_color);
        swap_foreground_background_colors();
#else
        IE_TRACE("swatch click win=%p fg=%08x", (void *)win, g_app->fg_color);
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
