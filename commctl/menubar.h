#ifndef __UI_MENUBAR_H__
#define __UI_MENUBAR_H__

#include "../user/user.h"

// Height of the menu bar strip (matches TITLEBAR_HEIGHT)
#define MENUBAR_HEIGHT 12

// One item inside a dropdown menu.  id == 0 means a horizontal separator.
typedef struct {
  const char *label;
  uint16_t    id;
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
#define kMenuBarMessageSetMenus (kWindowMessageUser + 200)

// Notification code placed in HIWORD(wparam) of kWindowMessageCommand that
// win_menubar sends to *itself* when the user selects an item.
//   LOWORD(wparam) = the selected item's id field
#define kMenuBarNotificationItemClick 300

result_t win_menubar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif  /* __UI_MENUBAR_H__ */
