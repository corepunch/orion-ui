// Tool palette window for the image editor.
//
// The palette is owner-drawn in a single window:
//   - top area: 2-column tool button grid
//   - bottom area: FG/BG/reset swatches
//
// No child button windows are used.

#include "imageeditor.h"
#include <orion/kernel/renderer.h>
#include <orion/user/rect.h>
#include <orion/user/svg_icon_loader.h>

// Image editor atlas tile size (all icons are square).
#define ICON_W  TOOL_ICON_W

typedef struct {
  int active_tool;
  int hot_index;
  int pressed_index;
} palette_state_t;

static bitmap_strip_t g_tool_strip = {0};
static bool g_tool_strip_loaded = false;

// Tool palette layout with ident, icon index, and fallback text.
static const toolbox_item_t k_tools[NUM_TOOLS] = {
  { ID_TOOL_SELECT,        IE_RECTANGULAR_MARQUEE,           "" },
  { ID_TOOL_MOVE,          IE_MOVE,                          "" },
  { ID_TOOL_MAGIC_WAND,    IE_MAGIC_WAND,                    "" },
  { ID_TOOL_CROP,          IE_CROP,                          "" },
  { ID_TOOL_HAND,          IE_HAND,                          "" },
  { ID_TOOL_EYEDROPPER,    IE_EYEDROPPER,                    "" },
  { ID_TOOL_ZOOM,          IE_ZOOM_IN,                       "" },
  { ID_TOOL_PENCIL,        IE_PENCIL,                        "" },
  { ID_TOOL_BRUSH,         IE_BRUSH,                         "" },
  { ID_TOOL_SPRAY,         IE_SPRAY_CAN,                     "" },
  { ID_TOOL_FILL,          IE_PAINT_BUCKET,                  "" },
  { ID_TOOL_ERASER,        IE_ERASER,                        "" },
  { ID_TOOL_LINE,          IE_LINE,                          "" },
  { ID_TOOL_TEXT,          IE_TEXT,                          "" },
  { ID_TOOL_RECT,          IE_RECTANGLE,                     "" },
  { ID_TOOL_ELLIPSE,       IE_ELLIPSE,                       "" },
  { ID_TOOL_ROUNDED_RECT,  IE_ROUNDED_RECTANGLE,             "" },
  { ID_TOOL_POLYGON,       IE_POLYGON_LASSO,                 "" },
  { ID_TOOL_MAGNIFIER,     IE_ZOOM_OUT,                      "" },
};

static palette_state_t *palette_state(window_t *win) {
  return win ? (palette_state_t *)win->userdata : NULL;
}

static irect16_t palette_tool_rect(int index) {
  int col = index % TOOLBOX_COLS;
  int row = index / TOOLBOX_COLS;
  return (irect16_t){
    col * TOOL_PALETTE_BTN_SIZE,
    row * TOOL_PALETTE_BTN_SIZE,
    TOOL_PALETTE_BTN_SIZE,
    TOOL_PALETTE_BTN_SIZE
  };
}

static int palette_hit_tool(int x, int y) {
  if (x < 0 || y < 0 || x >= PALETTE_WIN_W || y >= TOOL_TOOLBAR_H)
    return -1;
  int col = x / TOOL_PALETTE_BTN_SIZE;
  int row = y / TOOL_PALETTE_BTN_SIZE;
  int idx = row * TOOLBOX_COLS + col;
  return (idx >= 0 && idx < NUM_TOOLS) ? idx : -1;
}

static void palette_draw_icon(irect16_t r, int icon_idx, int offset) {
  if (!g_tool_strip_loaded || g_tool_strip.cols <= 0)
    return;
  int col = icon_idx % g_tool_strip.cols;
  int row = icon_idx / g_tool_strip.cols;
  float u0 = (float)(col * g_tool_strip.icon_w) / (float)g_tool_strip.sheet_w;
  float v0 = (float)(row * g_tool_strip.icon_h) / (float)g_tool_strip.sheet_h;
  float u1 = u0 + (float)g_tool_strip.icon_w / (float)g_tool_strip.sheet_w;
  float v1 = v0 + (float)g_tool_strip.icon_h / (float)g_tool_strip.sheet_h;
  irect16_t icon = rect_offset(rect_center(r, g_tool_strip.icon_w, g_tool_strip.icon_h),
                               offset, offset);
  draw_sprite_region((int)g_tool_strip.tex,
                     R(icon.x, icon.y, g_tool_strip.icon_w, g_tool_strip.icon_h),
                     UV_RECT(u0, v0, u1, v1), get_sys_color(brToolbarForeground), 0);
}

