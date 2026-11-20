// Branch rename dialog — renames the current branch.

#include "gitclient.h"

typedef struct {
  char cur_name[256];
  char new_name[256];
  bool result;
} branch_rename_state_t;

static const ctrl_binding_t rename_bindings[] = {
  DDX_TEXT(ID_BRANCH_RENAME_DIALOG_NAME, branch_rename_state_t, new_name),
};

static result_t rename_branch_proc(window_t *win, uint32_t msg,
                                    uint32_t wparam, void *lparam) {
  branch_rename_state_t *st = (branch_rename_state_t *)win->userdata;

  if (msg == evCreate) {
    win->userdata = lparam; st = (branch_rename_state_t *)lparam;
    strncpy(st->new_name, st->cur_name, sizeof(st->new_name) - 1);
    dialog_push(win, st, rename_bindings, ARRAY_LEN(rename_bindings));
    return true;
  }
  if (msg != evCommand || HIWORD(wparam) != btnClicked) return false;
  uint16_t id = LOWORD(wparam);
  if (id == ID_BRANCH_RENAME_DIALOG_CANCEL) { end_dialog(win, 0); return true; }
  if (id == ID_BRANCH_RENAME_DIALOG_OK) {
    dialog_pull(win, st, rename_bindings, ARRAY_LEN(rename_bindings));
    if (!st->new_name[0]) {
      message_box(win, ui_tr("Please enter a branch name."), ui_tr("Rename Branch"), MB_OK);
      return true;
    }
    if (!gc_rename_branch(st->cur_name, st->new_name)) {
      message_box(win, ui_tr("Branch rename failed."), ui_tr("Rename Branch"), MB_OK);
      return true;
    }
    st->result = true;
    end_dialog(win, 1);
    return true;
  }
  return false;
}

bool gc_show_rename_branch_dialog(window_t *parent, const char *cur_name) {
  branch_rename_state_t st = {0};
  strncpy(st.cur_name, cur_name ? cur_name : "HEAD", sizeof(st.cur_name) - 1);
  show_dialog_from_form(&gc_branch_rename_dialog_form, "Rename Branch", parent,
                         rename_branch_proc, &st);
  if (st.result)
    gc_refresh_all();
  return st.result;
}
