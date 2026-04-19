// VIEW: Task list adapter. Rendering and interaction are handled by win_reportview.

#include "taskmanager.h"

static int tasklist_title_width(window_t *win) {
  int count = (int)send_message(win, RVM_GETITEMCOUNT, 0, NULL);
  int total_h = COLUMNVIEW_ENTRY_HEIGHT + count * COLUMNVIEW_ENTRY_HEIGHT;
  int need_vscroll = total_h > win->frame.h;
  int sb = need_vscroll ? SCROLLBAR_WIDTH : 0;
  int avail = win->frame.w - sb - TASKLIST_PRIORITY_W - TASKLIST_STATUS_W;
  return (avail < 20) ? 20 : avail;
}

result_t tasklist_proc(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam) {
  result_t r = win_reportview(win, msg, wparam, lparam);

  if (msg == kWindowMessageResize) {
    send_message(win, RVM_SETREPORTCOLUMNWIDTH, 0,
                 (void *)(uintptr_t)tasklist_title_width(win));
  }

  return r;
}

void tasklist_refresh(window_t *list_win) {
  task_doc_t *doc = doc_from_window(list_win);
  if (!list_win || !doc) return;

  send_message(list_win, RVM_SETREDRAW, 0, NULL);

  send_message(list_win, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
  send_message(list_win, RVM_CLEARCOLUMNS, 0, NULL);

  reportview_column_t col_title = { "Title", 0 };
  reportview_column_t col_prio = { "Priority", TASKLIST_PRIORITY_W };
  reportview_column_t col_status = { "Status", TASKLIST_STATUS_W };

  send_message(list_win, RVM_ADDCOLUMN, 0, &col_title);
  send_message(list_win, RVM_ADDCOLUMN, 0, &col_prio);
  send_message(list_win, RVM_ADDCOLUMN, 0, &col_status);

  send_message(list_win, RVM_CLEAR, 0, NULL);

  for (int i = 0; i < doc->task_count; i++) {
    task_t *t = doc->tasks[i];
    if (!t) continue;

    const char *prio = priority_to_string(t->priority);
    const char *status = status_to_string(t->status);

    reportview_item_t item = {
      .text = t->title,
      .icon = icon8_editor_helmet,
      .color = get_sys_color(kColorTextNormal),
      .userdata = (uint32_t)i,
      .subitems = { prio, status },
      .subitem_count = 2,
    };

    send_message(list_win, RVM_ADDITEM, 0, &item);
  }

  if (doc->selected_idx >= 0 && doc->selected_idx < doc->task_count) {
    send_message(list_win, RVM_SETSELECTION, (uint32_t)doc->selected_idx, NULL);
  }

  // Apply width after rows are known so scrollbar-dependent width is stable.
  send_message(list_win, RVM_SETREPORTCOLUMNWIDTH, 0,
               (void *)(uintptr_t)tasklist_title_width(list_win));

  send_message(list_win, RVM_SETREDRAW, 1, NULL);
}
