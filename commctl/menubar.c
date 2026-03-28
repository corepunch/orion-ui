// Menu bar control
// Provides a horizontal menu bar strip and popup dropdown menus.
//
// Usage:
//   1. Create a top-level window with win_menubar as the proc
//      (usually WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON |
//       WINDOW_NORESIZE, full screen width, height = MENUBAR_HEIGHT).
//   2. Send kMenuBarMessageSetMenus with your menu_def_t array.
//   3. Handle kWindowMessageCommand in the same proc (chain with win_menubar)
//      checking HIWORD(wparam) == kMenuBarNotificationItemClick.

#include <stdlib.h>
#include <string.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"
#include "../user/text.h"
#include "../user/accel.h"
#include "menubar.h"

#define MENU_ITEM_H      12   // height of a normal dropdown row
#define MENU_SEP_H        5   // height of a separator row
#define MENU_SIDE_PAD     4   // horizontal text padding inside dropdown
#define MENU_MIN_W       90   // minimum popup width
#define MENU_LABEL_PAD   12   // extra space around each top-level label
#define MENU_HOTKEY_GAP  12   // minimum gap between label and right-aligned hotkey
#define MENU_START_Y      1   // vertical padding above the first dropdown item and below the last one

// ---- per-menubar userdata -----------------------------------------------

typedef struct {
  menu_def_t      *menus;       // shallow copy of the menu_def_t array
  int              count;       // number of menus
  int             *menu_x;      // x offset for each label (window-local)
  window_t        *open_popup;  // currently visible dropdown, or NULL
  int              active_idx;  // index of the currently open menu label (-1 if none)
  accel_table_t   *accel;       // optional accelerator table for hotkey hints (not owned)
} menubar_data_t;

// ---- per-popup userdata (flexible array, malloc'd) ----------------------

typedef struct {
  window_t      *menubar;
  accel_table_t *accel;       // for hotkey lookup; not owned (may be NULL)
  int            item_count;
  int            hovered;    // index of the item under the mouse (-1 if none)
  int            pressed;    // index of the item pressed on mouse-down (-1 if none)
  menu_item_t    items[];    // C99 flexible array
} popup_data_t;

// ---- helpers -------------------------------------------------------------

// Return the display width of a label, stopping at a '\t' character.
static int item_label_width(const char *label) {
  if (!label) return 0;
  const char *tab = strchr(label, '\t');
  return tab ? strnwidth(label, (int)(tab - label)) : strwidth(label);
}

// Draw a menu item label, stopping at a '\t' character.
static void draw_item_label(const char *label, int x, int y, uint32_t col) {
  if (!label) return;
  const char *tab = strchr(label, '\t');
  if (tab) {
    int len = (int)(tab - label);
    char buf[256];
    int n = len < (int)(sizeof(buf) - 1) ? len : (int)(sizeof(buf) - 1);
    memcpy(buf, label, (size_t)n);
    buf[n] = '\0';
    draw_text_small(buf, x, y, col);
  } else {
    draw_text_small(label, x, y, col);
  }
}

static int popup_height(const menu_def_t *m) {
  int h = MENU_START_Y * 2; // top and bottom padding
  for (int i = 0; i < m->item_count; i++)
    h += m->items[i].id ? MENU_ITEM_H : MENU_SEP_H;
  return h;
}

static int popup_width(const menu_def_t *m, const accel_table_t *accel) {
  int w = MENU_MIN_W;
  for (int i = 0; i < m->item_count; i++) {
    if (m->items[i].id) {
      int lw = item_label_width(m->items[i].label) + MENU_SIDE_PAD * 2;
      if (accel) {
        const accel_t *a = accel_find_cmd(accel, m->items[i].id);
        if (a) {
          char hkbuf[32];
          accel_format(a, hkbuf, sizeof(hkbuf));
          lw += MENU_HOTKEY_GAP + strwidth(hkbuf) + MENU_SIDE_PAD;
        }
      }
      if (lw > w) w = lw;
    }
  }
  return w;
}

// ---- popup window proc ---------------------------------------------------

