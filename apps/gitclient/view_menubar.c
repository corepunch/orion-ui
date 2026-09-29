// Menu bar and command dispatch — uses generated menus from gitclient.orion.

#include "gitclient.h"
#include "gc_actions.h"
#include <orion/gem.h>

static bool gc_get_selected_branch(char *buf, int buf_sz, bool *is_current) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->branches_win || !gc->history_db) return false;
  int sel = (int)send_message(gc->branches_win, RVM_GETSELECTION, 0, NULL);
  if (sel < 0) return false;
  result_node_t *rows = (result_node_t *)send_db_message(
    gc->history_db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
  int row = 0;
  bool found = false;
  for (result_node_t *n = rows; n; n = n->next, row++) {
    if (row == sel) {
      db_branche_t *b = *(db_branche_t **)n->data;
      strncpy(buf, b->name, (size_t)buf_sz - 1);
      if (is_current) *is_current = b->is_current;
      found = true;
      break;
    }
  }
  free_result_list(rows);
  return found;
}

result_t gc_menubar_proc(window_t *win, uint32_t msg,
                         uint32_t wparam, void *lparam) {
  if (msg == evCommand &&
      (HIWORD(wparam) == kMenuBarNotificationItemClick ||
       HIWORD(wparam) == kAcceleratorNotification)) {
    (void)gc_execute_action(LOWORD(wparam));
    return true;
  }
  return win_menubar(win, msg, wparam, lparam);
}

result_t gc_toolbar_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
  (void)lparam;
  if (msg == tbButtonClick) {
    (void)gc_execute_action((uint16_t)wparam);
    return true;
  }
  return false;
}

void gc_create_menubar(void) {
  gc_state_t *gc = g_gc;
  if (!gc) return;

  gc->chrome_win = create_application_chrome(
      "Git Client Chrome",
      gc_menubar_proc,
      kGCMenus, kGCMenuCount,
      gc_toolbar_proc,
      &gitclient_application_toolbar,
      gc->hinstance);
  gc->menubar_win = gc->chrome_win ? app_chrome_menubar(gc->chrome_win) : NULL;
  gc->toolbar_win = gc->chrome_win ? app_chrome_toolbar(gc->chrome_win) : NULL;
  if (!gc->menubar_win) {
    gc->menubar_win = set_app_menu(gc_menubar_proc,
                                   kGCMenus, kGCMenuCount,
                                   gc_handle_command,
                                   gc->hinstance);
  }

  gc->accel = load_accelerators(gitclient_default_accels,
                                gitclient_default_accel_count);
  if (gc->menubar_win && gc->accel)
    send_message(gc->menubar_win, kMenuBarMessageSetAccelerators,
                 0, gc->accel);
}
