// VIEW: Menu bar and command dispatch for the Task Manager.

#include "taskmanager.h"
#include "../../gem_magic.h"

// ============================================================
// Menu definitions
// ============================================================

static const menu_item_t kFileItems[] = {
  {"New",        ID_FILE_NEW},
  {"Open...",    ID_FILE_OPEN},
  {NULL,         0},
  {"Save",       ID_FILE_SAVE},
  {"Save As...", ID_FILE_SAVEAS},
  {NULL,         0},
  {"Quit",       ID_FILE_QUIT},
};

static const menu_item_t kTaskItems[] = {
  {"New Task",    ID_TASK_NEW},
  {"Edit Task",   ID_TASK_EDIT},
  {"Delete Task", ID_TASK_DELETE},
};

static const menu_item_t kViewItems[] = {
  {"Refresh", ID_VIEW_REFRESH},
};

static const menu_item_t kHelpItems[] = {
  {"About...", ID_HELP_ABOUT},
};

menu_def_t kMenus[] = {
  {"File", kFileItems, (int)(sizeof(kFileItems)/sizeof(kFileItems[0]))},
  {"Task", kTaskItems, (int)(sizeof(kTaskItems)/sizeof(kTaskItems[0]))},
  {"View", kViewItems, (int)(sizeof(kViewItems)/sizeof(kViewItems[0]))},
  {"Help", kHelpItems, (int)(sizeof(kHelpItems)/sizeof(kHelpItems[0]))},
};
const int kNumMenus = (int)(sizeof(kMenus)/sizeof(kMenus[0]));

// ============================================================
// Accelerator table
// ============================================================

static const accel_t kAccelEntries[] = {
  { FCONTROL|FVIRTKEY, AX_KEY_N,   ID_TASK_NEW    },
  { FCONTROL|FVIRTKEY, AX_KEY_E,   ID_TASK_EDIT   },
  { FVIRTKEY,          AX_KEY_DEL, ID_TASK_DELETE  },
  { FCONTROL|FVIRTKEY, AX_KEY_S,   ID_FILE_SAVE   },
  { FCONTROL|FVIRTKEY, AX_KEY_O,   ID_FILE_OPEN   },
};

// ============================================================
// File path helpers (open/save dialogs, .tdb extension)
// ============================================================

static bool pick_open_path(window_t *parent, char *path, size_t path_sz) {
  openfilename_t ofn = {0};
  ofn.lStructSize  = sizeof(ofn);
  ofn.hwndOwner    = parent;
  ofn.lpstrFile    = path;
  ofn.nMaxFile     = (uint32_t)path_sz;
  ofn.lpstrFilter  = "Task Database\0*.tdb\0All Files\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags        = OFN_FILEMUSTEXIST;
  return get_open_filename(&ofn);
}

static bool pick_save_path(window_t *parent, char *path, size_t path_sz) {
  openfilename_t ofn = {0};
  ofn.lStructSize  = sizeof(ofn);
  ofn.hwndOwner    = parent;
  ofn.lpstrFile    = path;
  ofn.nMaxFile     = (uint32_t)path_sz;
  ofn.lpstrFilter  = "Task Database\0*.tdb\0All Files\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags        = OFN_OVERWRITEPROMPT;
  return get_save_filename(&ofn);
}

// ============================================================
// handle_menu_command — dispatch File/Task/View/Help commands
// ============================================================

