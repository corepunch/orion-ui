// VIEW: Main window — feed list of posts (win_reportview).
//
// Main-window toolbar metadata is generated from socialfeed.orion.

#include "socialfeed.h"

#define FEED_CELL_TEXT_MAX 256
#define SF_MAX_LOG_TREE_DEPTH 6
#define SF_MAX_PAINT_LOGS 5

static int sf_count_children(window_t *win) {
  int n = 0;
  for (window_t *c = win ? win->children : NULL; c; c = c->next) n++;
  return n;
}

static void sf_log_window_tree(window_t *win, int depth) {
  if (!win || depth >= SF_MAX_LOG_TREE_DEPTH) return;
  SF_DEBUG("tree d=%d id=%u frame=%d,%d %dx%d flags=0x%08x visible=%d children=%d proc=%p title='%s'",
           depth, win->id, win->frame.x, win->frame.y, win->frame.w, win->frame.h,
           win->flags, window_has_state(win, WINDOW_STATE_VISIBLE), sf_count_children(win),
           (void *)win->proc, win->title);
  for (window_t *c = win->children; c; c = c->next)
    sf_log_window_tree(c, depth + 1);
}

// ============================================================
// feed_refresh — refresh tableview (automatic population!)
// ============================================================

void feed_refresh(void) {
  if (!g_app || !g_app->feed_win) return;
  SF_DEBUG("feed_refresh: feed_win=%p frame=%d,%d %dx%d visible=%d",
           (void *)g_app->feed_win,
           g_app->feed_win->frame.x, g_app->feed_win->frame.y,
           g_app->feed_win->frame.w, g_app->feed_win->frame.h,
           window_has_state(g_app->feed_win, WINDOW_STATE_VISIBLE));
  // tableview handles everything automatically
  send_message(g_app->feed_win, tvRefresh, 0, NULL);
  SF_DEBUG("feed_refresh done: rows=%d cols=%d",
           send_message(g_app->feed_win, RVM_GETITEMCOUNT, 0, NULL),
           send_message(g_app->feed_win, RVM_GETCOLUMNCOUNT, 0, NULL));
}

// ============================================================
// main_win_proc
// ============================================================

result_t main_win_proc(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      if (!g_app) return false;
      g_app->main_win = win;

      // The form creates stack -> tableview hierarchy automatically.
      // Database is auto-propagated to tableview during form creation.
      // Look up child windows by their IDs (generated from .orion).
      g_app->content_win = get_window_item(win, ID_MAIN_WINDOW_CONTENT);
      g_app->feed_win = get_window_item(win, ID_MAIN_WINDOW_FEED);
      SF_DEBUG("main evCreate: main=%p content=%p feed=%p",
               (void *)g_app->main_win, (void *)g_app->content_win, (void *)g_app->feed_win);
      if (g_app->main_win) {
        toolbar_state_t *tb = window_toolbar_state(g_app->main_win);
        SF_DEBUG("main evCreate toolbar: host=%p state=%p items=%d",
                 (void *)g_app->main_win->toolbar, (void *)tb, tb ? tb->item_count : -1);
      }
      sf_log_window_tree(win, 0);

      app_update_status();
      return true;

    case evResize:
      // Don't manually resize - auto-layout handles it
      return false;

    case evPaint:
      {
        static int paint_log_count = 0;
        if (paint_log_count < SF_MAX_PAINT_LOGS) {
          toolbar_state_t *tb = window_toolbar_state(win);
          SF_DEBUG("main evPaint[%d]: frame=%d,%d %dx%d content=%d,%d %dx%d feed=%d,%d %dx%d rows=%d tb_items=%d",
                   paint_log_count,
                   win->frame.x, win->frame.y, win->frame.w, win->frame.h,
                   g_app && g_app->content_win ? g_app->content_win->frame.x : -1,
                   g_app && g_app->content_win ? g_app->content_win->frame.y : -1,
                   g_app && g_app->content_win ? g_app->content_win->frame.w : -1,
                   g_app && g_app->content_win ? g_app->content_win->frame.h : -1,
                   g_app && g_app->feed_win ? g_app->feed_win->frame.x : -1,
                   g_app && g_app->feed_win ? g_app->feed_win->frame.y : -1,
                   g_app && g_app->feed_win ? g_app->feed_win->frame.w : -1,
                   g_app && g_app->feed_win ? g_app->feed_win->frame.h : -1,
                   g_app && g_app->feed_win ? send_message(g_app->feed_win, RVM_GETITEMCOUNT, 0, NULL) : -1,
                   tb ? tb->item_count : -1);
          paint_log_count++;
        }
      }
      return false;

    case evCommand: {
      switch (HIWORD(wparam)) {
        case btnClicked:
          handle_menu_command((uint16_t)LOWORD(wparam));
          return true;
        case kMenuBarNotificationItemClick:
          handle_menu_command((uint16_t)LOWORD(wparam));
          return true;

        case RVN_SELCHANGE:
          if (g_app)
            g_app->selected_idx = (int)(int16_t)LOWORD(wparam);
          return true;

        case RVN_DBLCLK:
          // Update selected_idx from the double-click event before viewing
          if (g_app)
            g_app->selected_idx = (int)(int16_t)LOWORD(wparam);
          handle_menu_command(ID_POST_VIEW);
          return true;

        case RVN_DELETE:
          handle_menu_command(ID_POST_DELETE);
          return true;

        default:
          return false;
      }
    }

    case evClose:
      ui_request_quit();
      return true;

    case evDestroy:
      if (g_app && g_app->main_win == win) {
        g_app->main_win = NULL;
        g_app->content_win = NULL;
        g_app->feed_win = NULL;
      }
      return false;

    default:
      return false;
  }
}

// ============================================================
// create_main_window
// ============================================================

void create_main_window(void) {
  if (!g_app) return;
  int x  = 40;
  int y  = MENUBAR_HEIGHT + 40;

  // Use the form defined in socialfeed.orion - it already contains
  // the stack and tableview with all bindings configured.
  // Database is automatically registered with framework at startup.
  window_t *win = create_window_from_form(&socialfeed_main_window_form,
                                          x, y,
                                          NULL, main_win_proc,
                                          g_app->hinstance, NULL);
  show_window(win, true);
}
