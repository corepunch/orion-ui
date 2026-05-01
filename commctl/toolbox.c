// win_toolbox — 2-column tool-palette grid (Photoshop / VB3 / MS Paint style).
//
// Unlike WINDOW_TOOLBAR (a horizontal non-client band at the top of a window),
// win_toolbox lives entirely inside the window client area and lays buttons in a
// fixed 2-column grid.  One button is the "active" (currently selected) tool and
// is drawn with an inset/pressed appearance.
//
// Typical usage — create a narrow floating window:
//
//   int rows = (NUM_TOOLS + TOOLBOX_COLS - 1) / TOOLBOX_COLS;
//   window_t *tool_win = create_window("Tools",
//       WINDOW_NORESIZE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON,
//       MAKERECT(x, y, TOOLBOX_COLS * TOOLBOX_BTN_SIZE,
//                TITLEBAR_HEIGHT + rows * TOOLBOX_BTN_SIZE),
//       NULL, my_toolbox_proc, hinstance, NULL);
//
//   // Inside my_toolbox_proc (which wraps win_toolbox):
//   case evCreate: {
//       // Optional: load a custom icon strip from a PNG sprite sheet.
//       // Icon tiles are square; wparam = tile size in px.
//       char path[512];
//       snprintf(path, sizeof(path), "%s/" SHAREDIR "/tools.png",
//                ui_get_exe_dir());
//       send_message(win, bxLoadStrip, 16, path);
//
//       toolbox_item_t items[] = {
//           { ID_TOOL_SELECT, 0 },
//           { ID_TOOL_PENCIL, 1 },
//           { ID_TOOL_BRUSH,  2 },
//       };
//       send_message(win, bxSetItems, 3, items);
//       send_message(win, bxSetActiveItem, ID_TOOL_SELECT, NULL);
//       send_message(win, bxSetIconTintBrush, brTextNormal, NULL);
//       return true;
//   }
//   case evCommand:
//       if (HIWORD(wparam) == bxClicked)
//           handle_tool_selected(LOWORD(wparam));
//       return false;
//   default:
//       return win_toolbox(win, msg, wparam, lparam);
//
// Notifications: clicking a button sends evCommand to the toolbox
// window itself with wparam = MAKEDWORD(ident, bxClicked) and
// lparam = the toolbox window.  The wrapping proc intercepts this command.

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "../user/user.h"
#include "../user/draw.h"
#include "../user/image.h"
#include "../user/icons.h"
#include "../kernel/renderer.h"

// Inactive: just the dark background from the global fill; no bevel.
// This matches the classic Photoshop / MS Paint toolbox look.  
// #define TOOLBOX_FLAT

// Maximum length of a copied tooltip string (including NUL terminator).
#define TOOLBOX_TOOLTIP_MAX 256

// Private state owned by each win_toolbox instance.
typedef struct {
  toolbox_item_t *items;       // heap-allocated copy of the item list
  char          (*item_tooltips)[TOOLBOX_TOOLTIP_MAX]; // owned tooltip string copies (parallel to items)
  int             count;       // number of items
  int             btn_size;    // 0 = use TOOLBOX_BTN_SIZE default
  int             active_ident; // ident of active item (-1 = none)
  int             pressed_idx; // index of currently pressed item (-1 = none)
  int             icon_tint_brush; // br* index for icon tint, -1 = disabled
  bitmap_strip_t  strip;       // icon strip (may point to own_strip_tex or external tex)
  uint32_t        own_strip_tex; // GL texture owned by bxLoadStrip (0 = none)
} toolbox_state_t;

static int effective_bsz(const toolbox_state_t *st) {
  return (st->btn_size > 0) ? st->btn_size : TOOLBOX_BTN_SIZE;
}

// Returns grid height (in client pixels) for the current item count.
// Exposed publicly as toolbox_grid_height().
int toolbox_grid_height(window_t *win) {
  toolbox_state_t *st = (toolbox_state_t *)win->userdata;
  if (!st || st->count == 0) return 0;
  int rows = (st->count + TOOLBOX_COLS - 1) / TOOLBOX_COLS;
  return rows * effective_bsz(st);
}

// Hit-test: returns item index at client-local (mx, my), or -1 if none.
static int toolbox_hit(const toolbox_state_t *st, int mx, int my) {
  if (mx < 0 || my < 0) return -1;
  int bsz = effective_bsz(st);
  int col = mx / bsz;
  int row = my / bsz;
  if (col < 0 || col >= TOOLBOX_COLS) return -1;
  int idx = row * TOOLBOX_COLS + col;
  if (idx < 0 || idx >= st->count) return -1;
  return idx;
}

