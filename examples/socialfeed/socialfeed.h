#ifndef __SOCIALFEED_H__
#define __SOCIALFEED_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "../../ui.h"
#include "../../commctl/columnview.h"
#include "../../commctl/menubar.h"
#include "../../user/accel.h"
#include "../../user/icons.h"

#ifndef SOCIALFEED_DEBUG
#define SOCIALFEED_DEBUG 1
#endif

#if SOCIALFEED_DEBUG
#define SF_DEBUG(...) do { axLog("[socialfeed] " __VA_ARGS__); } while (0)
#else
#define SF_DEBUG(...) ((void)0)
#endif

// ============================================================
// Layout constants
// ============================================================

#define SCREEN_W  640
#define SCREEN_H  480

#define FEED_AUTHOR_W    80
#define FEED_LIKES_W     50
#define FEED_COMMENTS_W  55

// Post detail dialog dimensions (client area)
#define POST_DLG_W  520
#define POST_DLG_H  336

// ============================================================
// Menu / command IDs
// ============================================================

#define ID_FILE_QUIT      5

#define ID_POST_NEW      10
#define ID_POST_LIKE     11
#define ID_POST_VIEW     12
#define ID_POST_DELETE   13

#define ID_VIEW_REFRESH  20

#define ID_HELP_ABOUT   100

// ============================================================
// Dialog control IDs
// ============================================================

// New-post dialog
#define ID_POST_AUTHOR_CTRL  1001
#define ID_POST_TITLE_CTRL   1002
#define ID_POST_BODY_CTRL    1003

// New-comment / reply dialog
#define ID_CMT_AUTHOR_CTRL   2001
#define ID_CMT_TEXT_CTRL     2002

// Post detail — child window IDs
#define ID_BTN_LIKE_POST       301
#define ID_BTN_ADD_COMMENT     302
#define ID_BTN_ADD_REPLY       303
#define ID_BTN_LIKE_COMMENT    304
#define ID_BTN_CLOSE           305

// Shared OK / Cancel IDs
#define ID_OK      1
#define ID_CANCEL  2

// ============================================================
// Data capacity constants
// ============================================================

#define POSTS_INIT_CAP    16
#define COMMENTS_INIT_CAP  8
#define REPLIES_INIT_CAP   4

// ============================================================
// Data model
// ============================================================

typedef struct comment_s {
  int                id;
  char              *author;
  char              *text;
  int                like_count;
  uint32_t           created_at;
  struct comment_s **replies;
  int                reply_count;
  int                reply_cap;
} comment_t;

typedef struct {
  int        id;
  char      *author;
  char      *title;
  char      *body;
  int        like_count;
  uint32_t   created_at;
  comment_t **comments;
  int        comment_count;
  int        comment_cap;
} post_t;

// ============================================================
// Application state
// ============================================================

typedef struct {
  post_t     **posts;
  int          post_count;
  int          post_cap;
  int          next_id;          // next post ID (Appwrite document ID)
  int          next_comment_id;  // next comment / reply ID
  int          selected_idx;
  window_t    *menubar_win;
  window_t    *main_win;
  window_t    *feed_win;
  hinstance_t  hinstance;
  accel_table_t *accel;
} app_state_t;

extern app_state_t *g_app;

// ============================================================
// Model functions (model_feed.c)
// ============================================================

char      *sf_strdup(const char *s);

comment_t *comment_create(const char *author, const char *text);
void       comment_free(comment_t *c);
bool       comment_add_reply(comment_t *c, comment_t *reply);
void       comment_like(comment_t *c);

post_t    *post_create(const char *author, const char *title, const char *body);
void       post_free(post_t *p);
bool       post_add_comment(post_t *p, comment_t *c);
void       post_like(post_t *p);

// ============================================================
// Controller functions (controller_app.c)
// ============================================================

app_state_t *app_init(void);
void         app_shutdown(app_state_t *app);
bool         app_add_post(post_t *post);
bool         app_delete_post(int index);
post_t      *app_get_post(int index);
void         app_update_status(void);

// Append a comment to a post, assigning it a unique document ID.
// Mirrors app_add_post — callers must use this instead of post_add_comment()
// directly so that all comments are assigned monotonically increasing IDs.
bool         app_add_comment(post_t *post, comment_t *c);

// Append a reply to a comment, assigning it a unique document ID.
bool         app_add_reply(comment_t *parent, comment_t *reply);

// ============================================================
// View — menu bar (view_menubar.c)
// ============================================================

extern menu_def_t  kMenus[];
extern const int   kNumMenus;
void     handle_menu_command(uint16_t id);
result_t app_menubar_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam);
void     create_menubar(void);

// ============================================================
// View — main window (view_main.c)
// ============================================================

result_t main_win_proc(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam);
void     feed_refresh(void);
void     create_main_window(void);

// ============================================================
// View — post detail dialog (view_dlg_post.c)
// ============================================================

void show_post_detail(window_t *parent, int post_idx);

// ============================================================
// View — new post / comment dialogs (view_dlg_forms.c)
// ============================================================

bool show_new_post_dialog(window_t *parent);
bool show_new_comment_dialog(window_t *parent, const char *prompt_title,
                             char *author_buf, size_t author_sz,
                             char *text_buf,   size_t text_sz);

#endif // __SOCIALFEED_H__
