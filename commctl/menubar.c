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
#include "menubar.h"

#define MENU_ITEM_H      12   // height of a normal dropdown row
#define MENU_SEP_H        5   // height of a separator row
#define MENU_SIDE_PAD     4   // horizontal text padding inside dropdown
#define MENU_MIN_W       90   // minimum popup width
#define MENU_LABEL_PAD   12   // extra space around each top-level label

// ---- per-menubar userdata -----------------------------------------------

typedef struct {
  menu_def_t *menus;       // shallow copy of the menu_def_t array
  int         count;       // number of menus
  int        *menu_x;      // x offset for each label (window-local)
  window_t   *open_popup;  // currently visible dropdown, or NULL
} menubar_data_t;

// ---- per-popup userdata (flexible array, malloc'd) ----------------------

typedef struct {
  window_t    *menubar;
  int          item_count;
  menu_item_t  items[];    // C99 flexible array
} popup_data_t;

// ---- helpers -------------------------------------------------------------

static int popup_height(const menu_def_t *m) {
  int h = 4;
  for (int i = 0; i < m->item_count; i++)
    h += m->items[i].id ? MENU_ITEM_H : MENU_SEP_H;
  return h;
}

static int popup_width(const menu_def_t *m) {
  int w = MENU_MIN_W;
  for (int i = 0; i < m->item_count; i++) {
    if (m->items[i].id) {
      int tw = strwidth(m->items[i].label) + MENU_SIDE_PAD * 2;
      if (tw > w) w = tw;
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
      int y = 2;
      for (int i = 0; i < pd->item_count; i++) {
        const menu_item_t *it = &pd->items[i];
        if (!it->id) {
          // separator
          fill_rect(COLOR_DARK_EDGE, MENU_SIDE_PAD, y + 2,
                    win->frame.w - MENU_SIDE_PAD * 2, 1);
          y += MENU_SEP_H;
        } else {
          draw_text_small(it->label, MENU_SIDE_PAD + 2, y + 2,
                          COLOR_TEXT_NORMAL);
          y += MENU_ITEM_H;
        }
      }
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      // Coords are popup-window-local (set_capture ensures we get them)
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      // Click inside the popup – hit-test items
      if (lx >= 0 && lx < win->frame.w && ly >= 0 && ly < win->frame.h) {
        int y = 2;
        for (int i = 0; i < pd->item_count; i++) {
          const menu_item_t *it = &pd->items[i];
          int h = it->id ? MENU_ITEM_H : MENU_SEP_H;
          if (it->id && ly >= y && ly < y + h) {
            window_t *mb = pd->menubar;
            menubar_data_t *mbd = mb ? (menubar_data_t *)mb->userdata : NULL;
            if (mbd) mbd->open_popup = NULL;
            // Send command to menubar window (application intercepts it there)
            if (mb) {
              send_message(mb, kWindowMessageCommand,
                           MAKEDWORD(it->id, kMenuBarNotificationItemClick),
                           NULL);
            }
            destroy_window(win);  // WM_DESTROY releases capture + frees pd
            return true;
          }
          y += h;
        }
        return true;  // click inside but not on an item
      }
      // Fall through to non-client handling (click outside)
      return false;
    }

    case kWindowMessageLeftButtonUp:
      return true;

    // With set_capture, a click outside our client rect arrives here
    case kWindowMessageNonClientLeftButtonUp: {
      menubar_data_t *mbd =
          pd->menubar ? (menubar_data_t *)pd->menubar->userdata : NULL;
      if (mbd) mbd->open_popup = NULL;
      destroy_window(win);
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

static void close_popup(menubar_data_t *data) {
  if (!data->open_popup) return;
  if (is_window(data->open_popup))
    destroy_window(data->open_popup);
  data->open_popup = NULL;
}

static void open_popup(window_t *mb_win, menubar_data_t *data, int idx) {
  close_popup(data);

  const menu_def_t *menu = &data->menus[idx];

  // Allocate popup data (flexible array)
  popup_data_t *pd = malloc(sizeof(popup_data_t) +
                            sizeof(menu_item_t) * menu->item_count);
  if (!pd) return;
  pd->menubar    = mb_win;
  pd->item_count = menu->item_count;
  for (int i = 0; i < menu->item_count; i++)
    pd->items[i] = menu->items[i];

  int pw = popup_width(menu);
  int ph = popup_height(menu);
  int px = mb_win->frame.x + data->menu_x[idx];
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
  invalidate_window(popup);
}

// ---- menu bar proc -------------------------------------------------------

result_t win_menubar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  menubar_data_t *data = (menubar_data_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate: {
      menubar_data_t *d = malloc(sizeof(menubar_data_t));
      memset(d, 0, sizeof(menubar_data_t));
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

    case kWindowMessagePaint: {
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      // Bottom border
      fill_rect(COLOR_DARK_EDGE, 0, win->frame.h - 1, win->frame.w, 1);
      if (!data || !data->menus) return true;
      for (int i = 0; i < data->count; i++) {
        bool active = data->open_popup && (i == i); // highlight active label
        (void)active; // simple: just draw all labels the same for now
        draw_text_small(data->menus[i].label,
                        data->menu_x[i], 2, COLOR_TEXT_NORMAL);
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
      close_popup(data);
      return true;
    }

    case kWindowMessageDestroy: {
      if (data) {
        close_popup(data);
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