static void palette_set_active_tool(window_t *win, int ident) {
  palette_state_t *st = palette_state(win);
  if (!st) return;
  st->active_tool = ident;
  invalidate_window(win);
}

// Iconoir SVG name for each IE_ICONS entry (24x24 tiles).
// NULL entries are left blank and logged to stderr at startup.
static const char *k_tool_svg_names[IE_ICON_COUNT] = {
  [IE_ADD_ANCHOR_POINT    ] = "plus-square",
  [IE_BRUSH               ] = "design-nib",
  [IE_BURN                ] = "fire-flame",
  [IE_BUTTON              ] = "cursor-pointer",
  [IE_CLIPBOARD           ] = "paste-clipboard",
  [IE_CLONE_STAMP         ] = "copy",
  [IE_COLOR_WHEEL         ] = "palette",
  [IE_COPY                ] = "copy",
  [IE_CROP                ] = "crop",
  [IE_DELETE_ANCHOR_POINT ] = "minus-square",
  [IE_DELETE_FILE         ] = "trash",
  [IE_DODGE               ] = "brightness",
  [IE_DOWNLOAD            ] = "download",
  [IE_DROPLET             ] = "droplet",
  [IE_ELLIPSE             ] = "circle",
  [IE_ERASER              ] = "erase",
  [IE_EXPORT              ] = "send-diagonal",
  [IE_EYEDROPPER          ] = "color-picker",
  [IE_FLIP_HORIZONTAL     ] = "mirror",
  [IE_FLIP_VERTICAL       ] = "mirror",
  [IE_GRADIENT            ] = "triangle",
  [IE_GRID                ] = "view-grid",
  [IE_GUIDES              ] = "ruler",
  [IE_HAND                ] = "drag-hand-gesture",
  [IE_HEALING             ] = "health-shield",
  [IE_HISTORY             ] = "clock-rotate-right",
  [IE_ICON010             ] = NULL,
  [IE_LASSO               ] = "selective-tool",
  [IE_LAYERS              ] = "multiple-pages",
  [IE_LINE                ] = "minus",
  [IE_LOCK                ] = "lock",
  [IE_MAGIC_WAND          ] = "magic-wand",
  [IE_MAGNET              ] = "magnet",
  [IE_MASK                ] = "frame",
  [IE_MOUNTAIN            ] = NULL,
  [IE_MOVE                ] = "ruler-arrows",
  [IE_NEW_FILE            ] = "page-plus",
  [IE_OPACITY             ] = "half-cookie",
  [IE_OPEN_FOLDER         ] = "folder",
  [IE_PAINT_BRUSH         ] = "design-nib",
  [IE_PAINT_BUCKET        ] = "fill-color",
  [IE_PEN                 ] = "design-nib",
  [IE_PENCIL              ] = "edit-pencil",
  [IE_POLYGON_LASSO       ] = "triangle",
  [IE_PRINT               ] = "printer",
  [IE_RECTANGLE           ] = "square",
  [IE_RECTANGULAR_MARQUEE ] = "square-dashed",
  [IE_REDO                ] = "redo",
  [IE_RESIZE_IMAGE        ] = "expand",
  [IE_ROTATE              ] = "rotate-camera-right",
  [IE_ROTATE_IMAGE        ] = "rotate-camera-right",
  [IE_ROUNDED_RECTANGLE   ] = "square",
  [IE_ROUNDED_SQUARE      ] = "square",
  [IE_RULER               ] = "ruler",
  [IE_SAVE                ] = "floppy-disk",
  [IE_SCISSORS            ] = "scissor",
  [IE_SETTINGS            ] = "settings",
  [IE_SMUDGE              ] = "fingerprint",
  [IE_SPONGE              ] = "droplet",
  [IE_SPRAY_CAN           ] = "droplet-half",
  [IE_TARGET              ] = "precision-tool",
  [IE_TEXT                ] = "text",
  [IE_TRANSFORM_SELECTION ] = "select-window",
  [IE_UNDO                ] = "undo",
  [IE_VIEW                ] = "eye",
  [IE_ZOOM_IN             ] = "zoom-in",
  [IE_ZOOM_OUT            ] = "zoom-out",
};

