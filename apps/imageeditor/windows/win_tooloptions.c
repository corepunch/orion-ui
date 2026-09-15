#include "imageeditor.h"
#include <orion/user/toolbar.h>

const int kBrushSizes[NUM_BRUSH_SIZES] = {0, 1, 2, 3, 4};

static const toolbar_item_t brush_tools[] = {
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_PENCIL, .icon = "ie-pencil", .tooltip = "Pencil"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_BRUSH,  .icon = "ie-brush",  .tooltip = "Brush"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_SPRAY,  .icon = "ie-spray",  .tooltip = "Spraypaint"},
};
static const toolbar_item_t shape_tools[] = {
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_LINE,         .icon = "ie-line",         .tooltip = "Line"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_RECT,         .icon = "ie-rect",         .tooltip = "Rectangle"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_ELLIPSE,      .icon = "ie-ellipse",      .tooltip = "Ellipse"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_ROUNDED_RECT, .icon = "ie-rounded-rect", .tooltip = "Rounded rectangle"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_POLYGON,      .icon = "ie-polygon",      .tooltip = "Polygon"},
};

int imageeditor_tool_group(int tool) {
  for (int i = 0; i < ARRAY_LEN(brush_tools); i++)
    if (tool == brush_tools[i].ident) return ID_TOOL_BRUSH;
  for (int i = 0; i < ARRAY_LEN(shape_tools); i++)
    if (tool == shape_tools[i].ident) return ID_TOOL_RECT;
  return tool;
}

static void options_slider_tooltip(window_t *win, int ident, const char *name, int value) {
  toolbar_state_t *tb = toolbar_get_state(win);
  for (int i = 0; tb && tb->item_tooltips && i < tb->item_count; i++) {
    if (tb->items[i].ident != ident) continue;
    snprintf(tb->item_tooltips[i], sizeof(tb->item_tooltips[i]), "%s: %d", name, value);
    tb->items[i].tooltip = tb->item_tooltips[i];
  }
}

void imageeditor_sync_tool_options(void) {
  if (!g_app || !g_app->tool_options_win) return;
  window_t *win = g_app->tool_options_win;
  int tool = g_app->current_tool;
  int group = imageeditor_tool_group(tool);
  toolbar_item_t items[6] = {0};
  int count = 0;
  if (group == ID_TOOL_BRUSH || group == ID_TOOL_RECT) {
    const toolbar_item_t *choices = group == ID_TOOL_BRUSH ? brush_tools : shape_tools;
    int n = group == ID_TOOL_BRUSH ? ARRAY_LEN(brush_tools) : ARRAY_LEN(shape_tools);
    for (int i = 0; i < n; i++) {
      items[count] = choices[i];
      items[count++].flags = choices[i].ident == tool ? TOOLBAR_BUTTON_FLAG_ACTIVE : 0;
    }
  }
  if (group == ID_TOOL_BRUSH || tool == ID_TOOL_ERASER) {
    items[count++] = (toolbar_item_t){.type = TOOLBAR_ITEM_SLIDER, .ident = IE_OPT_SIZE,
                                    .tooltip = "Brush size"};
  } else if (group == ID_TOOL_RECT) {
    items[count++] = (toolbar_item_t){.type = TOOLBAR_ITEM_BUTTON, .ident = IE_OPT_FILLED,
                                    .icon = "ie-fill", .tooltip = g_app->shape_filled ? "Filled — click for outline" : "Outline — click to fill",
                                    .flags = g_app->shape_filled ? TOOLBAR_BUTTON_FLAG_ACTIVE : 0};
  } else if (tool == ID_TOOL_MAGIC_WAND) {
    items[count++] = (toolbar_item_t){.type = TOOLBAR_ITEM_BUTTON, .ident = IE_OPT_AA,
                                    .icon = "ie-magic-wand", .tooltip = "Antialias selection edges",
                                    .flags = g_app->wand.antialias ? TOOLBAR_BUTTON_FLAG_ACTIVE : 0};
    items[count++] = (toolbar_item_t){.type = TOOLBAR_ITEM_SLIDER, .ident = IE_OPT_SPREAD,
                                    .tooltip = "Selection tolerance"};
    items[count++] = (toolbar_item_t){.type = TOOLBAR_ITEM_BUTTON, .ident = IE_OPT_COLOR,
                                    .icon = "color-picker", .tooltip = "Selection overlay color"};
  }
  window_t *focus = g_ui_runtime.focused;
  send_message(win, tbSetItems, count, items);
  window_t *slider = get_window_item(win, tool == ID_TOOL_MAGIC_WAND ? IE_OPT_SPREAD : IE_OPT_SIZE);
  if (slider) {
    bool wand = tool == ID_TOOL_MAGIC_WAND;
    slider_range_t range = {0, wand ? 255 : NUM_BRUSH_SIZES - 1};
    send_message(slider, slSetRange, 0, &range);
    send_message(slider, slSetPos, 0, (void *)(intptr_t)(wand ? g_app->wand.spread : g_app->brush_size));
    options_slider_tooltip(win, slider->id, wand ? "Tolerance" : "Size (px)",
                           wand ? g_app->wand.spread : 2 * kBrushSizes[CLAMP(g_app->brush_size, 0, NUM_BRUSH_SIZES - 1)] + 1);
  }
  resize_window(win, PALETTE_WIN_W, toolbar_effective_item_height(win) + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH));
  if (focus && is_window(focus)) g_ui_runtime.focused = focus;
  invalidate_window(win);
}

