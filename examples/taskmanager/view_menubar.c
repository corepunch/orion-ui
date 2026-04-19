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
  task_doc_t *doc = g_app->active_doc;
  window_t *parent = doc ? doc->win : g_app->menubar_win;
  TM_DEBUG("command id=%u name=%s doc=%p selected_idx=%d task_count=%d modified=%d",
           (unsigned)id,
           tm_command_name(id),
           (void *)doc,
           doc ? doc->selected_idx : -1,
           doc ? doc->task_count : -1,
           doc ? (int)doc->modified : -1);

  switch (id) {
    // ---- File ----
    case ID_FILE_NEW: {
      create_document(NULL);
      TM_DEBUG("action new_document");
      break;
    }
    case ID_FILE_OPEN: {
      char path[512] = "";
      if (pick_open_path(parent, path, sizeof(path))) {
        TM_DEBUG("action open_dialog accepted path=%s", path);
        task_doc_t *existing = app_find_document_by_path(path);
        if (existing && existing->win) {
          g_app->active_doc = existing;
          show_window(existing->win, true);
          app_update_status(existing);
          TM_DEBUG("action open_existing_document path=%s", path);
          break;
        }

        task_doc_t *ndoc = create_document(path);
        if (!ndoc) {
          TM_DEBUG("error create_document_failed path=%s", path);
          message_box(parent, "Failed to create document window.", "Error", MB_OK);
          return;
        }
        if (!task_file_load(path, ndoc)) {
          TM_DEBUG("error task_file_load_failed path=%s", path);
          close_document(ndoc);
          message_box(parent, "Failed to open file.", "Error", MB_OK);
        } else {
          strncpy(ndoc->filename, path, sizeof(ndoc->filename) - 1);
          ndoc->filename[sizeof(ndoc->filename) - 1] = '\0';
          ndoc->modified = false;
          tasklist_refresh(ndoc->list_win);
          doc_update_title(ndoc);
          app_update_status(ndoc);
          TM_DEBUG("action open_loaded_document path=%s task_count=%d",
                   path, ndoc->task_count);
        }
      }
      break;
    }
    case ID_FILE_SAVE: {
      if (!doc) break;
      if (doc->filename[0] == '\0') {
        char path[512] = "";
        if (!pick_save_path(parent, path, sizeof(path))) return;
        strncpy(doc->filename, path, sizeof(doc->filename) - 1);
        doc->filename[sizeof(doc->filename) - 1] = '\0';
      }
      if (!task_file_save(doc->filename, doc))
        message_box(parent, "Failed to save file.", "Error", MB_OK);
      else {
        doc->modified = false;
        doc_update_title(doc);
        app_update_status(doc);
        TM_DEBUG("action save path=%s", doc->filename);
      }
      break;
    }
    case ID_FILE_SAVEAS: {
      if (!doc) break;
      char path[512] = "";
      if (pick_save_path(parent, path, sizeof(path))) {
        strncpy(doc->filename, path, sizeof(doc->filename) - 1);
        doc->filename[sizeof(doc->filename) - 1] = '\0';
        if (!task_file_save(doc->filename, doc))
          message_box(parent, "Failed to save file.", "Error", MB_OK);
        else {
          doc->modified = false;
          doc_update_title(doc);
          app_update_status(doc);
          TM_DEBUG("action save_as path=%s", doc->filename);
        }
      }
      break;
    }
    case ID_FILE_QUIT:
      TM_DEBUG("action quit_requested");
      if (!app_close_all_documents(parent)) return;
      ui_request_quit();
      break;

    // ---- Task ----
    case ID_TASK_NEW:
      if (show_task_dialog(parent, NULL)) {
        tasklist_refresh(doc->list_win);
        doc_update_title(doc);
        app_update_status(doc);
        TM_DEBUG("action task_new task_count=%d", doc ? doc->task_count : -1);
      }
      break;
    case ID_TASK_EDIT: {
      int idx;
      task_t *t;
      if (!doc) return;
      idx = doc->selected_idx;
      t = app_get_task(doc, idx);
      if (!t) {
        message_box(parent, "No task selected.", "Edit Task", MB_OK);
        return;
      }
      if (show_task_dialog(parent, t)) {
        tasklist_refresh(doc->list_win);
        doc_update_title(doc);
        app_update_status(doc);
        TM_DEBUG("action task_edit idx=%d", idx);
      }
      break;
    }
    case ID_TASK_DELETE: {
      int idx;
      if (!doc) return;
      idx = doc->selected_idx;
      if (idx < 0 || idx >= doc->task_count) {
        message_box(parent, "No task selected.", "Delete Task", MB_OK);
        return;
      }
      int r = message_box(parent, "Delete selected task?",
                          "Delete Task", MB_YESNO);
      if (r == IDYES) {
        app_delete_task(doc, idx);
        tasklist_refresh(doc->list_win);
        doc_update_title(doc);
        app_update_status(doc);
        TM_DEBUG("action task_delete idx=%d new_task_count=%d", idx, doc->task_count);
      }
      break;
    }

    // ---- View ----
    case ID_VIEW_REFRESH:
      if (doc) {
        tasklist_refresh(doc->list_win);
        app_update_status(doc);
        TM_DEBUG("action view_refresh");
      }
      break;

    // ---- Help ----
    case ID_HELP_ABOUT:
      show_about_dialog(parent);
      TM_DEBUG("action show_about");
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
    case evCommand:
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