static result_t popup_proc(window_t *win, uint32_t msg,
                            uint32_t wparam, void *lparam) {
  popup_data_t *pd = (popup_data_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate:
      pd = (popup_data_t *)lparam;
      win->userdata = pd;
      pd->hovered = -1;
      pd->pressed = -1;
      set_capture(win);    // receive ALL mouse events, even outside our bounds
      return true;

    case kWindowMessagePaint: {
      // Background
      fill_rect(COLOR_PANEL_BG, 0, 0, win->frame.w, win->frame.h);
      // Border
      fill_rect(COLOR_DARK_EDGE, 0,               0,               win->frame.w, 1);
      fill_rect(COLOR_DARK_EDGE, 0,               win->frame.h - 1, win->frame.w, 1);
      fill_rect(COLOR_DARK_EDGE, 0,               0,               1, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, win->frame.w - 1, 0,               1, win->frame.h);
      // Items
      int y = MENU_START_Y;
      for (int i = 0; i < pd->item_count; i++) {
        const menu_item_t *it = &pd->items[i];
        if (!it->id) {
          // separator
          fill_rect(COLOR_DARK_EDGE, MENU_SIDE_PAD, y + 2,
                    win->frame.w - MENU_SIDE_PAD * 2, 1);
          y += MENU_SEP_H;
        } else {
          if (i == pd->hovered) {
            fill_rect(COLOR_FOCUSED, 1, y, win->frame.w - 2, MENU_ITEM_H);
          }
          bool hov = (i == pd->hovered);
          uint32_t label_col  = hov ? COLOR_PANEL_BG : COLOR_TEXT_NORMAL;
          uint32_t hotkey_col = hov ? COLOR_PANEL_BG : COLOR_TEXT_DISABLED;
          draw_item_label(it->label, MENU_SIDE_PAD + 2, y + 2, label_col);
          if (pd->accel) {
            const accel_t *a = accel_find_cmd(pd->accel, it->id);
            if (a) {
              char hkbuf[32];
              accel_format(a, hkbuf, sizeof(hkbuf));
              int hw = strwidth(hkbuf);
              draw_text_small(hkbuf, win->frame.w - MENU_SIDE_PAD - hw, y + 2,
                              hotkey_col);
            }
          }
          y += MENU_ITEM_H;
        }
      }
      return true;
    }

    case kWindowMessageMouseMove: {
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int new_hovered = -1;
      if (lx >= 0 && lx < win->frame.w && ly >= 0 && ly < win->frame.h) {
        int y = MENU_START_Y;
        for (int i = 0; i < pd->item_count; i++) {
          const menu_item_t *it = &pd->items[i];
          int h = it->id ? MENU_ITEM_H : MENU_SEP_H;
          if (it->id && ly >= y && ly < y + h) {
            new_hovered = i;
            break;
          }
          y += h;
        }
      }
      if (new_hovered != pd->hovered) {
        pd->hovered = new_hovered;
        invalidate_window(win);
      }
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      // Coords are popup-window-local (set_capture ensures we get them)
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      // Click inside the popup – record which item was pressed
      if (lx >= 0 && lx < win->frame.w && ly >= 0 && ly < win->frame.h) {
        int y = MENU_START_Y;
        pd->pressed = -1;
        for (int i = 0; i < pd->item_count; i++) {
          const menu_item_t *it = &pd->items[i];
          int h = it->id ? MENU_ITEM_H : MENU_SEP_H;
          if (it->id && ly >= y && ly < y + h) {
            pd->pressed = i;
            pd->hovered = i;
            invalidate_window(win);
            break;
          }
          y += h;
        }
        return true;
      }
      // Click outside popup bounds – close the popup
      {
        window_t *mb = pd->menubar;
        menubar_data_t *mbd = mb ? (menubar_data_t *)mb->userdata : NULL;
        if (mbd) {
          mbd->open_popup = NULL;
          mbd->active_idx = -1;
        }
        destroy_window(win);
        if (mb) invalidate_window(mb);
      }
      return true;
    }

    case kWindowMessageLeftButtonUp: {
      // Only act if user pressed inside the popup first
      if (pd->pressed < 0) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      // Find which item the mouse was released on
      int release_item = -1;
      if (lx >= 0 && lx < win->frame.w && ly >= 0 && ly < win->frame.h) {
        int y = MENU_START_Y;
        for (int i = 0; i < pd->item_count; i++) {
          const menu_item_t *it = &pd->items[i];
          int h = it->id ? MENU_ITEM_H : MENU_SEP_H;
          if (it->id && ly >= y && ly < y + h) {
            release_item = i;
            break;
          }
          y += h;
        }
      }
      window_t *mb = pd->menubar;
      menubar_data_t *mbd = mb ? (menubar_data_t *)mb->userdata : NULL;
      if (mbd) {
        mbd->open_popup = NULL;
        mbd->active_idx = -1;
      }
      if (release_item >= 0 && release_item == pd->pressed) {
        // Released on the same item that was pressed – fire action
        uint16_t item_id = pd->items[release_item].id;
        destroy_window(win);  // close popup before command runs (e.g. dialogs)
        if (mb) {
          invalidate_window(mb);
          send_message(mb, kWindowMessageCommand,
                       MAKEDWORD(item_id, kMenuBarNotificationItemClick),
                       NULL);
        }
      } else {
        // Released on a different item or outside – cancel, just close
        destroy_window(win);
        if (mb) invalidate_window(mb);
      }
      return true;
    }

    // Fallback: non-client mouse-up (should not fire with set_capture, kept for safety)
    case kWindowMessageNonClientLeftButtonUp: {
      window_t *mb = pd->menubar;
      menubar_data_t *mbd = mb ? (menubar_data_t *)mb->userdata : NULL;
      if (mbd) {
        mbd->open_popup = NULL;
        mbd->active_idx = -1;
      }
      destroy_window(win);
      if (mb) invalidate_window(mb);
      return true;
    }

    case kWindowMessageDestroy:
      free(win->userdata);
      win->userdata = NULL;
      set_capture(NULL);
      return true;

    default:
      return false;
  }
}

