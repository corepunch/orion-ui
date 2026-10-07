// Git Client — entry point.

#include "gitclient.h"
#include <orion/gem.h>
#include <orion/commctl/commctl.h>
#include <orion/user/svg_icon_loader.h>

// ============================================================
// Module-level application state
// ============================================================

static gc_state_t g_gc_state;
gc_state_t *g_gc = NULL;

// ============================================================
// gem_init / gem_shutdown
// ============================================================

static void gc_startup_open_first(void) {
  for (int i = 0; i < g_gc->workspace_count; i++) {
    char checkout[512], main_root[512];
    if (!git_locate(g_gc->workspace[i], checkout, sizeof(checkout), main_root, sizeof(main_root))) continue;
    gc_open_repo(checkout);
    if (g_gc->repo) return;
  }
}

static void gc_startup_seed_recents(void) {
  char snap[GC_MAX_RECENT_REPOS][512];
  int n = g_gc->recent_repo_count;
  if (n > GC_MAX_RECENT_REPOS) n = GC_MAX_RECENT_REPOS;
  memcpy(snap, g_gc->recent_repos, (size_t)n * 512);
  for (int i = 0; i < n; i++) {
    char checkout[512], main_root[512];
    const char *root = snap[i];
    if (git_locate(snap[i], checkout, sizeof(checkout), main_root, sizeof(main_root))) root = main_root;
    bool have = false;
    for (int j = 0; j < g_gc->workspace_count; j++)
      if (!strcmp(g_gc->workspace[j], root)) { have = true; break; }
    if (have || g_gc->workspace_count >= GC_MAX_RECENT_REPOS) continue;
    strncpy(g_gc->workspace[g_gc->workspace_count], root, 511);
    g_gc->workspace[g_gc->workspace_count][511] = 0;
    g_gc->workspace_count++;
  }
  g_gc->workspace_dirty = false;
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
#if GITCLIENT_DEBUG
  {
    char log_path[1024];
    int n = snprintf(log_path, sizeof(log_path), "%s/gitclient.log",
                     axSettingsDirectory());
    if (n > 0 && (size_t)n < sizeof(log_path) && axSetLogFile(log_path))
      GC_LOG("logging initialized: %s", axGetLogFile());
  }
#endif

  memset(&g_gc_state, 0, sizeof(g_gc_state));
  g_gc = &g_gc_state;

  g_gc->hinstance       = hinstance;
  g_gc->selected_commit = -1;
  g_gc->selected_file   = -1;
  g_gc->unified_diff    = true;
  gc_recent_load();

  // Register app-specific icons so they are found by sysicon_resolve().
  {
    char icons_path[4096];
    int n = snprintf(icons_path, sizeof(icons_path), "%s/../share/gitclient/icons",
                     ui_get_exe_dir());
    if (n > 0 && (size_t)n < sizeof(icons_path))
      svg_add_icons_dir(icons_path);
  }

  // Register database classes and create databases.
  // history_db  — branches, commits, history files, tags, stash, remotes (default for form).
  // changes_db  — working-tree files from git status (wired in page_changes_proc evCreate).
  // github_db   — issues and pull requests from the gh CLI (wired in page_github_proc evCreate).
  DB_CLASS(gitclient_db);
  DB_CLASS(changes_database_proc);
  DB_CLASS(github_database_proc);
  g_gc->history_db = create_database("gitclient_history", "gitclient_db", NULL);
  g_gc->changes_db = create_database("gitclient_changes", "changes_database_proc", NULL);
  g_gc->github_db  = create_database("gitclient_github",  "github_database_proc",  NULL);
  if (!g_gc->history_db || !g_gc->changes_db || !g_gc->github_db) return false;
  register_database("db",        g_gc->history_db);
  register_database("github_db", g_gc->github_db);
  ui_set_database(g_gc->history_db);
  GC_LOG("databases ready: history=%p changes=%p github=%p",
         (void *)g_gc->history_db, (void *)g_gc->changes_db, (void *)g_gc->github_db);

  // Register commctl classes (tableview, stack, grid, etc.).
  register_commctl_classes();

  // Load gitclient component plugin (DiffView, etc.).
  {
    char path[4096];
    int n = snprintf(path, sizeof(path), "%s/../lib/gitclient_components%s",
                     ui_get_exe_dir(), AX_DYNLIB_EXT);
    if (n > 0 && (size_t)n < sizeof(path))
      fe_load_component_plugin(path);
  }

  // Menubar + accelerators.
  gc_create_menubar();

  // Create main window from form definition.
  g_gc->main_win = create_window_from_form(&gc_main_window_form, 16, 32,
                                           NULL, gc_main_proc,
                                           hinstance, NULL);
  if (!g_gc->main_win) return false;
  maximize_window(g_gc->main_win);
  show_window(g_gc->main_win, true);

#ifndef BUILD_AS_GEM
  ui_register_open_file_handler(gc_handle_open_file);
#endif

  const char *paths[GC_MAX_RECENT_REPOS]; int path_count = 0; bool want_overview = false;
  for (int i = 1; i < argc; i++) {
    if (!argv[i] || !argv[i][0]) continue;
    if (!strcmp(argv[i], "--overview")) want_overview = true;
    else if (path_count < GC_MAX_RECENT_REPOS) paths[path_count++] = argv[i];
  }
  if (path_count == 1 && gc_workspace_file_is(paths[0])) {
    gc_workspace_open(paths[0], false);
    if (want_overview) gc_set_view_mode(GC_TAB_OVERVIEW);
  } else if (path_count > 0) {
    for (int i = 0; i < path_count; i++) gc_workspace_add(paths[i]);
    g_gc->workspace_dirty = false;
    if (path_count == 1 && !want_overview) {
      gc_open_repo(paths[0]);
      g_gc->workspace_dirty = false;
    } else {
      gc_startup_open_first();
      g_gc->workspace_dirty = false;
      gc_set_view_mode(GC_TAB_OVERVIEW);
    }
  } else {
    char checkout[512], main_root[512];
    if (git_locate(".", checkout, sizeof(checkout), main_root, sizeof(main_root))) {
      gc_open_repo(".");
      g_gc->workspace_dirty = false;
    } else if (g_gc->recent_repo_count > 0) {
      gc_startup_seed_recents();
      gc_startup_open_first();
      g_gc->workspace_dirty = false;
      if (g_gc->workspace_count > 1) gc_set_view_mode(GC_TAB_OVERVIEW);
    } else GC_LOG("startup directory is not a repository; waiting for Open Repository");
  }

  return true;
}