// Draw a single toolbox button at client-local (bx, by).
static void draw_toolbox_button(toolbox_state_t *st, int idx,
                                int bx, int by) {
  int bsz = effective_bsz(st);
  rect_t cell = { bx, by, bsz, bsz };

  bool is_active  = (st->items[idx].ident == st->active_ident);
  bool is_pressed = (idx == st->pressed_idx);
  bool depressed  = is_pressed || is_active;

  if (depressed) {
    draw_button(cell, 1, 1, true);   // inset / pressed look
  } else {
#ifndef TOOLBOX_FLAT
    // Unpressed tools still draw as raised buttons.
    draw_button(cell, 1, 1, false);
#endif
  }

  // Draw icon centred in the cell (shifted 1px when depressed).
  int px = depressed ? 1 : 0;
  int icon = st->items[idx].icon;
  uint32_t tint = 0xFFFFFFFF;
  if (st->icon_tint_brush >= 0 && st->icon_tint_brush < brCount)
    tint = get_sys_color((sys_color_idx_t)st->icon_tint_brush);

  if (icon >= SYSICON_BASE) {
    // Built-in 16x16 sysicon sheet (optionally tinted).
    rect_t icon_dst = rect_offset(rect_center(cell, 16, 16), px, px);
    draw_icon16(icon, icon_dst.x, icon_dst.y, tint);
  } else if (st->strip.tex && st->strip.cols > 0) {
    // Custom sprite-sheet strip (optionally tinted).
    bitmap_strip_t *s = &st->strip;
    int col_idx = icon % s->cols;
    int row_idx = icon / s->cols;
    float u0 = (float)(col_idx * s->icon_w) / (float)s->sheet_w;
    float v0 = (float)(row_idx * s->icon_h) / (float)s->sheet_h;
    float u1 = u0 + (float)s->icon_w / (float)s->sheet_w;
    float v1 = v0 + (float)s->icon_h / (float)s->sheet_h;
    rect_t icon_dst = rect_offset(rect_center(cell, s->icon_w, s->icon_h), px, px);
    draw_sprite_region((int)s->tex,
                       R(icon_dst.x, icon_dst.y, s->icon_w, s->icon_h),
                       UV_RECT(u0, v0, u1, v1), tint, 0);
  } else {
    // Text fallback: draw item index as a number.
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", idx);
    draw_text_small(buf, bx + px + 4, by + px + (bsz - 8) / 2,
                    get_sys_color(brTextNormal));
  }
}

