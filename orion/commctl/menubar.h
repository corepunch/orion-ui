#ifndef __UI_MENUBAR_H__
#define __UI_MENUBAR_H__

#include <orion/user/user.h>

// Height of the menu bar strip — matches the generous dropdown row rhythm.
// FONT_SIZE is a compile-time constant from kernel/kernel.h (included via user/user.h).
#define MENUBAR_HEIGHT (FONT_SIZE + 12)

// One item inside a dropdown menu.  label == NULL && id == 0 means separator.
// Submenus use id == 0 with a non-NULL label and submenu_items/submenu_count.
typedef struct menu_item_s {
  const char        *label;
  uint16_t           id;
  const struct menu_item_s *submenu_items;
  int                submenu_count;
} menu_item_t;

// A top-level menu entry, e.g. "File" with its list of dropdown items.
typedef struct {
  const char        *label;
  const menu_item_t *items;
  int                item_count;
} menu_def_t;

// Send to a menu-bar window to configure its menus.
//   wparam = number of menu_def_t entries
//   lparam = const menu_def_t *  (caller owns; structs are shallow-copied)
#define kMenuBarMessageSetMenus (evUser + 200)

// Send to a menu-bar window to associate an accelerator table for hotkey hints.
// The menu bar stores a reference (not a copy); the caller must keep the table
// alive at least as long as the menu-bar window.
//   wparam = 0 (unused)
//   lparam = const accel_table_t *  (pass NULL to clear)
#define kMenuBarMessageSetAccelerators (evUser + 201)

// Notification code placed in HIWORD(wparam) of evCommand that
// win_menubar sends to *itself* when the user selects an item.
//   LOWORD(wparam) = the selected item's id field
#define kMenuBarNotificationItemClick 300

result_t win_menubar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Show a context-menu popup at screen coordinates.
// Items are shallow-copied; the caller must keep the menu_item_t array alive
// only until this function returns (the array is copied into the popup).
// The popup sends evCommand to notify_win with:
//   HIWORD(wparam) = kMenuBarNotificationItemClick
//   LOWORD(wparam) = the selected item's id
// Returns true on success.
bool show_popup_menu(window_t *notify_win, const menu_item_t *items,
                     int item_count, int screen_x, int screen_y);

#endif  /* __UI_MENUBAR_H__ */