void gem_shutdown(void) {
  // Destroy windows before unloading plugins — component procs (gc_diff_proc
  // etc.) live in the plugin dylib; unloading first leaves dangling proc
  // pointers that crash when cleanup_all_windows sends WM_DESTROY.
  if (g_gc && g_gc->main_win) {
    destroy_window(g_gc->main_win);
    g_gc->main_win = NULL;
  }
  fe_unload_component_plugins();
  if (g_gc) {
    if (g_gc->history_db) {
      if (ui_get_database() == g_gc->history_db)
        ui_set_database(NULL);
      destroy_database(g_gc->history_db);
      g_gc->history_db = NULL;
    }
    if (g_gc->changes_db) {
      destroy_database(g_gc->changes_db);
      g_gc->changes_db = NULL;
    }
    if (g_gc->github_db) {
      destroy_database(g_gc->github_db);
      g_gc->github_db = NULL;
    }
    if (g_gc->accel)
      free_accelerators(g_gc->accel);
    g_gc = NULL;
  }
#if GITCLIENT_DEBUG
  GC_LOG("logging shutdown");
  axSetLogFile(NULL);
#endif
}

GEM_DEFINE("Git Client", "1.0", gem_init, gem_shutdown, NULL)

GEM_STANDALONE_MAIN("Git Client", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
                    g_gc->menubar_win, g_gc->accel)
