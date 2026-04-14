// VIEW: Task create/edit dialog using form-based layout.

#include "taskmanager.h"

// ============================================================
// Form layout
// ============================================================

static const form_ctrl_def_t kTaskEditChildren[] = {
  { FORM_CTRL_LABEL,    -1,                    {8,   8,  60, 13}, 0,             "Title:",       "lbl_title"    },
  { FORM_CTRL_TEXTEDIT, ID_TASK_TITLE_CTRL,    {70,  8, 200, 13}, 0,             "",             "edit_title"   },

  { FORM_CTRL_LABEL,    -1,                    {8,  28,  60, 13}, 0,             "Description:", "lbl_desc"     },
  { FORM_CTRL_TEXTEDIT, ID_TASK_DESC_CTRL,     {70, 28, 200, 60}, 0,             "",             "edit_desc"    },

  { FORM_CTRL_LABEL,    -1,                    {8,  95,  60, 13}, 0,             "Priority:",    "lbl_prio"     },
  { FORM_CTRL_COMBOBOX, ID_TASK_PRIORITY_CTRL, {70, 95, 100, 20}, 0,             "",             "combo_prio"   },

  { FORM_CTRL_LABEL,    -1,                    {8, 120,  60, 13}, 0,             "Status:",      "lbl_status"   },
  { FORM_CTRL_COMBOBOX, ID_TASK_STATUS_CTRL,   {70, 120, 100, 20}, 0,            "",             "combo_status" },

  { FORM_CTRL_LABEL,    -1,                    {8, 145,  60, 13}, 0,             "Due (epoch):", "lbl_due"      },
  { FORM_CTRL_TEXTEDIT, ID_TASK_DUEDATE_CTRL,  {70, 145, 100, 13}, 0,            "",             "edit_due"     },

  { FORM_CTRL_BUTTON,   ID_OK,                 {80, 170,  60, 18}, BUTTON_DEFAULT, "OK",     "btn_ok"     },
  { FORM_CTRL_BUTTON,   ID_CANCEL,             {150, 170, 60, 18}, 0,              "Cancel", "btn_cancel" },
};

static const form_def_t kTaskEditForm = {
  .name        = "Task",
  .w           = 280,
  .h           = 200,
  .flags       = 0,
  .children    = kTaskEditChildren,
  .child_count = (int)(sizeof(kTaskEditChildren)/sizeof(kTaskEditChildren[0])),
};

// ============================================================
// Dialog state
// ============================================================

typedef struct {
  task_t *task;       // NULL = create new, non-NULL = edit existing
  bool    accepted;
  char    title[128];
  char    desc[512];
  int     priority;
  int     status;
  uint32_t due_date;
} task_dlg_state_t;

// ============================================================
// Helpers to initialise combo boxes
// ============================================================

static void populate_priority_combo(window_t *win) {
  window_t *cb = get_window_item(win, ID_TASK_PRIORITY_CTRL);
  if (!cb) return;
  send_message(cb, kComboBoxMessageAddString, 0, (void *)"Low");
  send_message(cb, kComboBoxMessageAddString, 0, (void *)"Normal");
  send_message(cb, kComboBoxMessageAddString, 0, (void *)"High");
  send_message(cb, kComboBoxMessageAddString, 0, (void *)"Urgent");
}

static void populate_status_combo(window_t *win) {
  window_t *cb = get_window_item(win, ID_TASK_STATUS_CTRL);
  if (!cb) return;
  send_message(cb, kComboBoxMessageAddString, 0, (void *)"Todo");
  send_message(cb, kComboBoxMessageAddString, 0, (void *)"In Progress");
  send_message(cb, kComboBoxMessageAddString, 0, (void *)"Completed");
  send_message(cb, kComboBoxMessageAddString, 0, (void *)"Cancelled");
}

// ============================================================
// Window procedure
// ============================================================

