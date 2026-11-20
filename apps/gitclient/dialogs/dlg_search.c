// Search dialog — filter commits by text.

#include "gitclient.h"

typedef struct {
  char query[256];
} search_state_t;

static const ctrl_binding_t search_bindings[] = {
  DDX_TEXT(ID_SEARCH_DIALOG_QUERY, search_state_t, query),
};

static result_t search_dlg_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  search_state_t *st = (search_state_t *)win->userdata;

  if (msg == evCreate) {
    win->userdata = lparam; st = (search_state_t *)lparam;
    dialog_push(win, st, search_bindings, ARRAY_LEN(search_bindings));
    return true;
  }
  if (msg != evCommand || HIWORD(wparam) != btnClicked) return false;
  uint16_t id = LOWORD(wparam);
  if (id == ID_SEARCH_DIALOG_CANCEL) { end_dialog(win, 0); return true; }
  if (id == ID_SEARCH_DIALOG_OK) {
    dialog_pull(win, st, search_bindings, ARRAY_LEN(search_bindings));
    gc_state_t *gc = g_gc;
    if (!gc || !gc->repo) { end_dialog(win, 0); return true; }
    if (st->query[0]) {
      char buf[GC_DIFF_BUF_SIZE] = {0};
      const char *args[] = { "git", "log", "--all", "--oneline", "--grep", st->query, NULL };
      if (git_run_sync(gc->repo, args, buf, sizeof(buf))) {
        int count = 0;
        for (char *p = buf; *p; p++) if (*p == '\n') count++;
        char notice[128];
        if (count == 1) snprintf(notice, sizeof(notice), "%s", ui_tr("Found 1 matching commit."));
        else snprintf(notice, sizeof(notice), ui_tr("Found %d matching commits."), count);
        message_box(win, notice, ui_tr("Search Results"), MB_OK);
      }
    }
    end_dialog(win, 1);
    return true;
  }
  return false;
}

void gc_show_search_dialog(window_t *parent) {
  search_state_t st = {0};
  show_dialog_from_form(&gc_search_dialog_form, "Search", parent,
                         search_dlg_proc, &st);
}
