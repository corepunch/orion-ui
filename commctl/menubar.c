// Menu bar control
// Provides a horizontal menu bar strip and popup dropdown menus.
//
// Usage:
//   1. Create a top-level window with win_menubar as the proc
//      (usually WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON |
//       WINDOW_NORESIZE, full screen width, height = MENUBAR_HEIGHT).
//   2. Send kMenuBarMessageSetMenus with your menu_def_t array.
//   3. Handle evCommand in the same proc (chain with win_menubar)
//      checking HIWORD(wparam) == kMenuBarNotificationItemClick.

#include <stdlib.h>
#include <string.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"
#include "../user/text.h"
#include "../user/accel.h"
#include "menubar.h"

#define MENU_ITEM_H      TITLEBAR_HEIGHT  // height of a normal dropdown row (font-size dependent)
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
  window_t      *parent_popup;
  window_t      *child_popup;
  accel_table_t *accel;       // for hotkey lookup; not owned (may be NULL)
  int            item_count;
  int            hovered;    // index of the item under the mouse (-1 if none)
  int            pressed;    // index of the item pressed on mouse-down (-1 if none)
  menu_item_t    items[];    // C99 flexible array
} popup_data_t;

// ---- helpers -------------------------------------------------------------

static bool menu_item_is_separator(const menu_item_t *it) {
  return !it || (!it->label && it->id == 0);
}

static bool menu_item_has_submenu(const menu_item_t *it) {
  return it && it->label && it->submenu_items && it->submenu_count > 0;
}

static bool menu_item_is_active(const menu_item_t *it) {
  return it && !menu_item_is_separator(it) && (it->id || menu_item_has_submenu(it));
}

// Return the display width of a label, stopping at a '\t' character.
static int item_label_width(const char *label) {
  if (!label) return 0;
  const char *tab = strchr(label, '\t');
  return tab ? strnwidth(label, (int)(tab - label)) : strwidth(label);
}

static const char *item_label_shortcut(const char *label) {
  if (!label) return NULL;
  const char *tab = strchr(label, '\t');
  return (tab && tab[1]) ? tab + 1 : NULL;
}

// Draw a menu item label, stopping at a '\t' character.
static void draw_item_label(const char *label, irect16_t const *rect, uint32_t col) {
  if (!label) return;
  const char *tab = strchr(label, '\t');
  if (tab) {
    int len = (int)(tab - label);
    char buf[256];
    int n = len < (int)(sizeof(buf) - 1) ? len : (int)(sizeof(buf) - 1);
    memcpy(buf, label, (size_t)n);
    buf[n] = '\0';
    draw_text_small_clipped(buf, rect, col, 0);
  } else {
    draw_text_small_clipped(label, rect, col, 0);
  }
}

static int popup_items_height(const menu_item_t *items, int item_count) {
  int h = MENU_START_Y * 2; // top and bottom padding
  for (int i = 0; i < item_count; i++)
    h += menu_item_is_separator(&items[i]) ? MENU_SEP_H : MENU_ITEM_H;
  return h;
}