// ---- open / close popup --------------------------------------------------

static void close_popup(window_t *mb_win, menubar_data_t *data) {
  if (!data->open_popup) return;
  if (is_window(data->open_popup))
    destroy_window(data->open_popup);
  data->open_popup = NULL;
  data->active_idx = -1;
  if (mb_win) invalidate_window(mb_win);
}

static void open_popup(window_t *mb_win, menubar_data_t *data, int idx) {
  close_popup(mb_win, data);

  const menu_def_t *menu = &data->menus[idx];

  // Allocate popup data (flexible array)
  popup_data_t *pd = malloc(sizeof(popup_data_t) +
                            sizeof(menu_item_t) * menu->item_count);
  if (!pd) return;
  pd->menubar    = mb_win;
  pd->accel      = data->accel;
  pd->item_count = menu->item_count;
  pd->hovered    = -1;
  pd->pressed    = -1;
  for (int i = 0; i < menu->item_count; i++)
    pd->items[i] = menu->items[i];

  int pw = popup_width(menu, data->accel);
  int ph = popup_height(menu);
  int px = mb_win->frame.x + data->menu_x[idx] - 1; // TODO: why -1?
  int py = mb_win->frame.y + MENUBAR_HEIGHT;

  // Clamp to screen
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  if (px + pw > sw) px = sw - pw;
  if (px < 0)       px = 0;

  window_t *popup = create_window(
      "",
      WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(px, py, pw, ph),
      NULL, popup_proc, pd);
  popup->userdata = pd;
  show_window(popup, true);
  data->open_popup = popup;
  data->active_idx = idx;
  invalidate_window(popup);
  invalidate_window(mb_win);
}

// ---- menu bar proc -------------------------------------------------------

result_t win_menubar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  menubar_data_t *data = (menubar_data_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate: {
      menubar_data_t *d = malloc(sizeof(menubar_data_t));
      memset(d, 0, sizeof(menubar_data_t));
      d->active_idx = -1;
      win->userdata = d;
      return true;
    }

    case kMenuBarMessageSetMenus: {
      int count = (int)wparam;
      const menu_def_t *defs = (const menu_def_t *)lparam;
      if (!data) return false;
      free(data->menus);
      free(data->menu_x);
      data->count  = count;
      data->menus  = malloc(sizeof(menu_def_t) * count);
      data->menu_x = malloc(sizeof(int)        * count);
      if (!data->menus || !data->menu_x) return false;
      memcpy(data->menus, defs, sizeof(menu_def_t) * count);
      // Compute label x positions (window-local)
      int x = 4;
      for (int i = 0; i < count; i++) {
        data->menu_x[i] = x;
        x += strwidth(defs[i].label) + MENU_LABEL_PAD;
      }
      invalidate_window(win);
      return true;
    }

    case kMenuBarMessageSetAccelerators:
      if (data) data->accel = (accel_table_t *)lparam;
      return true;

    case kWindowMessagePaint: {
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      // Bottom border
      fill_rect(COLOR_DARK_EDGE, 0, win->frame.h - 1, win->frame.w, 1);
      if (!data || !data->menus) return true;
      for (int i = 0; i < data->count; i++) {
        bool active = (i == data->active_idx);
        int label_w = strwidth(data->menus[i].label) + MENU_LABEL_PAD;
        int label_x0 = data->menu_x[i] - 2;
        if (active) {
          fill_rect(COLOR_FOCUSED, label_x0, 0, label_w, win->frame.h - 1);
        }
        draw_text_small(data->menus[i].label,
                        data->menu_x[i], 2,
                        active ? COLOR_PANEL_BG : COLOR_TEXT_NORMAL);
      }
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      if (!data || !data->menus) return true;
      int lx = (int16_t)LOWORD(wparam);
      for (int i = 0; i < data->count; i++) {
        int label_w = strwidth(data->menus[i].label) + MENU_LABEL_PAD;
        int x0 = data->menu_x[i] - 2;
        int x1 = x0 + label_w;
        if (lx >= x0 && lx < x1) {
          open_popup(win, data, i);
          invalidate_window(win);
          return true;
        }
      }
      // Click outside any label – close any open popup
      close_popup(win, data);
      return true;
    }

    case kWindowMessageDisplayChange: {
      win->frame.w = LOWORD(wparam);
      invalidate_window(win);
      return false;
    }

    case kWindowMessageDestroy: {
      if (data) {
        close_popup(win, data);
        free(data->menus);
        free(data->menu_x);
        free(data);
        win->userdata = NULL;
      }
      return true;
    }

    default:
      return false;
  }
}
