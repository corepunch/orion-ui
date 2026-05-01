// VIEW: Menu bar and command dispatch for Social Feed.

#include "socialfeed.h"
#include "../../gem_magic.h"

// ============================================================
// Menu definitions
// ============================================================

static const menu_item_t kFileItems[] = {
  {"Quit", ID_FILE_QUIT},
};

static const menu_item_t kPostItems[] = {
  {"New Post...",  ID_POST_NEW},
  {"Like Post",    ID_POST_LIKE},
  {"View Post...", ID_POST_VIEW},
  {NULL, 0},
  {"Delete Post",  ID_POST_DELETE},
};

static const menu_item_t kViewItems[] = {
  {"Refresh", ID_VIEW_REFRESH},
};

static const menu_item_t kHelpItems[] = {
  {"About...", ID_HELP_ABOUT},
};

menu_def_t kMenus[] = {
  {"File", kFileItems, (int)(sizeof(kFileItems)/sizeof(kFileItems[0]))},
  {"Post", kPostItems, (int)(sizeof(kPostItems)/sizeof(kPostItems[0]))},
  {"View", kViewItems, (int)(sizeof(kViewItems)/sizeof(kViewItems[0]))},
  {"Help", kHelpItems, (int)(sizeof(kHelpItems)/sizeof(kHelpItems[0]))},
};

const int kNumMenus = (int)(sizeof(kMenus)/sizeof(kMenus[0]));

// ============================================================
// Accelerator table
// ============================================================

static const accel_t kAccelEntries[] = {
  { FCONTROL|FVIRTKEY, AX_KEY_N,     ID_POST_NEW    },
  { FCONTROL|FVIRTKEY, AX_KEY_L,     ID_POST_LIKE   },
  { FVIRTKEY,          AX_KEY_ENTER, ID_POST_VIEW   },
  { FVIRTKEY,          AX_KEY_DEL,   ID_POST_DELETE },
};

// ============================================================
// handle_menu_command — dispatch File / Post / View / Help
// ============================================================

void handle_menu_command(uint16_t id) {
  if (!g_app) return;
  window_t *parent = g_app->main_win ? g_app->main_win
                                     : g_app->menubar_win;
  SF_DEBUG("command id=%u", (unsigned)id);

  switch (id) {
    // ---- File ----
    case ID_FILE_QUIT:
      ui_request_quit();
      break;

    // ---- Post ----
    case ID_POST_NEW:
      if (show_new_post_dialog(parent)) {
        feed_refresh();
        app_update_status();
        SF_DEBUG("action new_post count=%d", g_app->post_count);
      }
      break;

    case ID_POST_LIKE: {
      post_t *p = app_get_post(g_app->selected_idx);
      if (!p) {
        message_box(parent, "Select a post to like.", "Like Post", MB_OK);
        break;
      }
      post_like(p);
      feed_refresh();
      SF_DEBUG("liked post id=%d likes=%d", p->id, p->like_count);
      break;
    }

    case ID_POST_VIEW: {
      int idx = g_app->selected_idx;
      if (!app_get_post(idx)) {
        message_box(parent, "Select a post to view.", "View Post", MB_OK);
        break;
      }
      show_post_detail(parent, idx);
      feed_refresh();
      SF_DEBUG("action view_post idx=%d", idx);
      break;
    }

    case ID_POST_DELETE: {
      int idx = g_app->selected_idx;
      if (!app_get_post(idx)) {
        message_box(parent, "Select a post to delete.", "Delete Post", MB_OK);
        break;
      }
      if (message_box(parent, "Delete selected post?", "Delete Post",
                      MB_YESNO) == IDYES) {
        app_delete_post(idx);
        feed_refresh();
        app_update_status();
        SF_DEBUG("action delete_post idx=%d count=%d", idx, g_app->post_count);
      }
      break;
    }

    // ---- View ----
    case ID_VIEW_REFRESH:
      feed_refresh();
      break;

    // ---- Help ----
    case ID_HELP_ABOUT:
      message_box(parent,
                  "Social Feed v1.0\n\n"
                  "A demonstration of posts, comments,\n"
                  "replies, and likes in Orion UI.",
                  "About Social Feed", MB_OK);
      break;

    default:
      break;
  }
}

// ============================================================
// Menu bar window procedure
// ============================================================

result_t app_menubar_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCommand:
      if (HIWORD(wparam) == kMenuBarNotificationItemClick ||
          HIWORD(wparam) == kAcceleratorNotification) {
        handle_menu_command((uint16_t)LOWORD(wparam));
        return true;
      }
      return false;
    default:
      return win_menubar(win, msg, wparam, lparam);
  }
}

// ============================================================
// create_menubar — build the global menu bar
// ============================================================

void create_menubar(void) {
  g_app->menubar_win = set_app_menu(app_menubar_proc, kMenus, kNumMenus,
                                    handle_menu_command, g_app->hinstance);

  g_app->accel = load_accelerators(kAccelEntries,
      (int)(sizeof(kAccelEntries)/sizeof(kAccelEntries[0])));

  if (g_app->menubar_win)
    send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators,
                 0, g_app->accel);
}