static bool palette_load_strip(window_t *win) {
  (void)win;
  if (g_tool_strip_loaded)
    return true;
  if (!g_ui_runtime.running)
    return false;
  memset(&g_tool_strip, 0, sizeof(g_tool_strip));
  char icons_dir[512];
  int n = snprintf(icons_dir, sizeof(icons_dir), "%s/../share/orion/icons",
                   ui_get_exe_dir());
  if (n <= 0 || (size_t)n >= sizeof(icons_dir))
    return false;
  if (svg_build_strip(icons_dir, k_tool_svg_names, IE_ICON_COUNT,
                      ICON_W, 16, &g_tool_strip, stderr))
    g_tool_strip_loaded = true;
  return g_tool_strip_loaded;
}

static void palette_draw_swatches(window_t *win) {
  irect16_t cr = get_client_rect(win);
  if (cr.h <= TOOL_TOOLBAR_H)
    return;
  irect16_t sw = {cr.x, cr.y + TOOL_TOOLBAR_H, cr.w, cr.h - TOOL_TOOLBAR_H};
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
  fill_rect(0xFFFFFFFF, reset_inner);
  fill_rect(0xFF000000, reset_black);
}

static result_t palette_root_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
  palette_state_t *st = palette_state(win);
  switch (msg) {
    case evCreate: {
      st = allocate_window_data(win, sizeof(palette_state_t));
      if (!st) return false;
      st->active_tool = ID_TOOL_SELECT;
      st->hot_index = -1;
      st->pressed_index = -1;

      palette_load_strip(win);
      return true;
    }

    case bxSetActiveItem:
      palette_set_active_tool(win, (int)(uint32_t)wparam);
      return true;

    case evPaint: {
      theme_draw(THEME_PART_SURFACE, R(0, 0, win->frame.w, win->frame.h), CTRL_NORMAL);
      for (int i = 0; i < NUM_TOOLS; i++) {
        irect16_t r = palette_tool_rect(i);
        bool active = st && (st->active_tool == k_tools[i].ident);
        bool pressed = st && (st->pressed_index == i);
        ctrl_state_t state = CTRL_NORMAL;
        if (active) state |= CTRL_SELECTED;
        if (pressed) state |= CTRL_PRESSED;
        if (st && st->hot_index == i) state |= CTRL_HOVER;
        theme_draw(THEME_PART_TOOLBAR_BUTTON, r, state);
        if (g_tool_strip_loaded) {
          palette_draw_icon(r, k_tools[i].icon,
                            (active || pressed) ? get_theme()->press_icon_offset : 0);
        } else {
          const char *label = tool_names[i] ? tool_names[i] : "";
          draw_text_small(label, r.x + 3, r.y + 4, get_sys_color(brToolbarForeground));
        }
      }
      palette_draw_swatches(win);
      return true;
    }

    case evLeftButtonDown: {
      int x = (int16_t)LOWORD(wparam);
      int y = (int16_t)HIWORD(wparam);
      int idx = palette_hit_tool(x, y);
      if (st) {
        st->pressed_index = idx;
        st->hot_index = idx;
      }
      track_mouse(win);
      invalidate_window(win);
      return true;
    }

    case evMouseMove: {
      track_mouse(win);
      int x = (int16_t)LOWORD(wparam);
      int y = (int16_t)HIWORD(wparam);
      int idx = palette_hit_tool(x, y);
      if (st && st->hot_index != idx) {
        st->hot_index = idx;
        invalidate_window(win);
      }
      return true;
    }

    case evMouseLeave:
      if (st && (st->hot_index != -1 || st->pressed_index != -1)) {
        st->hot_index = -1;
        st->pressed_index = -1;
        invalidate_window(win);
      }
      return true;

    case evLeftButtonUp: {
      int x = (int16_t)LOWORD(wparam);
      int y = (int16_t)HIWORD(wparam);
      int idx = palette_hit_tool(x, y);
      int pressed = st ? st->pressed_index : -1;
      if (st) st->pressed_index = -1;
      if (pressed >= 0 && pressed == idx) {
        int ident = k_tools[idx].ident;
        palette_set_active_tool(win, ident);
        if (g_app)
          handle_menu_command((uint16_t)ident);
      }
      invalidate_window(win);
      return true;
    }

    case evResize:
      invalidate_window(win);
      return true;

    case evDestroy:
      if (g_app && g_app->tool_win == win)
        g_app->tool_win = NULL;
      return false;

    default:
      return false;
  }
}

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  return palette_root_proc(win, msg, wparam, lparam);
}
