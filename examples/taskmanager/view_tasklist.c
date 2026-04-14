// VIEW: Task list using win_columnview.

#include "taskmanager.h"

// ============================================================
// tasklist_proc — window procedure (wraps win_columnview)
// ============================================================

result_t tasklist_proc(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate: {
      result_t r = win_columnview(win, msg, wparam, lparam);
      if (r) send_message(win, CVM_SETCOLUMNWIDTH, 240, NULL);
      return r;
    }
    default:
      return win_columnview(win, msg, wparam, lparam);
  }
}

// ============================================================
// tasklist_refresh — repopulate the list from app state
// ============================================================

void tasklist_refresh(window_t *list_win) {
  if (!list_win || !g_app) return;

  // Clear existing items.
  send_message(list_win, CVM_CLEAR, 0, NULL);

  for (int i = 0; i < g_app->task_count; i++) {
    task_t *t = g_app->tasks[i];
    if (!t) continue;

    // Build display string: "Title  [Priority] Status"
    char display[256];
    snprintf(display, sizeof(display), "#%d  %s  [%s]  %s",
             t->id,
             t->title,
             priority_to_string(t->priority),
             status_to_string(t->status));

    columnview_item_t item = {
      .text     = display,
      .icon     = 0,
      .color    = get_sys_color(kColorTextNormal),
      .userdata = (uint32_t)i,
    };
    send_message(list_win, CVM_ADDITEM, 0, &item);
  }

  // Restore selection if still valid.
  if (g_app->selected_idx >= 0 && g_app->selected_idx < g_app->task_count)
    send_message(list_win, CVM_SETSELECTION,
                 (uint32_t)g_app->selected_idx, NULL);
}