// Toolbox window procedure.
result_t win_toolbox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    // ── Lifecycle ─────────────────────────────────────────────────────────
    case evCreate: {
      toolbox_state_t *st = allocate_window_data(win, sizeof(toolbox_state_t));
      st->btn_size     = 0;
      st->active_ident = -1;
      st->pressed_idx  = -1;
      st->icon_tint_brush = -1;
      return true;
    }
    case evDestroy: {
      toolbox_state_t *st = (toolbox_state_t *)win->userdata;
      if (st) {
        free(st->item_tooltips);
        free(st->items);
        if (st->own_strip_tex)
          R_DeleteTexture(st->own_strip_tex);
        free(st);
        win->userdata = NULL;
      }
      return true;
    }

    // ── Paint ─────────────────────────────────────────────────────────────
    case evPaint: {
      toolbox_state_t *st = (toolbox_state_t *)win->userdata;
      if (!st) return false;
      int bsz = effective_bsz(st);

      // Fill the entire client area with the dark panel background so that
      // inactive buttons look flat (icon on plain dark surface) and any area
      // below the grid (extra client content in wrapping procs) starts clean.
      fill_rect(get_sys_color(brWindowDarkBg), R(0, 0, win->frame.w, win->frame.h));

      for (int i = 0; i < st->count; i++) {
        draw_toolbox_button(st, i,
                            (i % TOOLBOX_COLS) * bsz,
                            (i / TOOLBOX_COLS) * bsz);
      }
      return true;
    }

    // ── Mouse input ───────────────────────────────────────────────────────
    case evLeftButtonDown: {
      toolbox_state_t *st = (toolbox_state_t *)win->userdata;
      if (!st) return false;
      int idx = toolbox_hit(st, LOWORD(wparam), HIWORD(wparam));
      if (idx >= 0) {
        st->pressed_idx = idx;
        set_capture(win);
        invalidate_window(win);
        return true;
      }
      return false;
    }
    case evLeftButtonUp: {
      toolbox_state_t *st = (toolbox_state_t *)win->userdata;
      if (!st) return false;
      int idx   = toolbox_hit(st, LOWORD(wparam), HIWORD(wparam));
      int prev  = st->pressed_idx;
      st->pressed_idx = -1;
      set_capture(NULL);

      if (prev >= 0 && prev == idx) {
        // Confirmed click: update active tool and fire notification.
        st->active_ident = st->items[idx].ident;
        invalidate_window(win);
        // Send evCommand to the toolbox window itself.
        // A wrapping proc intercepts this before it reaches win_toolbox again.
        send_message(win, evCommand,
                     MAKEDWORD((uint16_t)st->items[idx].ident,
                               bxClicked),
                     win);
        return true;
      }
      if (prev >= 0)
        invalidate_window(win);
      return false;
    }

    // ── Toolbox messages ─────────────────────────────────────────────────
    case bxSetItems: {
      toolbox_state_t *st = (toolbox_state_t *)win->userdata;
      if (!st) return false;
      free(st->item_tooltips);
      free(st->items);
      st->items = NULL;
      st->item_tooltips = NULL;
      st->count = 0;
      st->pressed_idx = -1;
      int count = (int)wparam;
      if (count > 0 && lparam) {
        st->items = malloc((size_t)count * sizeof(toolbox_item_t));
        st->item_tooltips = calloc((size_t)count, TOOLBOX_TOOLTIP_MAX);
        if (st->items && st->item_tooltips) {
          memcpy(st->items, lparam, (size_t)count * sizeof(toolbox_item_t));
          for (int i = 0; i < count; i++) {
            if (st->items[i].tooltip && st->items[i].tooltip[0]) {
              strncpy(st->item_tooltips[i], st->items[i].tooltip, TOOLBOX_TOOLTIP_MAX - 1);
              st->item_tooltips[i][TOOLBOX_TOOLTIP_MAX - 1] = '\0';
              st->items[i].tooltip = st->item_tooltips[i];
            } else {
              st->item_tooltips[i][0] = '\0';
              st->items[i].tooltip = NULL;
            }
          }
          st->count = count;
        } else {
          free(st->items);
          free(st->item_tooltips);
          st->items = NULL;
          st->item_tooltips = NULL;
        }
      }
      invalidate_window(win);
      return true;
    }
    case bxSetActiveItem: {
      toolbox_state_t *st = (toolbox_state_t *)win->userdata;
      if (!st) return false;
      st->active_ident = (int)(int32_t)wparam;
      invalidate_window(win);
      return true;
    }
    case bxSetStrip: {
      toolbox_state_t *st = (toolbox_state_t *)win->userdata;
      if (!st) return false;
      // Switching to an external strip: release any previously owned texture,
      // then copy the descriptor without taking ownership of its GL texture.
      if (st->own_strip_tex) {
        R_DeleteTexture(st->own_strip_tex);
        st->own_strip_tex = 0;
      }
      if (lparam)
        memcpy(&st->strip, lparam, sizeof(bitmap_strip_t));
      else
        memset(&st->strip, 0, sizeof(bitmap_strip_t));
      // Caller owns the external texture lifetime.
      invalidate_window(win);
      return true;
    }
    case bxSetButtonSize: {
      toolbox_state_t *st = (toolbox_state_t *)win->userdata;
      if (!st) return false;
      int sz = (int)wparam;
      st->btn_size = (sz >= 8) ? sz : 0;  // 0 = default TOOLBOX_BTN_SIZE
      invalidate_window(win);
      return true;
    }
    case bxLoadStrip: {
      // Load a PNG sprite sheet and own the resulting GL texture.
      // wparam = square icon tile size in pixels; lparam = const char* path.
      toolbox_state_t *st = (toolbox_state_t *)win->userdata;
      if (!st || !lparam) return false;
      if (!g_ui_runtime.running) return false;
      int icon_w = (int)wparam;
      if (icon_w <= 0) return false;
      const char *path = (const char *)lparam;
      int w = 0, h = 0;
      uint8_t *pixels = load_image(path, &w, &h);
      if (!pixels) return false;
      if (w < icon_w || h < icon_w ||
          (w % icon_w) != 0 || (h % icon_w) != 0) {
        image_free(pixels);
        return false;
      }
      uint32_t tex = R_CreateTextureRGBA(w, h, pixels,
                                         R_FILTER_NEAREST, R_WRAP_CLAMP);
      image_free(pixels);
      if (!tex) return false;
      if (st->own_strip_tex)
        R_DeleteTexture(st->own_strip_tex);
      st->own_strip_tex  = tex;
      st->strip.tex      = tex;
      st->strip.icon_w   = icon_w;
      st->strip.icon_h   = icon_w;  // square tiles
      st->strip.cols     = w / icon_w;
      st->strip.sheet_w  = w;
      st->strip.sheet_h  = h;
      invalidate_window(win);
      return true;
    }
    case bxSetIconTintBrush: {
      toolbox_state_t *st = (toolbox_state_t *)win->userdata;
      if (!st) return false;
      int brush = (int)(int32_t)wparam;
      st->icon_tint_brush = (brush >= 0 && brush < brCount) ? brush : -1;
      invalidate_window(win);
      return true;
    }
    case evGetTooltipText: {
      toolbox_state_t *st = (toolbox_state_t *)win->userdata;
      if (!st || !lparam) return false;
      int idx = toolbox_hit(st, LOWORD(wparam), HIWORD(wparam));
      if (idx < 0 || !st->items[idx].tooltip || !st->items[idx].tooltip[0])
        return false;
      char *buf = (char *)lparam;
      strncpy(buf, st->items[idx].tooltip, TOOLBOX_TOOLTIP_MAX - 1);
      buf[TOOLBOX_TOOLTIP_MAX - 1] = '\0';
      return true;
    }
  }
  return false;
}
