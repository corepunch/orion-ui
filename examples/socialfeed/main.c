// Social Feed — entry point and application lifecycle.
//
// Demonstrates a social-media style feed with:
//   - Posts            : title, author, body, likes
//   - Comments         : attached to posts, can be liked
//   - Replies          : nested under comments, can be liked
//
// Architecture (MVC):
//   MODEL      : model_feed.c  — post_t / comment_t CRUD
//   CONTROLLER : controller_app.c — app_state_t, global operations
//   VIEW       : view_main.c / view_menubar.c /
//                view_dlg_post.c / view_dlg_forms.c

#include "socialfeed.h"
#include "../../gem_magic.h"

// ============================================================
// Seed helpers
// ============================================================

static void seed_comment(post_t *p, const char *author, const char *text) {
  comment_t *c = comment_create(author, text);
  if (c) post_add_comment(p, c);
}

static void seed_reply(post_t *p, int comment_idx,
                       const char *author, const char *text) {
  if (!p || comment_idx < 0 || comment_idx >= p->comment_count) return;
  comment_t *reply = comment_create(author, text);
  if (reply) comment_add_reply(p->comments[comment_idx], reply);
}

// ============================================================
// gem_init
// ============================================================

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  (void)argc; (void)argv;

#if SOCIALFEED_DEBUG
  {
    char log_path[1024];
    int n = snprintf(log_path, sizeof(log_path), "%s/socialfeed.log",
                     axSettingsDirectory());
    if (n > 0 && (size_t)n < sizeof(log_path))
      axSetLogFile(log_path);
  }
#endif

  g_app = app_init();
  if (!g_app) return false;
  g_app->hinstance = hinstance;

  create_menubar();

  // ---- Seed data ----

  // Post 1
  {
    post_t *p = post_create("alice",
        "Orion UI is awesome!",
        "Just started using Orion and the WinAPI-style message loop "
        "makes everything so familiar. Highly recommended for desktop apps.");
    if (p) {
      p->like_count = 12;
      app_add_post(p);

      seed_comment(p, "bob",   "Totally agree, the form_def_t API is very clean.");
      seed_comment(p, "carol", "Does it support high-DPI?");

      p->comments[0]->like_count = 4;
      seed_reply(p, 0, "alice", "Thanks! Took a while to get the DDX bindings right.");
      seed_reply(p, 1, "alice", "Yes, set UI_WINDOW_SCALE=2 in your build.");
      p->comments[1]->replies[0]->like_count = 2;
    }
  }

  // Post 2
  {
    post_t *p = post_create("bob",
        "Custom controls in 50 lines",
        "You can write a fully custom control by implementing just evCreate, "
        "evPaint, evLeftButtonDown and evDestroy. The framework handles the "
        "rest through the message loop.");
    if (p) {
      p->like_count = 7;
      app_add_post(p);

      seed_comment(p, "dave",  "Great tip! I used this for a colour picker widget.");
      seed_comment(p, "eve",   "What about keyboard navigation?");
      seed_comment(p, "frank", "Nice post, bookmarked!");

      p->comments[0]->like_count = 3;
      seed_reply(p, 1, "bob",  "Handle evKeyDown and call invalidate_window().");
      seed_reply(p, 1, "eve",  "Thanks, that worked perfectly.");
      p->comments[1]->replies[0]->like_count = 1;
    }
  }

  // Post 3
  {
    post_t *p = post_create("carol",
        "Scrollable panels with built-in scrollbars",
        "Use WINDOW_VSCROLL on the content window, call set_scroll_info() "
        "in evCreate, and handle evVScroll to update your pan offset. "
        "No extra child scrollbar windows needed.");
    if (p) {
      p->like_count = 5;
      app_add_post(p);

      seed_comment(p, "alice", "This tripped me up for days — glad you posted it!");
      p->comments[0]->like_count = 2;
    }
  }

  // Post 4
  {
    post_t *p = post_create("dave",
        "win_reportview cheat sheet",
        "RVM_ADDCOLUMN + RVM_ADDITEM to build the list, "
        "RVM_SETREDRAW(0) before bulk inserts and (1) after. "
        "Use RVM_SETREPORTCOLUMNWIDTH(0, ...) for the auto-stretch column.");
    if (p) {
      app_add_post(p);
    }
  }

  create_main_window();

  if (g_app->menubar_win && g_app->accel)
    send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators,
                 0, g_app->accel);

  SF_DEBUG("gem_init complete: %d posts seeded", g_app->post_count);
  return true;
}

// ============================================================
// gem_shutdown
// ============================================================

void gem_shutdown(void) {
  if (!g_app) return;
  SF_DEBUG("gem_shutdown");
  app_shutdown(g_app);
  g_app = NULL;
}

GEM_DEFINE("Social Feed", "1.0", gem_init, gem_shutdown, NULL)

GEM_STANDALONE_MAIN("Orion Social Feed", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
                    g_app->menubar_win, g_app->accel)