static int popup_items_width(const menu_item_t *items, int item_count,
                             const accel_table_t *accel) {
  int w = MENU_MIN_W;
  for (int i = 0; i < item_count; i++) {
    const menu_item_t *it = &items[i];
    if (menu_item_is_active(it)) {
      int lw = item_label_width(it->label) + MENU_SIDE_PAD * 2;
      if (menu_item_has_submenu(it)) {
        lw += strwidth(">") + MENU_HOTKEY_GAP;
      } else if (item_label_shortcut(it->label)) {
        lw += MENU_HOTKEY_GAP + strwidth(item_label_shortcut(it->label)) + MENU_SIDE_PAD;
      } else if (accel) {
        const accel_t *a = accel_find_cmd(accel, it->id);
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

static int popup_item_at(const popup_data_t *pd, int lx, int ly) {
  if (!pd || lx < 0 || ly < 0) return -1;
  int y = MENU_START_Y;
  for (int i = 0; i < pd->item_count; i++) {
    const menu_item_t *it = &pd->items[i];
    int h = menu_item_is_separator(it) ? MENU_SEP_H : MENU_ITEM_H;
    if (menu_item_is_active(it) && ly >= y && ly < y + h)
      return i;
    y += h;
  }
  return -1;
}

static int popup_item_y(const popup_data_t *pd, int index) {
  int y = MENU_START_Y;
  if (!pd) return y;
  for (int i = 0; i < index && i < pd->item_count; i++)
    y += menu_item_is_separator(&pd->items[i]) ? MENU_SEP_H : MENU_ITEM_H;
  return y;
}

static void close_popup_tree(window_t *popup);
static void open_submenu_popup(window_t *popup, popup_data_t *pd, int index);

// ---- popup window proc ---------------------------------------------------

static result_t popup_proc(window_t *win, uint32_t msg,
                            uint32_t wparam, void *lparam) {
  popup_data_t *pd = (popup_data_t *)win->userdata;
  switch (msg) {
    case evCreate:
      pd = (popup_data_t *)lparam;
      win->userdata = pd;
      pd->hovered = -1;
      pd->pressed = -1;
      set_capture(win);    // receive ALL mouse events, even outside our bounds
      return true;

    case evPaint: {
      // Background
      fill_rect(get_sys_color(brWindowBg), R(0, 0, win->frame.w, win->frame.h));
      // Border
      fill_rect(get_sys_color(brDarkEdge), R(0,               0,               win->frame.w, 1));
      fill_rect(get_sys_color(brDarkEdge), R(0,               win->frame.h - 1, win->frame.w, 1));
      fill_rect(get_sys_color(brDarkEdge), R(0,               0,               1, win->frame.h));
      fill_rect(get_sys_color(brDarkEdge), R(win->frame.w - 1, 0,               1, win->frame.h));
      // Items
      int y = MENU_START_Y;
      for (int i = 0; i < pd->item_count; i++) {
        const menu_item_t *it = &pd->items[i];
        if (menu_item_is_separator(it)) {
          // separator
          fill_rect(get_sys_color(brDarkEdge), R(MENU_SIDE_PAD, y + 2,
                    win->frame.w - MENU_SIDE_PAD * 2, 1));
          y += MENU_SEP_H;
        } else {
          if (i == pd->hovered) {
            fill_rect(get_sys_color(brFocusRing), R(1, y, win->frame.w - 2, MENU_ITEM_H));
          }
          bool hov = (i == pd->hovered);
          uint32_t label_col  = hov ? get_sys_color(brWindowBg)  : get_sys_color(brTextNormal);
          uint32_t hotkey_col = hov ? get_sys_color(brWindowBg)  : get_sys_color(brTextDisabled);
          draw_item_label(it->label, &(irect16_t){MENU_SIDE_PAD, y, win->frame.w - MENU_SIDE_PAD * 2, MENU_ITEM_H}, label_col);
          if (menu_item_has_submenu(it)) {
            draw_text_small_clipped(">",
                                   &(irect16_t){0, y, win->frame.w - MENU_SIDE_PAD, MENU_ITEM_H},
                                   hotkey_col, TEXT_ALIGN_RIGHT);
          } else if (item_label_shortcut(it->label)) {
            draw_text_small_clipped(item_label_shortcut(it->label),
                                   &(irect16_t){0, y, win->frame.w - MENU_SIDE_PAD, MENU_ITEM_H},
                                   hotkey_col, TEXT_ALIGN_RIGHT);
          } else if (pd->accel) {
            const accel_t *a = accel_find_cmd(pd->accel, it->id);
            if (a) {
              char hkbuf[32];
              accel_format(a, hkbuf, sizeof(hkbuf));
              draw_text_small_clipped(hkbuf,
                                     &(irect16_t){0, y, win->frame.w - MENU_SIDE_PAD, MENU_ITEM_H},
                                     hotkey_col, TEXT_ALIGN_RIGHT);
            }
          }
          y += MENU_ITEM_H;
        }
      }
      return true;
    }

    case evMouseMove: {
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int new_hovered = -1;
      if (lx >= 0 && lx < win->frame.w && ly >= 0 && ly < win->frame.h) {
        new_hovered = popup_item_at(pd, lx, ly);
      }
      if (new_hovered != pd->hovered) {
        pd->hovered = new_hovered;
        if (pd->child_popup) {
          close_popup_tree(pd->child_popup);
          pd->child_popup = NULL;
        }
        if (new_hovered >= 0 && menu_item_has_submenu(&pd->items[new_hovered]))
          open_submenu_popup(win, pd, new_hovered);
        invalidate_window(win);
      }
      return true;
    }

    case evLeftButtonDown: {
      // Coords are popup-window-local (set_capture ensures we get them)
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      // Click inside the popup – record which item was pressed
      if (lx >= 0 && lx < win->frame.w && ly >= 0 && ly < win->frame.h) {
        pd->pressed = popup_item_at(pd, lx, ly);
        pd->hovered = pd->pressed;
        if (pd->pressed >= 0 && menu_item_has_submenu(&pd->items[pd->pressed])) {
          open_submenu_popup(win, pd, pd->pressed);
          pd->pressed = -1;
        }
        invalidate_window(win);
        return true;
      }
      // Click outside popup bounds – close the popup
      {
        window_t *mb = pd->menubar;
        menubar_data_t *mbd = mb ? (menubar_data_t *)mb->userdata : NULL;
        window_t *root_popup = (mbd && mbd->open_popup) ? mbd->open_popup : win;
        if (mbd) {
          mbd->open_popup = NULL;
          mbd->active_idx = -1;
        }
        close_popup_tree(root_popup);
        if (mb) invalidate_window(mb);
      }
      return true;
    }

    case evLeftButtonUp: {
      // Only act if user pressed inside the popup first
      if (pd->pressed < 0) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      // Find which item the mouse was released on
      int release_item = -1;
      if (lx >= 0 && lx < win->frame.w && ly >= 0 && ly < win->frame.h) {
        release_item = popup_item_at(pd, lx, ly);
      }
      window_t *mb = pd->menubar;
      menubar_data_t *mbd = mb ? (menubar_data_t *)mb->userdata : NULL;
      window_t *root_popup = (mbd && mbd->open_popup) ? mbd->open_popup : win;
      if (mbd) {
        mbd->open_popup = NULL;
        mbd->active_idx = -1;
      }
      if (release_item >= 0 && release_item == pd->pressed &&
          !menu_item_has_submenu(&pd->items[release_item])) {
        // Released on the same item that was pressed – fire action
        uint16_t item_id = pd->items[release_item].id;
        close_popup_tree(root_popup);  // close popup before command runs
        if (mb) {
          invalidate_window(mb);
          send_message(mb, evCommand,
                       MAKEDWORD(item_id, kMenuBarNotificationItemClick),
                       NULL);
        }
      } else {
        // Released on a different item or outside – cancel, just close
        close_popup_tree(root_popup);
        if (mb) invalidate_window(mb);
      }
      return true;
    }

    // Fallback: non-client mouse-up (should not fire with set_capture, kept for safety)
    case evNCLeftButtonUp: {
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

    case evDestroy:
      if (pd && pd->child_popup && is_window(pd->child_popup)) {
        window_t *child = pd->child_popup;
        pd->child_popup = NULL;
        close_popup_tree(child);
      }
      if (pd && pd->parent_popup && is_window(pd->parent_popup)) {
        popup_data_t *parent_pd = (popup_data_t *)pd->parent_popup->userdata;
        if (parent_pd && parent_pd->child_popup == win)
          parent_pd->child_popup = NULL;
        set_capture(pd->parent_popup);
      } else {
        set_capture(NULL);
      }
      free(win->userdata);
      win->userdata = NULL;
      return true;

    default:
      return false;
  }
}

// ---- open / close popup --------------------------------------------------

static void close_popup(window_t *mb_win, menubar_data_t *data) {
  if (!data->open_popup) return;
  if (is_window(data->open_popup))
    close_popup_tree(data->open_popup);
  data->open_popup = NULL;
  data->active_idx = -1;
  if (mb_win) invalidate_window(mb_win);
}

static window_t *create_popup_window(window_t *mb_win, window_t *parent_popup,
                                     const menu_item_t *items, int item_count,
                                     accel_table_t *accel, int px, int py) {
  if (!mb_win || !items || item_count <= 0) return NULL;
  popup_data_t *pd = malloc(sizeof(popup_data_t) +
                            sizeof(menu_item_t) * item_count);
  if (!pd) return NULL;
  pd->menubar = mb_win;
  pd->parent_popup = parent_popup;
  pd->child_popup = NULL;
  pd->accel = accel;
  pd->item_count = item_count;
  pd->hovered = -1;
  pd->pressed = -1;
  for (int i = 0; i < item_count; i++)
    pd->items[i] = items[i];

  int pw = popup_items_width(items, item_count, accel);
  int ph = popup_items_height(items, item_count);

  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  if (px + pw > sw) px = sw - pw;
  if (py + ph > sh) py = sh - ph;
  if (px < 0) px = 0;
  if (py < 0) py = 0;

  window_t *popup = create_window(
      "",
      WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(px, py, pw, ph),
      NULL, popup_proc, mb_win->hinstance, pd);
  if (!popup) {
    free(pd);
    return NULL;
  }
  popup->userdata = pd;
  show_window(popup, true);
  invalidate_window(popup);
  return popup;
}

static void close_popup_tree(window_t *popup) {
  if (!popup || !is_window(popup)) return;
  popup_data_t *pd = (popup_data_t *)popup->userdata;
  if (pd && pd->child_popup && is_window(pd->child_popup)) {
    window_t *child = pd->child_popup;
    pd->child_popup = NULL;
    close_popup_tree(child);
  }
  destroy_window(popup);
}

static void open_submenu_popup(window_t *popup, popup_data_t *pd, int index) {
  if (!popup || !pd || index < 0 || index >= pd->item_count) return;
  const menu_item_t *it = &pd->items[index];
  if (!menu_item_has_submenu(it)) return;
  if (pd->child_popup && is_window(pd->child_popup))
    close_popup_tree(pd->child_popup);
  pd->child_popup = NULL;
  int px = popup->frame.x + popup->frame.w - 2;
  int py = popup->frame.y + popup_item_y(pd, index);
  pd->child_popup = create_popup_window(pd->menubar, popup,
                                        it->submenu_items, it->submenu_count,
                                        pd->accel, px, py);
}

static void open_popup(window_t *mb_win, menubar_data_t *data, int idx) {
  close_popup(mb_win, data);

  const menu_def_t *menu = &data->menus[idx];
  int px = mb_win->frame.x + data->menu_x[idx] - 1; // TODO: why -1?
  int py = mb_win->frame.y + MENUBAR_HEIGHT;

  window_t *popup = create_popup_window(mb_win, NULL, menu->items,
                                        menu->item_count, data->accel,
                                        px, py);
  if (!popup) return;
  data->open_popup = popup;
  data->active_idx = idx;
  invalidate_window(mb_win);
}

// ---- menu bar proc -------------------------------------------------------

result_t win_menubar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  menubar_data_t *data = (menubar_data_t *)win->userdata;
  switch (msg) {
    case evCreate: {
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

    case evPaint: {
      fill_rect(get_sys_color(brWindowDarkBg), R(0, 0, win->frame.w, win->frame.h));
      // Bottom border
      fill_rect(get_sys_color(brDarkEdge), R(0, win->frame.h - 1, win->frame.w, 1));
      if (!data || !data->menus) return true;
      for (int i = 0; i < data->count; i++) {
        bool active = (i == data->active_idx);
        int label_w = strwidth(data->menus[i].label) + MENU_LABEL_PAD;
        int label_x0 = data->menu_x[i] - 2;
        if (active) {
          fill_rect(get_sys_color(brFocusRing), R(label_x0, 0, label_w, win->frame.h - 1));
        }
        draw_text_small_clipped(data->menus[i].label,
                        &(irect16_t){data->menu_x[i], 0, label_w, win->frame.h},
                        active ? get_sys_color(brWindowBg) : get_sys_color(brTextNormal), 0);
      }
      return true;
    }

    case evLeftButtonDown: {
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

    case evDisplayChange: {
      win->frame.w = LOWORD(wparam);
      return false;
    }

    case evDestroy: {
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
