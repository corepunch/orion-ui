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
//
// Appwrite structure mapping:
//   post_t.id    → Appwrite document $id in the "posts" collection
//   comment_t.id → Appwrite document $id in the "comments" collection
//   (replies are comments with a parent comment_id, stored in the same
//    "comments" collection with a "parent_id" relationship field)

#include "socialfeed.h"
#include "../../gem_magic.h"

// ============================================================
// Seed helpers
// ============================================================

// Create a comment, assign it a global ID, and attach it to a post.
static comment_t *seed_comment(post_t *p, const char *author, const char *text) {
  comment_t *c = comment_create(author, text);
  if (c) app_add_comment(p, c);
  return c;
}

// Create a reply, assign it a global ID, and attach it to a comment.
static comment_t *seed_reply(comment_t *parent, const char *author, const char *text) {
  comment_t *reply = comment_create(author, text);
  if (reply) app_add_reply(parent, reply);
  return reply;
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
  // All posts, comments, and replies are assigned monotonically increasing IDs
  // via app_add_post / app_add_comment / app_add_reply — mirroring the
  // Appwrite document ID assignment in the "posts" and "comments" collections.

  // Post 1 — alice: Orion UI is awesome!
  {
    post_t *p = post_create("alice",
        "Orion UI is awesome!",
        "Just started using Orion and the WinAPI-style message loop "
        "makes everything so familiar. Highly recommended for desktop apps.");
    if (p) {
      p->like_count = 12;
      app_add_post(p);

      comment_t *c0 = seed_comment(p, "bob",
          "Totally agree, the form_def_t API is very clean.");
      comment_t *c1 = seed_comment(p, "carol",
          "Does it support high-DPI?");
      seed_comment(p, "eve",
          "I migrated from FLTK and this feels much more natural.");

      if (c0) {
        c0->like_count = 4;
        seed_reply(c0, "alice", "Thanks! Took a while to get the DDX bindings right.");
        seed_reply(c0, "dave",  "Same experience here, love the DDX approach.");
      }
      if (c1) {
        c1->like_count = 3;
        comment_t *r = seed_reply(c1, "alice", "Yes, set UI_WINDOW_SCALE=2 in your build.");
        if (r) r->like_count = 2;
      }
    }
  }

  // Post 2 — bob: Custom controls in 50 lines
  {
    post_t *p = post_create("bob",
        "Custom controls in 50 lines",
        "You can write a fully custom control by implementing just evCreate, "
        "evPaint, evLeftButtonDown and evDestroy. The framework handles the "
        "rest through the message loop.");
    if (p) {
      p->like_count = 7;
      app_add_post(p);

      comment_t *c0 = seed_comment(p, "dave",
          "Great tip! I used this for a colour picker widget.");
      comment_t *c1 = seed_comment(p, "eve",
          "What about keyboard navigation?");
      comment_t *c2 = seed_comment(p, "frank",
          "Nice post, bookmarked!");

      if (c0) {
        c0->like_count = 3;
        seed_reply(c0, "bob", "Show us the colour picker! Sounds useful.");
      }
      if (c1) {
        comment_t *r0 = seed_reply(c1, "bob",
            "Handle evKeyDown and call invalidate_window().");
        comment_t *r1 = seed_reply(c1, "eve",
            "Thanks, that worked perfectly.");
        if (r0) r0->like_count = 1;
        if (r1) r1->like_count = 2;
      }
      if (c2) c2->like_count = 1;
    }
  }

  // Post 3 — carol: Scrollable panels with built-in scrollbars
  {
    post_t *p = post_create("carol",
        "Scrollable panels with built-in scrollbars",
        "Use WINDOW_VSCROLL on the content window, call set_scroll_info() "
        "in evCreate, and handle evVScroll to update your pan offset. "
        "No extra child scrollbar windows needed.");
    if (p) {
      p->like_count = 5;
      app_add_post(p);

      comment_t *c0 = seed_comment(p, "alice",
          "This tripped me up for days — glad you posted it!");
      comment_t *c1 = seed_comment(p, "frank",
          "Does this work the same for WINDOW_HSCROLL?");

      if (c0) {
        c0->like_count = 2;
        seed_reply(c0, "carol", "The exact same pattern applies to WINDOW_HSCROLL.");
      }
      if (c1) {
        comment_t *r = seed_reply(c1, "carol",
            "Yes — handle evHScroll identically, just change SB_HORZ to SB_VERT.");
        if (r) r->like_count = 1;
      }
    }
  }

  // Post 4 — dave: win_reportview cheat sheet
  {
    post_t *p = post_create("dave",
        "win_reportview cheat sheet",
        "RVM_ADDCOLUMN + RVM_ADDITEM to build the list, "
        "RVM_SETREDRAW(0) before bulk inserts and (1) after. "
        "Use RVM_SETREPORTCOLUMNWIDTH(0, ...) for the auto-stretch column.");
    if (p) {
      p->like_count = 9;
      app_add_post(p);

      comment_t *c0 = seed_comment(p, "alice",
          "The RVM_SETREDRAW trick alone saved me huge repaint jank — thanks!");
      comment_t *c1 = seed_comment(p, "bob",
          "How do you handle double-click selection?");
      comment_t *c2 = seed_comment(p, "carol",
          "Any way to get multi-column sorting?");

      if (c0) {
        c0->like_count = 5;
        seed_reply(c0, "dave", "Glad it helps — that one is documented in columnview.h.");
      }
      if (c1) {
        comment_t *r = seed_reply(c1, "dave",
            "Listen for RVN_DBLCLK in evCommand — LOWORD(wparam) is the row index.");
        if (r) r->like_count = 3;
      }
      if (c2) {
        c2->like_count = 2;
        seed_reply(c2, "dave",
            "Not built-in yet — you'd need to sort the data array and call feed_refresh().");
      }
    }
  }

  // Post 5 — eve: Dialogs using form_def_t + DDX
  {
    post_t *p = post_create("eve",
        "Dialogs using form_def_t + DDX",
        "Declare children in a static form_ctrl_def_t[] array, add a "
        "ctrl_binding_t[] for data exchange, then call show_dialog_from_form(). "
        "No imperative child creation needed — everything is declarative.");
    if (p) {
      p->like_count = 6;
      app_add_post(p);

      comment_t *c0 = seed_comment(p, "frank",
          "The BIND_MLSTRING type for multiline edits is especially handy.");
      comment_t *c1 = seed_comment(p, "alice",
          "Is there a way to add validation before the dialog closes?");

      if (c0) c0->like_count = 2;
      if (c1) {
        c1->like_count = 1;
        comment_t *r = seed_reply(c1, "eve",
            "Yes — handle evCommand/btnClicked for ID_OK, run validation, "
            "and only call end_dialog() when it passes.");
        if (r) r->like_count = 3;
        seed_reply(c1, "alice", "Perfect, that's exactly what I needed. Thanks!");
      }
    }
  }

  create_main_window();

  SF_DEBUG("gem_init complete: %d posts seeded (next_comment_id=%d)",
           g_app->post_count, g_app->next_comment_id);
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
#if SOCIALFEED_DEBUG
  axSetLogFile(NULL);
#endif
}

GEM_DEFINE("Social Feed", "1.0", gem_init, gem_shutdown, NULL)

GEM_STANDALONE_MAIN("Orion Social Feed", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
                    g_app->menubar_win, g_app->accel)
