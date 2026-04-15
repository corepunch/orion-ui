// VIEW: Main window procedure for the Task Manager.

#include "taskmanager.h"

// ============================================================
// Toolbar button definitions
// ============================================================

static const toolbar_button_t kMainToolbar[] = {
  { sysicon_add,    ID_TASK_NEW,    false },
  { sysicon_pencil, ID_TASK_EDIT,   false },
  { sysicon_delete, ID_TASK_DELETE, false },
};

// ============================================================
// Main window procedure
// ============================================================

result_t main_win_proc(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      // Set main_win before calling app_update_status so the status bar
      // is populated on first paint.
      g_app->main_win = win;
      // Install toolbar buttons (New / Edit / Delete).
      send_message(win, kToolBarMessageAddButtons,
                   sizeof(kMainToolbar) / sizeof(kMainToolbar[0]),
                   (void *)kMainToolbar);
      // The list view is created as a child that fills the client area.
      g_app->list_win = create_window(
          "tasks",
          WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
          MAKERECT(0, 0, win->frame.w, win->frame.h),
          win, tasklist_proc, 0, NULL);
      tasklist_refresh(g_app->list_win);
      app_update_status(g_app);
      return true;

    case kWindowMessageResize:
      if (g_app->list_win)
        resize_window(g_app->list_win, win->frame.w, win->frame.h);
      return false;

    case kToolBarMessageButtonClick:
      handle_menu_command((uint16_t)wparam);
      return true;

    case kWindowMessageCommand: {
      // Forward menu commands and list notifications to menu handler.
      switch (HIWORD(wparam)) {
        case kMenuBarNotificationItemClick:
          handle_menu_command((uint16_t)LOWORD(wparam));
          return true;
        
        // CVN notifications: LOWORD(wparam) = item index, lparam = columnview_item_t*.
        case CVN_SELCHANGE: {
          int sel = (int)(int16_t)LOWORD(wparam);
          if (g_app) g_app->selected_idx = sel;
          return true;
        }
        
        case CVN_DBLCLK:
          handle_menu_command(ID_TASK_EDIT);
          return true;
        
        default:
          return false;
      }
    }

    case kWindowMessageClose:
      if (g_app && g_app->modified) {
        int r = message_box(win, "Discard unsaved changes and quit?",
                            "Quit", MB_YESNO);
        if (r != IDYES) return true;  // user cancelled — keep window open
      }
      ui_request_quit();
      return false;  // let framework proceed with default close/hide

    default:
      return false;
  }
}
