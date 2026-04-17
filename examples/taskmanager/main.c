// Orion Task Manager — entry point and application lifecycle.
//
// Demonstrates MVC-style architecture with:
//   - MODEL      : model_task.c   — task_t CRUD
//   - CONTROLLER : controller_app.c — app_state_t, command routing
//   - VIEW       : view_main.c / view_menubar.c / view_tasklist.c /
//                  view_dlg_task.c / view_dlg_about.c

#include "taskmanager.h"
#include "../../gem_magic.h"

// ============================================================
// gem_init / gem_shutdown (works standalone and as a .gem)
// ============================================================

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  (void)argc; (void)argv;

  g_app = app_init();
  if (!g_app) return false;

  g_app->hinstance = hinstance;
  // Seed the app with 20 example tasks spread across priorities and statuses.
  {
    struct { const char *title; const char *desc; task_priority_t prio; task_status_t status; } seed[] = {
      { "Set up development environment",  "Install SDK, IDE and configure build tools.",           PRIORITY_HIGH,   STATUS_COMPLETED  },
      { "Write project specification",     "Document requirements and scope for the next release.", PRIORITY_NORMAL, STATUS_COMPLETED  },
      { "Design database schema",          "Define tables, indexes and foreign-key constraints.",   PRIORITY_HIGH,   STATUS_COMPLETED  },
      { "Implement user authentication",   "Login, logout, password reset flows.",                  PRIORITY_URGENT, STATUS_INPROGRESS },
      { "Build REST API endpoints",        "CRUD operations for resources under /api/v1/.",         PRIORITY_HIGH,   STATUS_INPROGRESS },
      { "Create CI/CD pipeline",           "Set up automated build, test and deploy workflows.",    PRIORITY_NORMAL, STATUS_INPROGRESS },
      { "Write unit tests for model",      "Achieve >=80% coverage on the data layer.",            PRIORITY_HIGH,   STATUS_TODO       },
      { "Write integration tests",         "End-to-end tests for all API routes.",                  PRIORITY_NORMAL, STATUS_TODO       },
      { "Review third-party licenses",     "Audit all dependencies for license compatibility.",     PRIORITY_LOW,    STATUS_TODO       },
      { "Performance profiling",           "Profile hot paths and reduce P99 latency.",             PRIORITY_HIGH,   STATUS_TODO       },
      { "Security audit",                  "Penetration testing and OWASP Top-10 review.",          PRIORITY_URGENT, STATUS_TODO       },
      { "Update API documentation",        "Regenerate OpenAPI spec and publish to docs site.",     PRIORITY_NORMAL, STATUS_TODO       },
      { "Migrate legacy data",             "Transform and import records from the old database.",   PRIORITY_HIGH,   STATUS_TODO       },
      { "Implement export to CSV",         "Allow users to download their data as a CSV file.",     PRIORITY_NORMAL, STATUS_TODO       },
      { "Add dark mode support",           "Detect system preference and apply dark theme.",        PRIORITY_LOW,    STATUS_TODO       },
      { "Fix pagination bug",              "Off-by-one error on the last page of results.",         PRIORITY_HIGH,   STATUS_TODO       },
      { "Localize UI strings",             "Extract all user-visible strings into resource files.", PRIORITY_NORMAL, STATUS_TODO       },
      { "Accessibility review",            "Ensure keyboard nav and screen-reader compatibility.",  PRIORITY_NORMAL, STATUS_TODO       },
      { "Prepare release notes",           "Summarise changes since the previous release tag.",     PRIORITY_LOW,    STATUS_TODO       },
      { "Deploy to production",            "Tag release, build artifacts and roll out to prod.",    PRIORITY_URGENT, STATUS_TODO       },
    };
    uint32_t base = (uint32_t)time(NULL);
    for (int i = 0; i < 20; i++) {
      task_t *t = task_create(seed[i].title, seed[i].desc,
                              seed[i].prio, seed[i].status,
                              base + (uint32_t)(i + 1) * 86400u);
      if (t) app_add_task(g_app, t);
    }
    g_app->modified = false;
  }

  // Build the menu bar.
  create_menubar();

  // Create the main document window.
  int sw = MIN(480, ui_get_system_metrics(kSystemMetricScreenWidth));
  int sh = MIN(320, ui_get_system_metrics(kSystemMetricScreenHeight));
  window_t *mw = create_window(
      "Task Manager",
      WINDOW_STATUSBAR | WINDOW_TOOLBAR,
      MAKERECT(MAIN_WIN_X, MAIN_WIN_Y,
               sw - MAIN_WIN_X - 4,
               sh - MAIN_WIN_Y - 4),
      NULL, main_win_proc, hinstance, NULL);
  // Note: g_app->main_win is set inside main_win_proc's kWindowMessageCreate
  // so that app_update_status() works correctly during initial window setup.
  show_window(mw, true);

  // Attach accelerators to the menu bar for hotkey hints in menus.
  if (g_app->menubar_win && g_app->accel)
    send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators,
                 0, g_app->accel);

  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;
  app_shutdown(g_app);
  g_app = NULL;
}

GEM_DEFINE("Task Manager", "1.0", gem_init, gem_shutdown, NULL)

// ============================================================
// Standalone main
// ============================================================

#ifndef BUILD_AS_GEM
int main(int argc, char *argv[]) {
  if (!ui_init_graphics(UI_INIT_DESKTOP, "Orion Task Manager",
                        SCREEN_W, SCREEN_H))
    return 1;

  if (!gem_init(argc, argv, 0)) {
    ui_shutdown_graphics();
    return 1;
  }

  while (ui_is_running()) {
    ui_event_t e;
    while (get_message(&e)) {
      if (!translate_accelerator(g_app->menubar_win, &e, g_app->accel))
        dispatch_message(&e);
    }
    repost_messages();
  }

  gem_shutdown();
  ui_shutdown_graphics();
  return 0;
}
#endif
