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

void doc_update_title(task_doc_t *doc) {
  char title[128];
  const char *name;
  const char *slash;

  if (!doc || !doc->win) return;

  name = doc->filename[0] ? doc->filename : "Untitled";
  slash = strrchr(name, '/');
  if (slash) name = slash + 1;

  snprintf(title, sizeof(title), "%s%s", name, doc->modified ? " *" : "");
  strncpy(doc->win->title, title, sizeof(doc->win->title) - 1);
  doc->win->title[sizeof(doc->win->title) - 1] = '\0';
  invalidate_window(doc->win);
}

bool doc_confirm_close(task_doc_t *doc, window_t *parent_win) {
  if (!doc) return true;
  if (doc->modified) {
    int r = message_box(parent_win,
                        "Discard unsaved changes?",
                        "Close Document",
                        MB_YESNO);
    if (r != IDYES) return false;
  }
  close_document(doc);
  return true;
}

task_doc_t *create_document(const char *filename) {
  task_doc_t *doc;
  int sw;
  int sh;
  int wx;
  int wy;
  window_t *win;

  if (!g_app) return NULL;

  doc = doc_state_init();
  if (!doc) return NULL;

  if (filename) {
    strncpy(doc->filename, filename, sizeof(doc->filename) - 1);
    doc->filename[sizeof(doc->filename) - 1] = '\0';
  }

  sw = MIN(480, ui_get_system_metrics(kSystemMetricScreenWidth));
  sh = MIN(320, ui_get_system_metrics(kSystemMetricScreenHeight));
  wx = g_app->next_x;
  wy = g_app->next_y;
  g_app->next_x += DOC_CASCADE;
  g_app->next_y += DOC_CASCADE;
  if (g_app->next_x + (sw - MAIN_WIN_X - 4) > SCREEN_W) {
    g_app->next_x = DOC_START_X;
    g_app->next_y = DOC_START_Y;
  }

  win = create_window(filename ? filename : "Untitled",
                      WINDOW_STATUSBAR | WINDOW_TOOLBAR,
                      MAKERECT(wx, wy,
                               sw - MAIN_WIN_X - 4,
                               sh - MAIN_WIN_Y - 4),
                      NULL, main_win_proc, g_app->hinstance, doc);
  if (!win) {
    doc_state_shutdown(doc);
    return NULL;
  }

  show_window(win, true);
  doc->next = g_app->docs;
  g_app->docs = doc;
  g_app->active_doc = doc;
  doc_update_title(doc);
  app_update_status(doc);
  return doc;
}

void close_document(task_doc_t *doc) {
  if (!doc || !g_app) return;

  if (g_app->active_doc == doc)
    g_app->active_doc = NULL;

  if (g_app->docs == doc) {
    g_app->docs = doc->next;
  } else {
    for (task_doc_t *it = g_app->docs; it; it = it->next) {
      if (it->next == doc) {
        it->next = doc->next;
        break;
      }
    }
  }

  if (doc->win && is_window(doc->win))
    destroy_window(doc->win);

  doc_state_shutdown(doc);

  if (!g_app->active_doc)
    g_app->active_doc = g_app->docs;
}

result_t main_win_proc(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam) {
  task_doc_t *doc = (task_doc_t *)win->userdata;

  switch (msg) {
    case kWindowMessageCreate:
      doc = (task_doc_t *)lparam;
      win->userdata = doc;
      if (!doc) return false;
      doc->win = win;
      g_app->active_doc = doc;
      // Install toolbar buttons (New / Edit / Delete).
      send_message(win, kToolBarMessageAddButtons,
                   sizeof(kMainToolbar) / sizeof(kMainToolbar[0]),
                   (void *)kMainToolbar);
      // The list view is created as a child that fills the client area.
      doc->list_win = create_window(
          "tasks",
          WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
          MAKERECT(0, 0, get_client_rect(win).w, get_client_rect(win).h),
          win, tasklist_proc, 0, NULL);
      tasklist_refresh(doc->list_win);
      app_update_status(doc);
      return true;

    case kWindowMessageResize:
      if (doc && doc->list_win) {
        rect_t cr = get_client_rect(win);
        resize_window(doc->list_win, cr.w, cr.h);
      }
      return false;

    case kWindowMessageSetFocus:
      if (g_app && doc) g_app->active_doc = doc;
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
          task_doc_t *cmd_doc = doc_from_window((window_t *)lparam ? (window_t *)lparam : win);
          if (g_app && cmd_doc) {
            g_app->active_doc = cmd_doc;
            cmd_doc->selected_idx = sel;
          }
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
      if (!doc) return false;
      doc_confirm_close(doc, win);
      return true;

    default:
      return false;
  }
}
