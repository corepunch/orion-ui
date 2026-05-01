// VIEW: Post detail dialog — shows post body, threaded comments, and likes.
//
// Layout (client area POST_DLG_W x POST_DLG_H):
//   y=  4  Post title
//   y= 18  "by Author"
//   y= 32  Body text (wrapped, up to ~50px high)
//   y= 86  "N likes"  +  [♥ Like Post] button
//   y=104  Horizontal separator
//   y=108  "Comments (N):" label
//   y=122  Comments reportview (fills to y=272)
//   y=278  [Add Comment] [Add Reply] [Like Comment]  [Close]

#include "socialfeed.h"

// ============================================================
// Flat comment item — represents one row in the comment list
// (either a top-level comment or a reply)
// ============================================================

typedef struct {
  bool is_reply;
  int  comment_idx; // index into post->comments[]
  int  reply_idx;   // index into comment->replies[] (-1 for top-level)
} flat_item_t;

// ============================================================
// Post-detail dialog state
// ============================================================

#define MAX_FLAT_ITEMS 128

typedef struct {
  post_t      *post;
  int          post_idx;
  flat_item_t  flat[MAX_FLAT_ITEMS];
  int          flat_count;
  int          selected_flat;       // index into flat[], or -1
  window_t    *comments_win;
} post_detail_t;

// ============================================================
// build_flat — flatten comments+replies into the flat[] array
// ============================================================

static void build_flat(post_detail_t *s) {
  s->flat_count = 0;
  for (int ci = 0; ci < s->post->comment_count; ci++) {
    comment_t *c = s->post->comments[ci];
    if (!c) continue;

    if (s->flat_count < MAX_FLAT_ITEMS) {
      s->flat[s->flat_count].is_reply     = false;
      s->flat[s->flat_count].comment_idx  = ci;
      s->flat[s->flat_count].reply_idx    = -1;
      s->flat_count++;
    }

    for (int ri = 0; ri < c->reply_count && s->flat_count < MAX_FLAT_ITEMS; ri++) {
      s->flat[s->flat_count].is_reply     = true;
      s->flat[s->flat_count].comment_idx  = ci;
      s->flat[s->flat_count].reply_idx    = ri;
      s->flat_count++;
    }
  }
}

// ============================================================
// get_flat_item — return comment_t* for a flat row
// ============================================================

static comment_t *flat_to_comment(post_detail_t *s, int fi) {
  if (fi < 0 || fi >= s->flat_count) return NULL;
  flat_item_t *f = &s->flat[fi];
  comment_t   *c = s->post->comments[f->comment_idx];
  if (!c) return NULL;
  return f->is_reply ? c->replies[f->reply_idx] : c;
}

// ============================================================
// refresh_comments — rebuild comment reportview
// ============================================================

