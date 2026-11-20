#include "gitclient.h"

typedef struct { char names[GC_MAX_BRANCHES][256]; int count; bool result; } switch_state_t;

static void switch_fill(window_t *win, switch_state_t *st) {
  window_t *list = get_window_item(win, ID_SWITCH_BRANCH_DIALOG_BRANCHES);
  window_t *query = get_window_item(win, ID_SWITCH_BRANCH_DIALOG_QUERY);
  char q[256] = {0}; if (query) send_message(query, edGetText, sizeof(q), q);
  if (!list) return; send_message(list, cbClear, 0, NULL); bool selected = false;
  for (int i = 0; i < st->count; i++) if (!q[0] || strstr(st->names[i], q)) {
    send_message(list, cbAddString, 0, st->names[i]);
    if (!selected) { set_window_item_text(win, ID_SWITCH_BRANCH_DIALOG_BRANCHES, "%s", st->names[i]); selected = true; }
  }
}

static result_t switch_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch_state_t *st = win->userdata;
  if (msg == evCreate) {
    win->userdata = lparam; st = lparam;
    git_branch_t raw[GC_MAX_BRANCHES]; int n = g_gc ? git_get_branches(g_gc->repo, raw, GC_MAX_BRANCHES) : 0;
    for (int i = 0; st && i < n; i++) if (!raw[i].is_remote)
      strncpy(st->names[st->count++], raw[i].name, 255);
    if (st) switch_fill(win, st); return true;
  }
  if (msg != evCommand || !st) return false;
  if (HIWORD(wparam) == edUpdate && LOWORD(wparam) == ID_SWITCH_BRANCH_DIALOG_QUERY) { switch_fill(win, st); return true; }
  if (HIWORD(wparam) != btnClicked) return false;
  uint16_t id = LOWORD(wparam);
  if (id == ID_SWITCH_BRANCH_DIALOG_CANCEL) { end_dialog(win, 0); return true; }
  if (id == ID_SWITCH_BRANCH_DIALOG_NEW_BRANCH) { gc_show_new_branch_dialog(win); end_dialog(win, 1); return true; }
  if (id == ID_SWITCH_BRANCH_DIALOG_OK) {
    window_t *list = get_window_item(win, ID_SWITCH_BRANCH_DIALOG_BRANCHES); char name[256] = {0};
    if (list) strncpy(name, list->title, sizeof(name) - 1);
    if (name[0] && gc_checkout_branch(name)) { st->result = true; end_dialog(win, 1); }
    else message_box(win, ui_tr("Could not switch branches."), ui_tr("Switch Branch"), MB_OK);
    return true;
  }
  return false;
}

bool gc_show_switch_branch_dialog(window_t *parent) {
  switch_state_t st = {0};
  show_dialog_from_form(&gitclient_switch_branch_dialog_form, "Switch Branch", parent, switch_proc, &st);
  if (st.result) gc_refresh_all(); return st.result;
}
