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

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/text.h>
#include <orion/user/accel.h>
#include <orion/user/theme.h>
#include "menubar.h"
#include "popup_item.h"

#define MENU_ITEM_H      POPUP_ITEM_HEIGHT

// ---- per-menubar userdata -----------------------------------------------

typedef struct {
  menu_def_t      *menus;       // shallow copy of the menu_def_t array
  int              count;       // number of menus
  int             *menu_x;      // x offset for each label (window-local)
  window_t        *open_popup;  // currently visible dropdown, or NULL
  int              active_idx;  // index of the currently open menu label (-1 if none)
  window_t        *restore_pressed;
  accel_table_t   *accel;       // optional accelerator table for hotkey hints (not owned)
} menubar_data_t;

// ---- per-popup userdata (flexible array, malloc'd) ----------------------

typedef struct {
  window_t      *menubar;
  window_t      *notify_win;  // where to send evCommand (falls back to menubar)
  window_t      *parent_popup;
  window_t      *child_popup;
  accel_table_t *accel;       // for hotkey lookup; not owned (may be NULL)
  int            item_count;
  int            hovered;    // index of the item under the mouse (-1 if none)
  int            pressed;    // index of the item pressed on mouse-down (-1 if none)
  menu_item_t    items[];    // C99 flexible array
} popup_data_t;

static window_t *s_context_popup;

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

