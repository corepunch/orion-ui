// Modal commit dialog (form_def_t).
//
//  ┌─ Commit ──────────────────────────────────────────────┐
//  │ Message:                                              │
//  │ ┌────────────────────────────────────────────────┐   │
//  │ │                                                │   │
//  │ │                                                │   │
//  │ └────────────────────────────────────────────────┘   │
//  │ [ ] Amend last commit              [Cancel] [Commit] │
//  └───────────────────────────────────────────────────────┘

#include "gitclient.h"

// Control IDs
#define CTL_MSG     1
#define CTL_AMEND   2
#define CTL_COMMIT  3
#undef  CTL_CANCEL
#define CTL_CANCEL  4

static const form_ctrl_def_t kCommitCtrls[] = {
  { FORM_CTRL_MULTIEDIT, CTL_MSG,    {8,   20, 300, 60}, 0,              "",                 "msg"    },
  { FORM_CTRL_CHECKBOX,  CTL_AMEND,  {8,   86,  120, 13}, 0,              "Amend last commit","amend"  },
  { FORM_CTRL_BUTTON,    CTL_COMMIT, {220, 106,  45, 14}, BUTTON_DEFAULT, "Commit",           "ok"     },
  { FORM_CTRL_BUTTON,    CTL_CANCEL, {269, 106,  45, 14}, 0,              "Cancel",           "cancel" },
};
static const form_def_t kCommitForm = {
  .name        = "Commit",
  .width       = 324,
  .height      = 128,
  .children    = kCommitCtrls,
  .child_count = 4,
  .ok_id       = CTL_COMMIT,
  .cancel_id   = CTL_CANCEL,
};

// Dialog state passed via lparam.
typedef struct {
  bool  amend_requested;
  bool  result;          // true = committed
} commit_dlg_state_t;

static result_t commit_dlg_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  commit_dlg_state_t *st = (commit_dlg_state_t *)win->userdata;

  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      st = (commit_dlg_state_t *)lparam;
      if (st && st->amend_requested) {
        // Tick the amend checkbox and load the last commit message.
        send_message(get_window_item(win, CTL_AMEND),
                     btnSetCheck, 1, NULL);
        gc_state_t *gc = g_gc;
        if (gc && gc->repo) {
          char prev_msg[1024] = {0};
          const char *args[] = {
            "git", "log", "-1", "--format=%B", NULL
          };
          git_run_sync(gc->repo, args, prev_msg, sizeof(prev_msg));
          // Strip trailing newline.
          char *nl = strrchr(prev_msg, '\n');
          if (nl && *(nl+1) == '\0') *nl = '\0';
          send_message(get_window_item(win, CTL_MSG),
                       edSetText, 0, prev_msg);
        }
      }
      return true;

    case evPaint:
      draw_text_small("Message:", 4, 8, get_sys_color(brTextDisabled));
      return false;

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (!src) return false;
        if (src->id == CTL_CANCEL) {
          end_dialog(win, 0);
          return true;
        }
        if (src->id == CTL_COMMIT) {
          gc_state_t *gc = g_gc;
          if (!gc || !gc->repo) { end_dialog(win, 0); return true; }

          // Retrieve message text.
          char msg_text[2048] = {0};
          send_message(get_window_item(win, CTL_MSG),
                       edGetText,
                       sizeof(msg_text), msg_text);

          if (!msg_text[0]) {
            message_box(win, "Please enter a commit message.",
                        "Commit", MB_OK);
            return true;
          }

          bool amend = send_message(get_window_item(win, CTL_AMEND),
                                    btnGetCheck, 0, NULL) != 0;

          char buf[4096] = {0};
          bool ok;
          if (amend) {
            const char *args[] = {
              "git", "commit", "--amend", "-m", msg_text, NULL
            };
            ok = git_run_sync(gc->repo, args, buf, sizeof(buf));
          } else {
            const char *args[] = {
              "git", "commit", "-m", msg_text, NULL
            };
            ok = git_run_sync(gc->repo, args, buf, sizeof(buf));
          }

          if (!ok) {
            message_box(win, buf, "Commit failed", MB_OK);
            return true;
          }

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

bool gc_show_commit_dialog(window_t *parent, bool amend) {
  commit_dlg_state_t st = { .amend_requested = amend, .result = false };
  show_dialog_from_form(&kCommitForm, "Commit", parent, commit_dlg_proc, &st);
  if (st.result)
    gc_refresh_all();
  return st.result;
}
