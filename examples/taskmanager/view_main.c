// VIEW: Main window procedure for the Task Manager.

#include "taskmanager.h"

// ============================================================
// Main window procedure
// ============================================================

result_t main_win_proc(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      // The list view is created as a child that fills the client area.
      g_app->list_win = create_window(
          "tasks",
          WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
          MAKERECT(0, 0, win->frame.w, win->frame.h),
          win, tasklist_proc, NULL);
      tasklist_refresh(g_app->list_win);
      app_update_status(g_app);
      return true;

    case kWindowMessageResize:
      if (g_app->list_win)
        resize_window(g_app->list_win, win->frame.w, win->frame.h);
      return false;

    case kWindowMessageCommand:
      // Forward menu commands and list notifications to menu handler.
      if (HIWORD(wparam) == kMenuBarNotificationItemClick) {
        handle_menu_command((uint16_t)LOWORD(wparam));
        return true;
      }
      // Forward task-list selection changes to update selected_idx.
      if (HIWORD(wparam) == CVN_SELCHANGE) {
        window_t *list = (window_t *)lparam;
        int sel = (int)send_message(list, CVM_GETSELECTION, 0, NULL);
        if (g_app) g_app->selected_idx = sel;
        return true;
      }
      if (HIWORD(wparam) == CVN_DBLCLK) {
        handle_menu_command(ID_TASK_EDIT);
        return true;
      }
      return false;

    case kWindowMessageClose:
      if (g_app && g_app->modified) {
        int r = message_box(win, "Discard unsaved changes and quit?",
                            "Quit", MB_YESNO);
        if (r != IDYES) return true;  // cancel the close
      }
      ui_request_quit();
      return true;

    default:
      return false;
  }
}