static const char *item_shortcut(const menu_item_t *it, const accel_table_t *accel,
                                 char *buf, int size) {
  const accel_t *a = accel_find_cmd(accel, it->id);
  if (a) {
    accel_format(a, buf, size);
    return buf;
  }
  return item_label_shortcut(it->label);
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
      } else {
        char hkbuf[128];
        const char *shortcut = item_shortcut(it, accel, hkbuf, sizeof(hkbuf));
        if (shortcut) lw += MENU_HOTKEY_GAP + strwidth(shortcut);
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

static bool popup_contains_screen_point(const window_t *popup, int sx, int sy) {
  return popup && sx >= popup->frame.x && sy >= popup->frame.y &&
         sx < popup->frame.x + popup->frame.w &&
         sy < popup->frame.y + popup->frame.h;
}

static bool popup_forward_to_parent_if_inside(window_t *win, popup_data_t *pd,
                                              uint32_t msg, int lx, int ly) {
  window_t *parent;
  if (!win || !pd || !pd->parent_popup || !is_window(pd->parent_popup))
    return false;

  parent = pd->parent_popup;
  {
    int sx = win->frame.x + lx;
    int sy = win->frame.y + ly;
    if (!popup_contains_screen_point(parent, sx, sy))
      return false;
    send_message(parent, msg,
                 MAKEDWORD((uint16_t)(sx - parent->frame.x),
                           (uint16_t)(sy - parent->frame.y)),
                 NULL);
    return true;
  }
}

static void close_popup_tree(window_t *popup);
static void open_submenu_popup(window_t *popup, popup_data_t *pd, int index);

static window_t *popup_root(window_t *popup) {
  popup_data_t *pd;
  while (popup && (pd = (popup_data_t *)popup->userdata) && pd->parent_popup)
    popup = pd->parent_popup;
  return popup;
}

static void dismiss_popup(window_t *popup) {
  window_t *root = popup_root(popup);
  popup_data_t *pd = root ? (popup_data_t *)root->userdata : NULL;
  window_t *mb = pd ? pd->menubar : NULL;
  if (root == s_context_popup) s_context_popup = NULL;
  if (mb && is_window(mb)) {
    menubar_data_t *mbd = (menubar_data_t *)mb->userdata;
    if (mbd) { mbd->open_popup = NULL; mbd->active_idx = -1; }
  }
  close_popup_tree(root);
  if (mb && is_window(mb)) invalidate_window(mb);
}

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
      theme_draw(THEME_PART_MENU_POPUP, R(0, 0, win->frame.w, win->frame.h), CTRL_NORMAL);
      // Items
      int y = MENU_START_Y;
      for (int i = 0; i < pd->item_count; i++) {
        const menu_item_t *it = &pd->items[i];
        if (menu_item_is_separator(it)) {
          // separator
          theme_draw(THEME_PART_SEPARATOR, R(0, y + MENU_SEP_H / 2,
                    win->frame.w, 1), CTRL_NORMAL);
          y += MENU_SEP_H;
        } else {
          bool hov = (i == pd->hovered);
          uint32_t label_col  = theme_foreground(THEME_PART_MENU_ITEM,
                                                  hov ? CTRL_HOVER : CTRL_NORMAL);
          uint32_t hotkey_col = hov ? label_col : get_sys_color(brTextDisabled);
          popup_item_paint(R(0, y, win->frame.w, MENU_ITEM_H), it->label,
                           hov ? CTRL_HOVER : CTRL_NORMAL);
          if (menu_item_has_submenu(it)) {
            draw_text_small_clipped(">",
                                   &(irect16_t){0, y, win->frame.w - MENU_SIDE_PAD, MENU_ITEM_H},
                                   hotkey_col, TEXT_ALIGN_RIGHT);
          } else {
            char hkbuf[128];
            const char *shortcut = item_shortcut(it, pd->accel, hkbuf, sizeof(hkbuf));
            if (shortcut) {
              draw_text_small_clipped(shortcut,
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
      if (popup_forward_to_parent_if_inside(win, pd, evMouseMove, lx, ly))
        return true;
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
      if (popup_forward_to_parent_if_inside(win, pd, evLeftButtonDown, lx, ly))
        return true;
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
      dismiss_popup(win);
      return true;
    }

    case evLeftButtonUp: {
      // Only act if user pressed inside the popup first
      if (pd->pressed < 0) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      if (popup_forward_to_parent_if_inside(win, pd, evLeftButtonUp, lx, ly))
        return true;
      // Find which item the mouse was released on
      int release_item = -1;
      if (lx >= 0 && lx < win->frame.w && ly >= 0 && ly < win->frame.h) {
        release_item = popup_item_at(pd, lx, ly);
      }
      window_t *target = pd->notify_win ? pd->notify_win : pd->menubar;
      if (release_item >= 0 && release_item == pd->pressed &&
          !menu_item_has_submenu(&pd->items[release_item])) {
        uint16_t item_id = pd->items[release_item].id;
        dismiss_popup(win);
        if (target) {
          send_message(target, evCommand,
                       MAKEDWORD(item_id, kMenuBarNotificationItemClick),
                       NULL);
        }
      } else {
        dismiss_popup(win);
      }
      return true;
    }

    // Fallback: non-client mouse-up (should not fire with set_capture, kept for safety)
    case evNCLeftButtonUp: {
      dismiss_popup(win);
      return true;
    }

    case evDestroy:
      if (win == s_context_popup) s_context_popup = NULL;
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
                                     accel_table_t *accel, int px, int py,
                                     window_t *notify_win) {
  if (!items || item_count <= 0) return NULL;
  popup_data_t *pd = malloc(sizeof(popup_data_t) +
                             sizeof(menu_item_t) * item_count);
  if (!pd) return NULL;
  pd->menubar = mb_win;
  pd->notify_win = notify_win;
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
  if (sw > 0 && sh > 0) {
    if (px + pw > sw) px = sw - pw;
    if (py + ph > sh) py = sh - ph;
    if (px < 0) px = 0;
    if (py < 0) py = 0;
  }

  window_t *popup = create_window(
      "",
      WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(px, py, pw, ph),
      NULL, popup_proc,
      notify_win ? notify_win->hinstance : (mb_win ? mb_win->hinstance : 0), pd);
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
                                        pd->accel, px, py, pd->notify_win);
}

static void open_popup(window_t *mb_win, menubar_data_t *data, int idx) {
  close_popup(mb_win, data);

  const menu_def_t *menu = &data->menus[idx];
  int px = window_screen_x(mb_win) + data->menu_x[idx] - 1; // TODO: why -1?
  int py = window_screen_y(mb_win) + MENUBAR_HEIGHT;

  window_t *popup = create_popup_window(mb_win, NULL, menu->items,
                                        menu->item_count, data->accel,
                                        px, py, NULL);
  if (!popup) return;
  data->open_popup = popup;
  data->active_idx = idx;
  invalidate_window(mb_win);
}

// ---- menu bar proc -------------------------------------------------------

static window_t *menubar_maximized_window(window_t *win) {
  hinstance_t owner = get_root_window(win)->hinstance;
  window_t *target = NULL;
  for (window_t *root = g_ui_runtime.windows; root; root = root->next)
    if (root->hinstance == owner && root->maximized &&
        window_has_state(root, WINDOW_STATE_VISIBLE)) target = root;
  return target;
}

static irect16_t menubar_restore_rect(window_t *win) {
  irect16_t client = get_client_rect(win);
  return rect_split_right(client, client.h);
}

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

    case kMenuBarMessageGetContentWidth:
      if (!data || !data->count) return 4;
      return data->menu_x[data->count - 1] + strwidth(data->menus[data->count - 1].label) + MENU_LABEL_PAD;

    case evPaint: {
      theme_draw(THEME_PART_MENU_BAR, R(0, 0, win->frame.w, win->frame.h), CTRL_NORMAL);
      window_t *maximized = menubar_maximized_window(win);
      irect16_t restore = menubar_restore_rect(win);
      if (maximized) {
        draw_theme_icon_in_rect(THEME_ICON_RESTORE, restore, get_sys_color(brTextNormal));
      }
      if (!data || !data->menus) return true;
      if (data->active_idx >= 0 && data->active_idx < data->count) {
        int i = data->active_idx;
        irect16_t selection = R(data->menu_x[i] - 2, 0,
            strwidth(data->menus[i].label) + MENU_LABEL_PAD, win->frame.h - 1);
        // Capsule padding can overlap adjacent hit targets; paint behind all labels.
        if (get_theme()->style == THEME_MODERN) {
          selection = rect_center(selection, strwidth(data->menus[i].label), selection.h);
          selection = rect_inset_xy(selection, -(MENU_CAPSULE_PAD + MENU_CAPSULE_INSET), 0);
        }
        theme_draw(THEME_PART_MENU_ITEM, selection, CTRL_SELECTED);
      }
      for (int i = 0; i < data->count; i++) {
        bool active = (i == data->active_idx);
        int label_w = strwidth(data->menus[i].label) + MENU_LABEL_PAD;
        int label_x0 = data->menu_x[i] - 2;
        if (maximized && label_x0 + label_w > restore.x) break;
        irect16_t label_rect = {label_x0, 0, label_w, win->frame.h};
        draw_text_small_clipped(data->menus[i].label, &label_rect,
                        theme_foreground(THEME_PART_MENU_ITEM,
                                         active ? CTRL_SELECTED : CTRL_NORMAL),
                        TEXT_ALIGN_CENTER);
      }
      return true;
    }

    case evLeftButtonDown: {
      if (!data) return true;
      window_t *target = menubar_maximized_window(win);
      ipoint16_t point = {(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
      if (target && rect_contains_point(menubar_restore_rect(win), point)) {
        close_popup(win, data);
        data->restore_pressed = target;
        fprintf(stderr, "[mb] restore press win=%u target=%u\n", win->id, target->id);
        fflush(stderr);
        set_capture(win);
        invalidate_window(win);
        return true;
      }
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

    case evLeftButtonUp: {
      if (!data || !data->restore_pressed) return false;
      window_t *target = data->restore_pressed;
      data->restore_pressed = NULL;
      set_capture(NULL);
      ipoint16_t point = {(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
      if (target == menubar_maximized_window(win) && rect_contains_point(menubar_restore_rect(win), point)) {
        fprintf(stderr, "[mb] restore click win=%u target=%u\n", win->id, target->id);
        fflush(stderr);
        restore_window(target);
      }
      invalidate_window(win);
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

// ---- public context-menu API ------------------------------------------------

bool show_popup_menu(window_t *notify_win, const menu_item_t *items,
                     int item_count, int screen_x, int screen_y) {
  if (!notify_win || !items || item_count <= 0) return false;
  if (s_context_popup && is_window(s_context_popup)) dismiss_popup(s_context_popup);
  window_t *popup = create_popup_window(NULL, NULL, items, item_count,
                                        NULL, screen_x, screen_y, notify_win);
  s_context_popup = popup;
  return popup != NULL;
}
