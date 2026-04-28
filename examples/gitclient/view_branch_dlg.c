// New-branch dialog (form_def_t).
//
//  ┌─ New Branch ──────────────────────────────┐
//  │ Name: [_____________________________]     │
//  │ From: [master__________________▾]         │
//  │ [ ] Checkout after creation               │
//  │                        [Cancel] [Create]  │
//  └───────────────────────────────────────────┘

#include "gitclient.h"

#define CTL_NAME     1
#define CTL_FROM     2
#define CTL_CHECKOUT 3
#define CTL_CREATE   4
#define CTL_CANCEL   5

static const form_ctrl_def_t kNewBranchCtrls[] = {
  { FORM_CTRL_TEXTEDIT,  CTL_NAME,     {50, 8,  170, 13}, 0, "",               "name"     },
  { FORM_CTRL_COMBOBOX,  CTL_FROM,     {50, 26, 170, 13}, 0, "",               "from"     },
  { FORM_CTRL_CHECKBOX,  CTL_CHECKOUT, {8,  46, 160, 13}, 0, "Checkout after creation","co"},
  { FORM_CTRL_BUTTON,    CTL_CREATE,   {144,64,  60, 14}, BUTTON_DEFAULT, "Create",  "ok"  },
  { FORM_CTRL_BUTTON,    CTL_CANCEL,   {208,64,  60, 14}, 0, "Cancel",          "cancel"  },
};
static const form_def_t kNewBranchForm = {
  .name        = "New Branch",
  .width       = 276,
  .height      = 86,
  .children    = kNewBranchCtrls,
  .child_count = 5,
  .ok_id       = CTL_CREATE,
  .cancel_id   = CTL_CANCEL,
};

typedef struct {
  bool result;
} new_branch_state_t;

static result_t new_branch_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      win->userdata = lparam;
      gc_state_t *gc = g_gc;
      window_t *from_cb = get_window_item(win, CTL_FROM);
      if (gc) {
        // Populate the "from" combobox with all branch names.
        for (int i = 0; i < gc->branch_count; i++) {
          if (!gc->branches[i].is_remote)
            send_message(from_cb, cbAddString, 0, gc->branches[i].name);
        }
        // Set current branch as default.
        char cur[256] = {0};
        git_current_branch(gc->repo, cur, sizeof(cur));
        set_window_item_text(win, CTL_FROM, "%s", cur);
      }
      return true;
    }

    case evPaint:
      draw_text_small("Name:", 4, 11, get_sys_color(brTextDisabled));
      draw_text_small("From:", 4, 29, get_sys_color(brTextDisabled));
      return false;

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (!src) return false;
        if (src->id == CTL_CANCEL) {
          end_dialog(win, 0);
          return true;
        }
        if (src->id == CTL_CREATE) {
          gc_state_t *gc = g_gc;
          if (!gc || !gc->repo) { end_dialog(win, 0); return true; }

          char name[256] = {0};
          window_t *name_edit = get_window_item(win, CTL_NAME);
          if (name_edit)
            strncpy(name, name_edit->title, sizeof(name) - 1);
          if (!name[0]) {
            message_box(win, "Please enter a branch name.", "New Branch", MB_OK);
            return true;
          }

          char from[256] = {0};
          window_t *from_edit = get_window_item(win, CTL_FROM);
          if (from_edit)
            strncpy(from, from_edit->title, sizeof(from) - 1);

          char buf[1024] = {0};
          const char *args_create[] = {
            "git", "branch", name, from[0] ? from : NULL, NULL
          };
          bool ok = git_run_sync(gc->repo, args_create, buf, sizeof(buf));
          if (!ok) {
            message_box(win, buf, "Branch failed", MB_OK);
            return true;
          }

          bool checkout = send_message(get_window_item(win, CTL_CHECKOUT),
                                       btnGetCheck, 0, NULL) != 0;
          if (checkout) {
            const char *args_co[] = { "git", "checkout", name, NULL };
            git_run_sync(gc->repo, args_co, buf, sizeof(buf));
          }

          new_branch_state_t *st = (new_branch_state_t *)win->userdata;
          if (st) st->result = true;
          end_dialog(win, 1);
          return true;
        }
      }
      return false;

    default:
      return false;
  }
}

bool gc_show_new_branch_dialog(window_t *parent) {
  new_branch_state_t st = { false };
  show_dialog_from_form(&kNewBranchForm, "New Branch", parent,
                         new_branch_proc, &st);
  if (st.result)
    gc_refresh_all();
  return st.result;
}
