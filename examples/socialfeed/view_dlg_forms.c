// VIEW: Form-based dialogs — New Post and New Comment / Reply.

#include "socialfeed.h"

// ============================================================
// New Post dialog (form-based)
// ============================================================

static const form_ctrl_def_t kNewPostChildren[] = {
  { FORM_CTRL_LABEL,     -1,                  { 4,  8, 58, 13 }, 0,              "Author:",  "lbl_author" },
  { FORM_CTRL_TEXTEDIT,  ID_POST_AUTHOR_CTRL, { 64, 6, 200, 16 }, 0,             "",         "edit_author"},

  { FORM_CTRL_LABEL,     -1,                  { 4, 28, 58, 13 }, 0,              "Title:",   "lbl_title"  },
  { FORM_CTRL_TEXTEDIT,  ID_POST_TITLE_CTRL,  { 64, 26, 200, 16 }, 0,            "",         "edit_title" },

  { FORM_CTRL_LABEL,     -1,                  { 4, 48, 58, 13 }, 0,              "Body:",    "lbl_body"   },
  { FORM_CTRL_MULTIEDIT, ID_POST_BODY_CTRL,   { 64, 46, 200, 60 }, 0,            "",         "edit_body"  },

  { FORM_CTRL_BUTTON, ID_OK,     { 72, 118, 60, 18 }, BUTTON_DEFAULT, "Post",   "btn_ok"     },
  { FORM_CTRL_BUTTON, ID_CANCEL, { 136, 118, 60, 18 }, 0,             "Cancel", "btn_cancel" },
};

typedef struct {
  char author[64];
  char title[128];
  char body[512];
  bool accepted;
} new_post_state_t;

static const ctrl_binding_t kNewPostBindings[] = {
  { ID_POST_AUTHOR_CTRL, BIND_STRING,   offsetof(new_post_state_t, author), sizeof_field(new_post_state_t, author) },
  { ID_POST_TITLE_CTRL,  BIND_STRING,   offsetof(new_post_state_t, title),  sizeof_field(new_post_state_t, title)  },
  { ID_POST_BODY_CTRL,   BIND_MLSTRING, offsetof(new_post_state_t, body),   sizeof_field(new_post_state_t, body)   },
};

static const form_def_t kNewPostForm = {
  .name          = "New Post",
  .width         = 272,
  .height        = 146,
  .children      = kNewPostChildren,
  .child_count   = (int)(sizeof(kNewPostChildren)/sizeof(kNewPostChildren[0])),
  .bindings      = kNewPostBindings,
  .binding_count = (int)(sizeof(kNewPostBindings)/sizeof(kNewPostBindings[0])),
  .ok_id         = ID_OK,
  .cancel_id     = ID_CANCEL,
};

static result_t new_post_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  new_post_state_t *s = (new_post_state_t *)win->userdata;

  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      s = (new_post_state_t *)lparam;
      dialog_push(win, s, kNewPostBindings,
                  (int)(sizeof(kNewPostBindings)/sizeof(kNewPostBindings[0])));
      return true;

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (src->id == ID_OK) {
          dialog_pull(win, s, kNewPostBindings,
                      (int)(sizeof(kNewPostBindings)/sizeof(kNewPostBindings[0])));

          if (s->author[0] == '\0') {
            message_box(win, "Author is required.", "Validation", MB_OK);
            return true;
          }
          if (s->title[0] == '\0') {
            message_box(win, "Title is required.", "Validation", MB_OK);
            return true;
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

bool show_new_post_dialog(window_t *parent) {
  new_post_state_t state = { "", "", "", false };

  show_dialog_from_form(&kNewPostForm, "New Post", parent,
                        new_post_proc, &state);

  if (!state.accepted) return false;

  post_t *p = post_create(state.author, state.title, state.body);
  return p ? app_add_post(p) : false;
}

// ============================================================
// New Comment / Reply dialog (form-based)
// ============================================================

static const form_ctrl_def_t kNewCommentChildren[] = {
  { FORM_CTRL_LABEL,    -1,               { 4,  8, 58, 13 }, 0,              "Author:", "lbl_author" },
  { FORM_CTRL_TEXTEDIT, ID_CMT_AUTHOR_CTRL, { 64, 6, 188, 16 }, 0,           "",        "edit_author"},

  { FORM_CTRL_LABEL,    -1,               { 4, 28, 58, 13 }, 0,              "Text:",   "lbl_text"   },
  { FORM_CTRL_TEXTEDIT, ID_CMT_TEXT_CTRL, { 64, 26, 188, 16 }, 0,            "",        "edit_text"  },

  { FORM_CTRL_BUTTON, ID_OK,     { 62, 54, 60, 18 }, BUTTON_DEFAULT, "Submit", "btn_ok"     },
  { FORM_CTRL_BUTTON, ID_CANCEL, { 126, 54, 60, 18 }, 0,             "Cancel", "btn_cancel" },
};

typedef struct {
  char *author_buf;
  size_t author_sz;
  char *text_buf;
  size_t text_sz;
  bool accepted;
} new_comment_state_t;

static result_t new_comment_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  new_comment_state_t *s = (new_comment_state_t *)win->userdata;

  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      return true;

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (src->id == ID_OK) {
          s = (new_comment_state_t *)win->userdata;

          window_t *ea = get_window_item(win, ID_CMT_AUTHOR_CTRL);
          window_t *et = get_window_item(win, ID_CMT_TEXT_CTRL);

          if (!ea || ea->title[0] == '\0') {
            message_box(win, "Author is required.", "Validation", MB_OK);
            return true;
          }
          if (!et || et->title[0] == '\0') {
            message_box(win, "Text is required.", "Validation", MB_OK);
            return true;
          }

          strncpy(s->author_buf, ea->title, s->author_sz - 1);
          s->author_buf[s->author_sz - 1] = '\0';
          strncpy(s->text_buf,   et->title, s->text_sz - 1);
          s->text_buf[s->text_sz - 1] = '\0';

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

static const form_def_t kNewCommentForm = {
  .name        = "New Comment",
  .width       = 260,
  .height      = 82,
  .children    = kNewCommentChildren,
  .child_count = (int)(sizeof(kNewCommentChildren)/sizeof(kNewCommentChildren[0])),
};

bool show_new_comment_dialog(window_t *parent, const char *prompt_title,
                             char *author_buf, size_t author_sz,
                             char *text_buf,   size_t text_sz) {
  new_comment_state_t state = {
    .author_buf = author_buf,
    .author_sz  = author_sz,
    .text_buf   = text_buf,
    .text_sz    = text_sz,
    .accepted   = false,
  };

  show_dialog_from_form(&kNewCommentForm, prompt_title, parent,
                        new_comment_proc, &state);

  return state.accepted;
}
