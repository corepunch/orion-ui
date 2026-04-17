// VIEW: Main window procedure for the Task Manager.

#include "taskmanager.h"

// ============================================================
// Toolbar button definitions
// ============================================================

static const toolbar_button_t kMainToolbar[] = {
  { sysicon_add,    ID_TASK_NEW,    0 },
  { sysicon_pencil, ID_TASK_EDIT,   0 },
  TOOLBAR_SPACING_TOKEN,
  { sysicon_delete, ID_TASK_DELETE, 0 },
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
          MAKERECT(0, 0, get_client_rect(win).w, get_client_rect(win).h),
          win, tasklist_proc, 0, NULL);
      tasklist_refresh(g_app->list_win);
      app_update_status(g_app);
      return true;

    case kWindowMessageResize:
      if (g_app->list_win) {
        rect_t cr = get_client_rect(win);
        resize_window(g_app->list_win, cr.w, cr.h);
      }
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
        
        // ReportView notifications: LOWORD(wparam) = row index, lparam = reportview_item_t*.
        case RVN_SELCHANGE: {
          reportview_item_t *item = (reportview_item_t *)lparam;
          int sel = item ? (int)item->userdata : (int)(int16_t)LOWORD(wparam);
          if (g_app) g_app->selected_idx = sel;
          return true;
        }
        
        case RVN_DBLCLK:
          handle_menu_command(ID_TASK_EDIT);
          return true;

        case RVN_DELETE:
          handle_menu_command(ID_TASK_DELETE);
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