result_t win_tool_options_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      send_message(win, tbSetOrientation, TOOLBAR_VERTICAL, NULL);
      send_message(win, tbSetButtonSize, TOOL_PALETTE_BTN_SIZE, NULL);
      send_message(win, tbSetStyle, TOOLBAR_STYLE_GRIP, NULL);
      return true;
    case evPaint: return true;
    case evDisplayChange: {
      int width = LOWORD(wparam), height = HIWORD(wparam);
      if (width <= 0 || height <= 0) return false;
      int x = CLAMP(win->frame.x, 0, MAX(0, width - win->frame.w));
      int y = CLAMP(win->frame.y, 0, MAX(0, height - win->frame.h));
      if (x != win->frame.x || y != win->frame.y) {
        IE_TRACE("options constrain win=%p position=%d,%d", (void *)win, x, y);
        move_window(win, x, y);
      }
      return true;
    }
    case evClose:
      IE_TRACE("options close rejected win=%p", (void *)win);
      return true;
    case tbButtonClick: {
      if (!g_app) return false;
      IE_TRACE("options click win=%p ident=%u tool=%d", (void *)win, wparam, g_app->current_tool);
      int group = imageeditor_tool_group(g_app->current_tool);
      if (imageeditor_tool_group(wparam) == group && (group == ID_TOOL_BRUSH || group == ID_TOOL_RECT)) {
        handle_menu_command(wparam);
        return true;
      }
      switch (wparam) {
        case IE_OPT_FILLED: g_app->shape_filled = !g_app->shape_filled; break;
        case IE_OPT_AA:     g_app->wand.antialias = !g_app->wand.antialias; break;
        case IE_OPT_COLOR: {
          uint32_t color = g_app->wand.overlay_color;
          if (show_color_picker(win, color, &color)) {
            g_app->wand.overlay_color = color;
            IE_TRACE("wand overlay color=%u", color);
            if (g_app->active_doc && g_app->active_doc->canvas_win)
              invalidate_window(g_app->active_doc->canvas_win);
          }
          break;
        }
        default: return false;
      }
      IE_TRACE("options state filled=%d antialias=%d", g_app->shape_filled, g_app->wand.antialias);
      imageeditor_sync_tool_options();
      return true;
    }
    case evCommand: {
      if (!g_app || HIWORD(wparam) != sliderValueChanged || !lparam) return false;
      window_t *slider = lparam;
      int value = (int)send_message(slider, slGetPos, 0, NULL);
      switch (LOWORD(wparam)) {
        case IE_OPT_SIZE:
          g_app->brush_size = CLAMP(value, 0, NUM_BRUSH_SIZES - 1);
          options_slider_tooltip(win, IE_OPT_SIZE, "Size (px)", 2 * kBrushSizes[g_app->brush_size] + 1);
          break;
        case IE_OPT_SPREAD:
          g_app->wand.spread = CLAMP(value, 0, 255);
          options_slider_tooltip(win, IE_OPT_SPREAD, "Tolerance", g_app->wand.spread);
          break;
        default: return false;
      }
      IE_TRACE("options slider win=%p ident=%u value=%d", (void *)win, slider->id, value);
      invalidate_window(win);
      return true;
    }
    case evDestroy:
      if (g_app && g_app->tool_options_win == win) g_app->tool_options_win = NULL;
      return true;
    default: return false;
  }
}