void handle_menu_command(uint16_t id) {
  if (!g_app) return;
  window_t *parent = g_app->main_win;

  switch (id) {
    // ---- File ----
    case ID_FILE_NEW: {
      if (g_app->modified) {
        int r = message_box(parent, "Discard unsaved changes?",
                            "New File", MB_YESNO);
        if (r != IDYES) return;
      }
      // Clear all tasks
      for (int i = 0; i < g_app->task_count; i++)
        task_free(g_app->tasks[i]);
      g_app->task_count  = 0;
      g_app->next_id     = 1;
      g_app->selected_idx = -1;
      g_app->modified    = false;
      g_app->filename[0] = '\0';
      tasklist_refresh(g_app->list_win);
      app_update_status(g_app);
      break;
    }
    case ID_FILE_OPEN: {
      char path[512] = "";
      if (pick_open_path(parent, path, sizeof(path))) {
        if (!task_file_load(path, g_app)) {
          message_box(parent, "Failed to open file.", "Error", MB_OK);
        } else {
          strncpy(g_app->filename, path, sizeof(g_app->filename) - 1);
          g_app->filename[sizeof(g_app->filename) - 1] = '\0';
          g_app->modified = false;
          tasklist_refresh(g_app->list_win);
          app_update_status(g_app);
        }
      }
      break;
    }
    case ID_FILE_SAVE: {
      if (g_app->filename[0] == '\0') {
        char path[512] = "";
        if (!pick_save_path(parent, path, sizeof(path))) return;
        strncpy(g_app->filename, path, sizeof(g_app->filename) - 1);
        g_app->filename[sizeof(g_app->filename) - 1] = '\0';
      }
      if (!task_file_save(g_app->filename, g_app))
        message_box(parent, "Failed to save file.", "Error", MB_OK);
      else
        g_app->modified = false;
      break;
    }
    case ID_FILE_SAVEAS: {
      char path[512] = "";
      if (pick_save_path(parent, path, sizeof(path))) {
        strncpy(g_app->filename, path, sizeof(g_app->filename) - 1);
        g_app->filename[sizeof(g_app->filename) - 1] = '\0';
        if (!task_file_save(g_app->filename, g_app))
          message_box(parent, "Failed to save file.", "Error", MB_OK);
        else
          g_app->modified = false;
      }
      break;
    }
    case ID_FILE_QUIT:
      if (g_app->modified) {
        int r = message_box(parent, "Discard unsaved changes and quit?",
                            "Quit", MB_YESNO);
        if (r != IDYES) return;
      }
      ui_request_quit();
      break;

    // ---- Task ----
    case ID_TASK_NEW:
      if (show_task_dialog(parent, NULL)) {
        tasklist_refresh(g_app->list_win);
        app_update_status(g_app);
      }
      break;
    case ID_TASK_EDIT: {
      int idx = g_app->selected_idx;
      task_t *t = app_get_task(g_app, idx);
      if (!t) {
        message_box(parent, "No task selected.", "Edit Task", MB_OK);
        return;
      }
      if (show_task_dialog(parent, t)) {
        tasklist_refresh(g_app->list_win);
        app_update_status(g_app);
      }
      break;
    }
    case ID_TASK_DELETE: {
      int idx = g_app->selected_idx;
      if (idx < 0 || idx >= g_app->task_count) {
        message_box(parent, "No task selected.", "Delete Task", MB_OK);
        return;
      }
      int r = message_box(parent, "Delete selected task?",
                          "Delete Task", MB_YESNO);
      if (r == IDYES) {
        app_delete_task(g_app, idx);
        tasklist_refresh(g_app->list_win);
        app_update_status(g_app);
      }
      break;
    }

    // ---- View ----
    case ID_VIEW_REFRESH:
      tasklist_refresh(g_app->list_win);
      app_update_status(g_app);
      break;

    // ---- Help ----
    case ID_HELP_ABOUT:
      show_about_dialog(parent);
      break;

    default:
      break;
  }
}

// ============================================================
// Menu bar window procedure
// ============================================================

result_t app_menubar_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCommand:
      if (HIWORD(wparam) == kMenuBarNotificationItemClick ||
          HIWORD(wparam) == kAcceleratorNotification) {
        handle_menu_command((uint16_t)LOWORD(wparam));
        return true;
      }
      return false;
    default:
      return win_menubar(win, msg, wparam, lparam);
  }
}

// ============================================================
// create_menubar — called from main.c to build the menu bar
// ============================================================

void create_menubar(void) {
  g_app->menubar_win = set_app_menu(app_menubar_proc, kMenus, kNumMenus,
                                    handle_menu_command, g_app->hinstance);

  g_app->accel = load_accelerators(kAccelEntries,
      (int)(sizeof(kAccelEntries)/sizeof(kAccelEntries[0])));
  if (g_app->menubar_win)
    send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators,
                 0, g_app->accel);
}
