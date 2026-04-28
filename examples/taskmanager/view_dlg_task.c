// VIEW: Task create/edit dialog using form-based layout.

#include "taskmanager.h"

// ============================================================
// Form layout
// ============================================================

static const form_ctrl_def_t kTaskEditChildren[] = {
  { FORM_CTRL_LABEL,    -1,                    {8,   8,  60, 13}, 0,             "Title:",       "lbl_title"    },
  { FORM_CTRL_TEXTEDIT, ID_TASK_TITLE_CTRL,    {70,  6, 200, 16}, 0,             "",             "edit_title"   },

  { FORM_CTRL_LABEL,     -1,                    {8,  28,  60, 13}, 0,             "Description:", "lbl_desc"     },
  { FORM_CTRL_MULTIEDIT, ID_TASK_DESC_CTRL,     {70, 28, 200, 60}, 0,             "",             "edit_desc"    },

  { FORM_CTRL_LABEL,    -1,                    {8,  95,  60, 13}, 0,             "Priority:",    "lbl_prio"     },
  { FORM_CTRL_COMBOBOX, ID_TASK_PRIORITY_CTRL, {70, 95, 100, 18}, 0,             "",             "combo_prio"   },

  { FORM_CTRL_LABEL,    -1,                    {8, 120,  60, 13}, 0,             "Status:",      "lbl_status"   },
  { FORM_CTRL_COMBOBOX, ID_TASK_STATUS_CTRL,   {70, 120, 100, 18}, 0,            "",             "combo_status" },

  { FORM_CTRL_LABEL,    -1,                    {8, 145,  60, 13}, 0,             "Due (epoch):", "lbl_due"      },
  { FORM_CTRL_TEXTEDIT, ID_TASK_DUEDATE_CTRL,  {70, 143, 100, 16}, 0,            "",             "edit_due"     },

  { FORM_CTRL_BUTTON,   ID_OK,                 {80, 170,  60, 18}, BUTTON_DEFAULT, "OK",     "btn_ok"     },
  { FORM_CTRL_BUTTON,   ID_CANCEL,             {150, 170, 60, 18}, 0,              "Cancel", "btn_cancel" },
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
// DDX binding table
// ============================================================

static const ctrl_binding_t k_task_bindings[] = {
  { ID_TASK_TITLE_CTRL,    BIND_STRING,    offsetof(task_dlg_state_t, title),    sizeof_field(task_dlg_state_t, title) },
  { ID_TASK_DESC_CTRL,     BIND_MLSTRING,  offsetof(task_dlg_state_t, desc),     sizeof_field(task_dlg_state_t, desc)  },
  { ID_TASK_PRIORITY_CTRL, BIND_INT_COMBO, offsetof(task_dlg_state_t, priority), PRIORITY_NORMAL },
  { ID_TASK_STATUS_CTRL,   BIND_INT_COMBO, offsetof(task_dlg_state_t, status),   STATUS_TODO },
};

static const form_def_t kTaskEditForm = {
  .name          = "Task",
  .width         = 280,
  .height        = 208,
  .flags         = 0,
  .children      = kTaskEditChildren,
  .child_count   = (int)(sizeof(kTaskEditChildren)/sizeof(kTaskEditChildren[0])),
  .bindings      = k_task_bindings,
  .binding_count = ARRAY_LEN(k_task_bindings),
  .ok_id         = ID_OK,
  .cancel_id     = ID_CANCEL,
};

// ============================================================
// Helpers to initialise combo boxes
// ============================================================

static void populate_priority_combo(window_t *win) {
  window_t *cb = get_window_item(win, ID_TASK_PRIORITY_CTRL);
  if (!cb) return;
  send_message(cb, cbAddString, 0, (void *)"Low");
  send_message(cb, cbAddString, 0, (void *)"Normal");
  send_message(cb, cbAddString, 0, (void *)"High");
  send_message(cb, cbAddString, 0, (void *)"Urgent");
}

static void populate_status_combo(window_t *win) {
  window_t *cb = get_window_item(win, ID_TASK_STATUS_CTRL);
  if (!cb) return;
  send_message(cb, cbAddString, 0, (void *)"Todo");
  send_message(cb, cbAddString, 0, (void *)"In Progress");
  send_message(cb, cbAddString, 0, (void *)"Completed");
  send_message(cb, cbAddString, 0, (void *)"Cancelled");
}

// ============================================================
// Window procedure
// ============================================================

static result_t task_dlg_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  task_dlg_state_t *s = (task_dlg_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      win->userdata = lparam;
      s = (task_dlg_state_t *)lparam;

      // Populate combo items first (must precede dialog_push).
      populate_priority_combo(win);
      populate_status_combo(win);

      if (s->task) {
        // Copy existing task data into state so dialog_push can populate controls.
        strncpy(s->title, s->task->title, sizeof(s->title) - 1);
        s->title[sizeof(s->title) - 1] = '\0';
        strncpy(s->desc, s->task->description, sizeof(s->desc) - 1);
        s->desc[sizeof(s->desc) - 1] = '\0';
        s->priority = (int)s->task->priority;
        s->status   = (int)s->task->status;

        if (s->task->due_date) {
          char due_buf[32];
          snprintf(due_buf, sizeof(due_buf), "%u", s->task->due_date);
          set_window_item_text(win, ID_TASK_DUEDATE_CTRL, "%s", due_buf);
        }
      }
      dialog_push(win, s, k_task_bindings, ARRAY_LEN(k_task_bindings));
      return true;
    }

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;

        if (src->id == ID_OK) {
          // Validate title before accepting.
          window_t *et = get_window_item(win, ID_TASK_TITLE_CTRL);
          if (!et || et->title[0] == '\0') {
            message_box(win, "Title is required.", "Validation", MB_OK);
            return true;
          }

          dialog_pull(win, s, k_task_bindings, ARRAY_LEN(k_task_bindings));

          // Due date: optional uint32_t — not in binding table (needs custom parsing).
          window_t *edue = get_window_item(win, ID_TASK_DUEDATE_CTRL);
          s->due_date = 0;
          if (edue && edue->title[0] != '\0') {
            char *endp = NULL;
            unsigned long parsed = strtoul(edue->title, &endp, 10);
            if (*endp != '\0') {
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
  task_doc_t *doc = doc_from_window(parent);
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
    if (doc) doc->modified = true;
  } else {
    // Create mode: build a new task and add to app state.
    task_t *t = task_create(state.title, state.desc,
                            (task_priority_t)state.priority,
                            (task_status_t)state.status,
                            state.due_date);
    if (t && doc) app_add_task(doc, t);
  }
  return true;
}