static void refresh_comments(post_detail_t *s) {
  if (!s || !s->comments_win) return;
  window_t *cv = s->comments_win;

  build_flat(s);

  send_message(cv, RVM_SETREDRAW, 0, NULL);
  send_message(cv, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
  send_message(cv, RVM_CLEARCOLUMNS, 0, NULL);

  rect_t cr  = get_client_rect(cv);
  int cv_w   = cr.w - SCROLLBAR_WIDTH;
  int auth_w = 70;
  int like_w = 45;
  int text_w = cv_w - auth_w - like_w;
  if (text_w < 20) text_w = 20;

  reportview_column_t col_author = { "Author",  (uint32_t)auth_w };
  reportview_column_t col_text   = { "Text",    (uint32_t)text_w };
  reportview_column_t col_likes  = { "Likes",   (uint32_t)like_w };

  send_message(cv, RVM_ADDCOLUMN, 0, &col_author);
  send_message(cv, RVM_ADDCOLUMN, 0, &col_text);
  send_message(cv, RVM_ADDCOLUMN, 0, &col_likes);

  send_message(cv, RVM_CLEAR, 0, NULL);

  char author_buf[128];
  char likes_buf[16];

  for (int i = 0; i < s->flat_count; i++) {
    flat_item_t *f    = &s->flat[i];
    comment_t   *item = flat_to_comment(s, i);
    if (!item) continue;

    snprintf(likes_buf, sizeof(likes_buf), "%d", item->like_count);

    if (f->is_reply) {
      snprintf(author_buf, sizeof(author_buf), "  -> %s", item->author);
    } else {
      strncpy(author_buf, item->author, sizeof(author_buf) - 1);
      author_buf[sizeof(author_buf) - 1] = '\0';
    }

    reportview_item_t row = {
      .text          = author_buf,
      .icon          = f->is_reply ? -1 : icon8_editor_helmet,
      .color         = get_sys_color(f->is_reply ? brTextDisabled : brTextNormal),
      .userdata      = (uint32_t)i,
      .subitems      = { item->text, likes_buf },
      .subitem_count = 2,
    };
    send_message(cv, RVM_ADDITEM, 0, &row);
  }

  if (s->selected_flat >= 0 && s->selected_flat < s->flat_count)
    send_message(cv, RVM_SETSELECTION, (uint32_t)s->selected_flat, NULL);

  send_message(cv, RVM_SETREDRAW, 1, NULL);
}

// ============================================================
// draw_post_header — custom paint for the post info section
// ============================================================

static void draw_post_header(post_detail_t *s) {
  post_t *p = s->post;

  uint32_t col_normal   = get_sys_color(brTextNormal);
  uint32_t col_disabled = get_sys_color(brTextDisabled);

  // Title
  draw_text_small(p->title, 4, 4, col_normal);

  // Author line
  char by_buf[160];
  snprintf(by_buf, sizeof(by_buf), "by %s", p->author);
  draw_text_small(by_buf, 4, 18, col_disabled);

  // Body (wrapped in a rect; reserve right side for the like button)
  rect_t body_rect = { 4, 32, POST_DLG_W - 110, 50 };
  draw_text_wrapped(p->body, &body_rect, col_normal);

  // Like count
  char like_buf[32];
  snprintf(like_buf, sizeof(like_buf),
           p->like_count == 1 ? "%d like" : "%d likes", p->like_count);
  draw_text_small(like_buf, 4, 88, col_disabled);

  // Separator line
  fill_rect(get_sys_color(brDarkEdge),  R(0, 102, POST_DLG_W, 1));
  fill_rect(get_sys_color(brLightEdge), R(0, 103, POST_DLG_W, 1));

  // "Comments (N):" label
  char hdr_buf[64];
  snprintf(hdr_buf, sizeof(hdr_buf),
           "Comments (%d):", s->post->comment_count);
  draw_text_small(hdr_buf, 4, 108, col_normal);
}

// ============================================================
// Post detail dialog window procedure
// ============================================================

static result_t post_detail_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  post_detail_t *s = (post_detail_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      // This dialog mixes a custom-drawn post-header section with a
      // win_reportview comment list (not a standard FORM_CTRL_* type).
      // Because win_reportview cannot be expressed in a form_def_t, all
      // child windows are created imperatively here rather than via a form.
      s = (post_detail_t *)lparam;
      win->userdata    = s;
      s->selected_flat = -1;

      // ---- Like Post button ----
      window_t *btn_like_post = create_window("Like Post", WINDOW_NOTITLE,
          MAKERECT(POST_DLG_W - 100, 82, 96, 18),
          win, win_button, 0, NULL);
      if (btn_like_post) btn_like_post->id = ID_BTN_LIKE_POST;

      // ---- Comments reportview ----
      s->comments_win = create_window("comments",
          WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
          MAKERECT(2, 122, POST_DLG_W - 4, 150),
          win, win_reportview, 0, NULL);

      // ---- Action buttons ----
      window_t *btn_add_cmt = create_window("Add Comment", WINDOW_NOTITLE,
          MAKERECT(2, 280, 90, 18),
          win, win_button, 0, NULL);
      if (btn_add_cmt) btn_add_cmt->id = ID_BTN_ADD_COMMENT;

      window_t *btn_add_rep = create_window("Add Reply", WINDOW_NOTITLE,
          MAKERECT(96, 280, 74, 18),
          win, win_button, 0, NULL);
      if (btn_add_rep) btn_add_rep->id = ID_BTN_ADD_REPLY;

      window_t *btn_like_cmt = create_window("Like Comment", WINDOW_NOTITLE,
          MAKERECT(174, 280, 90, 18),
          win, win_button, 0, NULL);
      if (btn_like_cmt) btn_like_cmt->id = ID_BTN_LIKE_COMMENT;

      window_t *btn_close = create_window("Close",
          WINDOW_NOTITLE | BUTTON_DEFAULT,
          MAKERECT(POST_DLG_W - 76, 280, 74, 18),
          win, win_button, 0, NULL);
      if (btn_close) btn_close->id = ID_BTN_CLOSE;

      refresh_comments(s);
      return true;
    }

    case evPaint:
      draw_post_header(s);
      return false;

    case evCommand: {
      uint16_t notif  = (uint16_t)HIWORD(wparam);
      window_t *src   = (window_t *)lparam;

      // ---- Reportview notifications ----
      if (notif == RVN_SELCHANGE) {
        s->selected_flat = (int)(int16_t)LOWORD(wparam);
        return true;
      }
      if (notif == RVN_DBLCLK) {
        // Double-click: treat as "like comment"
        comment_t *c = flat_to_comment(s, s->selected_flat);
        if (c) {
          comment_like(c);
          refresh_comments(s);
        }
        return true;
      }

      // ---- Button clicks ----
      if (notif != btnClicked || !src) return false;

      switch (src->id) {
        // ---- Like Post ----
        case ID_BTN_LIKE_POST:
          post_like(s->post);
          invalidate_window(win);
          SF_DEBUG("liked post id=%d likes=%d", s->post->id, s->post->like_count);
          return true;

        // ---- Add Comment ----
        case ID_BTN_ADD_COMMENT: {
          char author[64] = "";
          char text[512]  = "";
          if (show_new_comment_dialog(win, "New Comment",
                                      author, sizeof(author),
                                      text,   sizeof(text))) {
            comment_t *c = comment_create(author, text);
            if (c) {
              app_add_comment(s->post, c);
              refresh_comments(s);
              SF_DEBUG("comment added post_id=%d comment_id=%d", s->post->id, c->id);
            }
          }
          return true;
        }

        // ---- Add Reply ----
        case ID_BTN_ADD_REPLY: {
          int fi = s->selected_flat;
          if (fi < 0 || fi >= s->flat_count) {
            message_box(win, "Select a comment to reply to.",
                        "Add Reply", MB_OK);
            return true;
          }

          // Replies always attach to the top-level comment.
          int ci = s->flat[fi].comment_idx;
          comment_t *parent_c = s->post->comments[ci];
          if (!parent_c) return true;

          char author[64] = "";
          char text[512]  = "";
          char prompt[128];
          snprintf(prompt, sizeof(prompt), "Reply to %s", parent_c->author);

          if (show_new_comment_dialog(win, prompt,
                                      author, sizeof(author),
                                      text,   sizeof(text))) {
            comment_t *reply = comment_create(author, text);
            if (reply) {
              app_add_reply(parent_c, reply);
              refresh_comments(s);
              SF_DEBUG("reply added comment_idx=%d reply_id=%d", ci, reply->id);
            }
          }
          return true;
        }

        // ---- Like Comment ----
        case ID_BTN_LIKE_COMMENT: {
          comment_t *c = flat_to_comment(s, s->selected_flat);
          if (!c) {
            message_box(win, "Select a comment to like.",
                        "Like Comment", MB_OK);
            return true;
          }
          comment_like(c);
          refresh_comments(s);
          SF_DEBUG("liked comment idx=%d likes=%d", s->selected_flat, c->like_count);
          return true;
        }

        // ---- Close ----
        case ID_BTN_CLOSE:
          end_dialog(win, 0);
          return true;

        default:
          return false;
      }
    }

    case evClose:
      end_dialog(win, 0);
      return true;

    default:
      return false;
  }
}

// ============================================================
// show_post_detail — public entry point
// ============================================================

void show_post_detail(window_t *parent, int post_idx) {
  post_t *p = app_get_post(post_idx);
  if (!p) return;

  post_detail_t state = {
    .post        = p,
    .post_idx    = post_idx,
    .flat_count  = 0,
    .selected_flat = -1,
    .comments_win  = NULL,
  };

  show_dialog("Post Detail", POST_DLG_W, POST_DLG_H,
              parent, post_detail_proc, &state);
}
