// Commit dialog — uses generated form from gitclient.orion.

#include "gitclient.h"

typedef struct {
  char msg_text[2048];
  bool amend;
  bool amend_requested;
  bool result;
} commit_dlg_state_t;

static const ctrl_binding_t commit_bindings[] = {
  DDX_TEXT (ID_COMMIT_DIALOG_MSG,   commit_dlg_state_t, msg_text),
  DDX_CHECK(ID_COMMIT_DIALOG_AMEND, commit_dlg_state_t, amend),
};

static result_t commit_dlg_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  commit_dlg_state_t *st = (commit_dlg_state_t *)win->userdata;

  switch (msg) {
    case evCreate:
      win->userdata = lparam; st = (commit_dlg_state_t *)lparam;
      if (st->amend_requested) {
        st->amend = true;
        gc_state_t *gc = g_gc;
        if (gc && gc->repo) {
          const char *args[] = { "git", "log", "-1", "--format=%B", NULL };
          git_run_sync(gc->repo, args, st->msg_text, sizeof(st->msg_text));
          char *nl = strrchr(st->msg_text, '\n');
          if (nl && *(nl + 1) == '\0') *nl = '\0';
        }
      }
      dialog_push(win, st, commit_bindings, ARRAY_LEN(commit_bindings));
      return true;

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (!src) return false;
        if (src->id == ID_COMMIT_DIALOG_CANCEL) { end_dialog(win, 0); return true; }
        if (src->id == ID_COMMIT_DIALOG_OK) {
          dialog_pull(win, st, commit_bindings, ARRAY_LEN(commit_bindings));
          if (!st->msg_text[0]) {
            message_box(win, ui_tr("Please enter a commit message."), ui_tr("Commit"), MB_OK);
            return true;
          }
          if (!gc_commit(st->msg_text, st->amend)) {
            message_box(win, ui_tr("Commit failed."), ui_tr("Commit"), MB_OK);
            return true;
          }
          st->result = true;
          end_dialog(win, 1);
          return true;
        }
      }
      return false;

    default:
      return false;
  }
}

bool gc_show_commit_dialog(window_t *parent, bool amend) {
  commit_dlg_state_t st = { .amend_requested = amend };
  show_dialog_from_form(&gc_commit_dialog_form, "Commit", parent,
                         commit_dlg_proc, &st);
  if (st.result)
    gc_refresh_all();
  return st.result;
}
