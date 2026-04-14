// VIEW: Menu bar and command dispatch for the Task Manager.

#include "taskmanager.h"

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
  {"New Task\tCtrl+N",    ID_TASK_NEW},
  {"Edit Task\tCtrl+E",   ID_TASK_EDIT},
  {"Delete Task\tDel",    ID_TASK_DELETE},
};

static const menu_item_t kViewItems[] = {
  {"Refresh\tF5", ID_VIEW_REFRESH},
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
// File path dialog
// ============================================================

typedef struct {
  char *path;
  int   sz;
  bool  ok;
  const char *prompt_label;
} filepath_dlg_t;

static const form_ctrl_def_t kFileDlgChildren[] = {
  { FORM_CTRL_TEXTEDIT, 1, {4, 22, 230, 13}, 0,             "", "edit_path"  },
  { FORM_CTRL_BUTTON,   2, {50, 44, 50, 14}, BUTTON_DEFAULT, "OK",     "btn_ok"     },
  { FORM_CTRL_BUTTON,   3, {108, 44, 50, 14}, 0,             "Cancel", "btn_cancel" },
};
static const form_def_t kFileDlgForm = {
  .name        = "File Path",
  .w           = 240,
  .h           = 68,
  .flags       = 0,
  .children    = kFileDlgChildren,
  .child_count = 3,
};

static result_t filepath_dlg_proc(window_t *win, uint32_t msg,
                                   uint32_t wparam, void *lparam) {
  filepath_dlg_t *s = (filepath_dlg_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate:
      win->userdata = lparam;
      return true;
    case kWindowMessagePaint:
      if (s && s->prompt_label)
        draw_text_small(s->prompt_label, 4, 8, get_sys_color(kColorTextNormal));
      return false;
    case kWindowMessageCommand:
      if (HIWORD(wparam) == kButtonNotificationClicked) {
        window_t *src = (window_t *)lparam;
        if (src->id == 2) {
          window_t *edit = get_window_item(win, 1);
          if (edit && s) {
            strncpy(s->path, edit->title, (size_t)s->sz - 1);
            s->path[s->sz - 1] = '\0';
            s->ok = true;
          }
          end_dialog(win, 1);
          return true;
        }
        if (src->id == 3) { end_dialog(win, 0); return true; }
      }
      return false;
    default: return false;
  }
}

// Show a simple text-entry dialog for a file path.
// Returns true and fills path if the user provides a non-empty string.
static bool prompt_filepath(window_t *parent, const char *prompt,
                             char *path, int path_sz) {
  filepath_dlg_t state = { path, path_sz, false, prompt };
  show_dialog_from_form(&kFileDlgForm, prompt, parent,
                        filepath_dlg_proc, &state);
  return state.ok && state.path[0] != '\0';
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
      if (prompt_filepath(parent, "Open file:", path, sizeof(path))) {
        if (!task_file_load(path, g_app)) {
          message_box(parent, "Failed to open file.", "Error", MB_OK);
        } else {
          strncpy(g_app->filename, path, sizeof(g_app->filename) - 1);
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
        if (!prompt_filepath(parent, "Save as:", path, sizeof(path))) return;
        strncpy(g_app->filename, path, sizeof(g_app->filename) - 1);
      }
      if (!task_file_save(g_app->filename, g_app))
        message_box(parent, "Failed to save file.", "Error", MB_OK);
      else
        g_app->modified = false;
      break;
    }
    case ID_FILE_SAVEAS: {
      char path[512] = "";
      if (prompt_filepath(parent, "Save as:", path, sizeof(path))) {
        strncpy(g_app->filename, path, sizeof(g_app->filename) - 1);
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
      if (HIWORD(wparam) == kMenuBarNotificationItemClick) {
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
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  window_t *mb = create_window(
      "menubar",
      WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(0, 0, sw, MENUBAR_HEIGHT),
      NULL, app_menubar_proc, NULL);
  send_message(mb, kMenuBarMessageSetMenus,
               (uint32_t)kNumMenus, (void *)kMenus);
  show_window(mb, true);
  g_app->menubar_win = mb;

  g_app->accel = load_accelerators(kAccelEntries,
      (int)(sizeof(kAccelEntries)/sizeof(kAccelEntries[0])));
  send_message(mb, kMenuBarMessageSetAccelerators, 0, g_app->accel);
}