static result_t task_dlg_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  task_dlg_state_t *s = (task_dlg_state_t *)win->userdata;

  switch (msg) {
    case kWindowMessageCreate: {
      win->userdata = lparam;
      s = (task_dlg_state_t *)lparam;

      // Populate combo boxes.
      populate_priority_combo(win);
      populate_status_combo(win);

      if (s->task) {
        // Editing an existing task: pre-populate fields.
        set_window_item_text(win, ID_TASK_TITLE_CTRL, "%s", s->task->title);
        set_window_item_text(win, ID_TASK_DESC_CTRL,  "%s", s->task->description);

        window_t *cp = get_window_item(win, ID_TASK_PRIORITY_CTRL);
        if (cp) send_message(cp, kComboBoxMessageSetCurrentSelection,
                             (uint32_t)s->task->priority, NULL);

        window_t *cs = get_window_item(win, ID_TASK_STATUS_CTRL);
        if (cs) send_message(cs, kComboBoxMessageSetCurrentSelection,
                             (uint32_t)s->task->status, NULL);

        if (s->task->due_date) {
          char due_buf[32];
          snprintf(due_buf, sizeof(due_buf), "%u", s->task->due_date);
          set_window_item_text(win, ID_TASK_DUEDATE_CTRL, "%s", due_buf);
        }
      } else {
        // New task: default to Normal priority, Todo status.
        window_t *cp = get_window_item(win, ID_TASK_PRIORITY_CTRL);
        if (cp) send_message(cp, kComboBoxMessageSetCurrentSelection,
                             PRIORITY_NORMAL, NULL);
        window_t *cs = get_window_item(win, ID_TASK_STATUS_CTRL);
        if (cs) send_message(cs, kComboBoxMessageSetCurrentSelection,
                             STATUS_TODO, NULL);
      }
      return true;
    }

    case kWindowMessageCommand:
      if (HIWORD(wparam) == kButtonNotificationClicked) {
        window_t *src = (window_t *)lparam;

        if (src->id == ID_OK) {
          // Read and validate fields.
          window_t *et = get_window_item(win, ID_TASK_TITLE_CTRL);
          if (!et || et->title[0] == '\0') {
            message_box(win, "Title is required.", "Validation", MB_OK);
            return true;
          }
          strncpy(s->title, et->title, sizeof(s->title) - 1);
          s->title[sizeof(s->title) - 1] = '\0';

          window_t *ed = get_window_item(win, ID_TASK_DESC_CTRL);
          if (ed) {
            strncpy(s->desc, ed->title, sizeof(s->desc) - 1);
            s->desc[sizeof(s->desc) - 1] = '\0';
          } else {
            s->desc[0] = '\0';
          }

          window_t *cp = get_window_item(win, ID_TASK_PRIORITY_CTRL);
          s->priority = cp ? (int)send_message(cp, kComboBoxMessageGetCurrentSelection, 0, NULL) : PRIORITY_NORMAL;
          if (s->priority < 0) s->priority = PRIORITY_NORMAL;

          window_t *cs = get_window_item(win, ID_TASK_STATUS_CTRL);
          s->status = cs ? (int)send_message(cs, kComboBoxMessageGetCurrentSelection, 0, NULL) : STATUS_TODO;
          if (s->status < 0) s->status = STATUS_TODO;

          window_t *edue = get_window_item(win, ID_TASK_DUEDATE_CTRL);
          s->due_date = 0;
          if (edue && edue->title[0] != '\0') {
            char *endp = NULL;
            unsigned long parsed = strtoul(edue->title, &endp, 10);
            if (endp == edue->title || *endp != '\0') {
              message_box(win, "Due date must be a Unix timestamp (e.g. 1735689600) or empty for none.",
                          "Validation", MB_OK);
              return true;
            }
            s->due_date = (uint32_t)parsed;
          }

          s->accepted = true;
          end_dialog(win, 1);
          return true;
        }
        if (src->id == ID_CANCEL) {
          end_dialog(win, 0);
          return true;
        }
      }
      return false;

    default:
      return false;
  }
}

// ============================================================
// Public entry point
// ============================================================

bool show_task_dialog(window_t *parent, task_t *task) {
  task_dlg_state_t state = {
    .task     = task,
    .accepted = false,
    .priority = PRIORITY_NORMAL,
    .status   = STATUS_TODO,
    .due_date = 0,
  };
  state.title[0] = '\0';
  state.desc[0]  = '\0';

  const char *dlg_title = task ? "Edit Task" : "New Task";
  show_dialog_from_form(&kTaskEditForm, dlg_title, parent,
                        task_dlg_proc, &state);

  if (!state.accepted) return false;

  if (task) {
    // Edit mode: update existing task.
    task_update(task, state.title, state.desc,
                (task_priority_t)state.priority,
                (task_status_t)state.status,
                state.due_date);
    if (g_app) g_app->modified = true;
  } else {
    // Create mode: build a new task and add to app state.
    task_t *t = task_create(state.title, state.desc,
                            (task_priority_t)state.priority,
                            (task_status_t)state.status,
                            state.due_date);
    if (t && g_app) app_add_task(g_app, t);
  }
  return true;
}
