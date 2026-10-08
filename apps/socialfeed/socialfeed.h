#ifndef __SOCIALFEED_H__
#define __SOCIALFEED_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <orion/ui.h>
#include <orion/commctl/columnview.h>
#include <orion/commctl/menubar.h>
#include <orion/user/accel.h>

// All definitions generated from socialfeed.orion:
//   - Menu IDs, toolbar IDs, form definitions
//   - Database schema (table enums, structs, field bindings)
#include "build/generated/apps/socialfeed/socialfeed.h"

// ============================================================
// Database Implementation (db_simple_xml.c)
// ============================================================
//
// Messages supported (Zero Wrapper Structs API):
//   dbCreate - allocate userdata, parse lparam source path
//   dbLoad   - load from XML file
//   dbSave   - save to XML file (only if dirty)
//   dbInsert - wparam=TABLE_*; lparam=record_data → returns (lresult_t)record_ptr
//   dbUpdate - wparam=TABLE_*; lparam=record_ptr → returns 1 on success
//   dbDelete - wparam=TABLE_*; lparam=(void*)(intptr_t)record_id → returns 1 on success
//   dbFetch  - wparam=MAKEDWORD(TABLE_*,filter_field); lparam=(intptr_t)filter_value → returns (lresult_t)result_node_t*
//   dbFind   - wparam=MAKEDWORD(TABLE_*,search_field); lparam=(intptr_t)value or (void*)str → returns (lresult_t)record_ptr
//
// Example usage:
//   // Insert
//   author_t author = { .name = "alice" };
//   author_t *inserted = (author_t *)send_db_message(db, dbInsert, TABLE_AUTHORS, &author);
//
//   // Fetch all posts
//   result_node_t *posts = (result_node_t *)send_db_message(db, dbFetch,
//     MAKEDWORD(TABLE_POSTS, 0), (void *)(intptr_t)0);
//   int count = count_result_list(posts);
//   free_result_list(posts);
//
lresult_t db_simple_xml(database_t *db, uint32_t msg, uint32_t wparam, void *lparam);

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
// Data capacity constants
// ============================================================

#define POSTS_INIT_CAP    16
#define COMMENTS_INIT_CAP  8
#define REPLIES_INIT_CAP   4

// ============================================================
// Application state
// ============================================================

typedef struct {
  database_t  *db;           // Database for automatic view population
  int          selected_idx;
  window_t    *menubar_win;
  window_t    *main_win;
  window_t    *content_win;
  window_t    *feed_win;
  hinstance_t  hinstance;
  accel_table_t *accel;
} app_state_t;

extern app_state_t *g_app;

// ============================================================
// Controller functions (controller_app.c)
// ============================================================

app_state_t *app_init(void);
void         app_shutdown(app_state_t *app);
bool         app_delete_post(int index);
bool         app_like_post(int post_id);
bool         app_like_comment(int comment_id);
int          app_get_post_id_from_index(int index);
void         app_update_status(void);

// Append a comment to a post in database.
// Takes post_id, author_id, and comment text.
// Returns true if comment was successfully inserted.
bool         app_add_comment(int post_id, int author_id, const char *text);

// ============================================================
// View — menu bar (view_menubar.c)
// ============================================================

void     handle_menu_command(uint16_t id);
result_t app_menubar_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam);
void     create_menubar(void);

// ============================================================
// Test — database dialog tests (test_db_dialog.c)
// ============================================================

void test_author_edit_dialog(window_t *parent, database_t *db);
void test_new_author_dialog(window_t *parent, database_t *db);

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
// View — Dialogs (MIGRATED to show_db_dialog)
// ============================================================
// All form-based dialogs now use show_db_dialog() / show_db_dialog_ex().
// See view_menubar.c and view_dlg_post.c for usage.

#endif // __SOCIALFEED_H__
