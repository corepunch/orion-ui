// The operating system's own chrome, where it has any: the application menu
// in the system menu bar, and a window whose title bar hides behind the
// content so that an application's top row (a ribbon's tabs) takes its place.
// Platforms without them answer false and the application shows its own.

#include <stdio.h>

#include "user.h"

static window_t *g_native_menu_owner;
static isize16_t g_native_controls;

bool ui_set_native_menu(window_t *owner, const ax_menu_item_t *menus, int count) {
  if (!owner || count < 0 || (count && !menus)) {
    fprintf(stderr, "[native] menu rejected owner=%p menus=%p count=%d\n", (void *)owner, (void *)menus, count);
    fflush(stderr);
    return false;
  }
  if (!axSetApplicationMenu(menus, (uint32_t)count)) return false;
  g_native_menu_owner = owner;
  return true;
}

// A menu choice arrives like its accelerator: the same command, to the same window.
void ui_native_menu_command(uint32_t command) {
  if (is_window(g_native_menu_owner))
    send_message(g_native_menu_owner, evCommand, MAKEDWORD((uint16_t)command, kAcceleratorNotification), NULL);
}

bool ui_set_native_titlebar_hidden(bool hidden) {
  uint32_t w = 0, h = 0;
  if (!axSetTitlebarHidden(hidden, &w, &h)) return false;
  g_native_controls = (isize16_t){(int)(w / UI_WINDOW_SCALE), (int)(h / UI_WINDOW_SCALE)};
  return true;
}

isize16_t ui_native_window_controls(void) { return g_native_controls; }

bool ui_drag_native_window(void) { return axDragWindow(); }
