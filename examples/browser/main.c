#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../ui.h"
#include "../../gem_magic.h"

#include "browser.h"

typedef struct {
  window_t *menubar_win;
  accel_table_t *accel;
  bool http_ready;
  hinstance_t hinstance;
  browser_state_t *windows;
  browser_state_t *active;
} app_state_t;

static app_state_t g_app = {0};

static const accel_t kBrowserAccelEntries[] = {
  { FCONTROL|FVIRTKEY, AX_KEY_N,     ID_MENU_FILE_NEW },
  { FCONTROL|FVIRTKEY, AX_KEY_O,     ID_MENU_FILE_OPEN },
  { FCONTROL|FVIRTKEY, AX_KEY_S,     ID_MENU_FILE_SAVE },
  { FCONTROL|FVIRTKEY, AX_KEY_Q,     ID_MENU_FILE_QUIT },
  { FCONTROL|FVIRTKEY, AX_KEY_COMMA, ID_MENU_BROWSER_SETTINGS },
  { FVIRTKEY,          AX_KEY_F1,    ID_MENU_HELP_ABOUT },
};

static const menu_item_t kFileItems[] = {
  {"New", ID_MENU_FILE_NEW},
  {"Open...", ID_MENU_FILE_OPEN},
  {"Save HTML...", ID_MENU_FILE_SAVE},
  {"", 0},
  {"Quit", ID_MENU_FILE_QUIT},
};

static const menu_item_t kBrowserSettingsItems[] = {
  {"Preferences...", ID_MENU_BROWSER_SETTINGS},
};

static const menu_item_t kHelpItems[] = {
  {"About...", ID_MENU_HELP_ABOUT},
};

static const menu_def_t kBrowserMenus[] = {
  {"File", kFileItems, 5},
  {"Settings", kBrowserSettingsItems, 1},
  {"Help", kHelpItems, 1},
};

static void browser_handle_menu_command(uint16_t id);
static result_t browser_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
static result_t browser_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

static browser_state_t *browser_state_from_root(window_t *root) {
  for (browser_state_t *it = g_app.windows; it; it = it->next) {
    if (it->win == root) return it;
  }
  return NULL;
}

static browser_state_t *browser_get_active_state(void) {
  if (g_ui_runtime.focused) {
    browser_state_t *focused = browser_state_from_root(get_root_window(g_ui_runtime.focused));
    if (focused) {
      g_app.active = focused;
      return focused;
    }
  }

  if (g_app.active && g_app.active->win)
    return g_app.active;

  return g_app.windows;
}

static window_t *browser_get_active_window(void) {
  browser_state_t *st = browser_get_active_state();
  return st ? st->win : NULL;
}

static void browser_track_window(browser_state_t *st) {
  if (!st) return;
  st->prev = NULL;
  st->next = g_app.windows;
  if (g_app.windows) g_app.windows->prev = st;
  g_app.windows = st;
  g_app.active = st;
}

static void browser_untrack_window(browser_state_t *st) {
  if (!st) return;

  if (st->prev) st->prev->next = st->next;
  else g_app.windows = st->next;
  if (st->next) st->next->prev = st->prev;

  if (g_app.active == st)
    g_app.active = g_app.windows;

  st->next = NULL;
  st->prev = NULL;
}

static void browser_destroy_all_windows(void) {
  while (g_app.windows) {
    window_t *win = g_app.windows->win;
    if (!win) break;
    destroy_window(win);
  }
}

static void browser_make_default_save_name(const browser_state_t *st,
                                           char *path, size_t path_sz) {
  const char *name = "page.html";

  if (!path || path_sz == 0) return;
  path[0] = '\0';

  if (st && st->current_url[0]) {
    if (browser_is_file_url(st->current_url)) {
      if (browser_url_to_local_path(st->current_url, path, path_sz)) return;
    } else {
      const char *slash = strrchr(st->current_url, '/');
      const char *base = slash ? slash + 1 : st->current_url;
      size_t len = strcspn(base, "?#");
      if (len > 0 && len + 1 < path_sz) {
        snprintf(path, path_sz, "%.*s", (int)len, base);
        if (!strchr(path, '.')) {
          size_t used = strlen(path);
          if (used + 5 < path_sz)
            snprintf(path + used, path_sz - used, ".html");
        }
        return;
      }
    }
  }

  snprintf(path, path_sz, "%s", name);
}

static void browser_run_open(window_t *parent) {
  char path[1024] = {0};
  window_t *win = browser_get_active_window();

  if (!win) return;
  if (!browser_pick_open_path(parent ? parent : win, path, sizeof(path))) return;
  if (!browser_load_local_file(win, path, true)) {
    message_box(parent ? parent : win,
                "Failed to open the selected HTML file.",
                "Open Failed", MB_OK);
  }
}

static void browser_run_save(window_t *parent) {
  browser_state_t *st = browser_get_active_state();
  char path[1024] = {0};

  if (!st || !st->win) return;
  if (!st->html_raw) {
    message_box(parent ? parent : st->win,
                "There is no loaded HTML source to save yet.",
                "Save HTML", MB_OK);
    return;
  }

  browser_make_default_save_name(st, path, sizeof(path));
  if (!browser_pick_save_path(parent ? parent : st->win, path, sizeof(path))) return;
  if (!browser_save_html_file(st->win, path)) {
    message_box(parent ? parent : st->win,
                "Failed to save the current HTML document.",
                "Save Failed", MB_OK);
  }
}

static bool browser_open_window(const char *initial_url) {
  rect_t wr;
  window_t *win;

  if (!g_app.http_ready) {
    if (!http_init()) return false;
    g_app.http_ready = true;
  }

  wr.x = CW_USEDEFAULT;
  wr.y = CW_USEDEFAULT;
  wr.w = 480;
  wr.h = 320 + TITLEBAR_HEIGHT;

  win = create_window(
    "Browser (MVP)",
    WINDOW_TOOLBAR,
    &wr,
    NULL,
    browser_proc,
    g_app.hinstance,
    NULL
  );
  if (!win) return false;

  if (!g_app.menubar_win) {
    g_app.menubar_win = set_app_menu(
      browser_menubar_proc,
      kBrowserMenus,
      (int)(sizeof(kBrowserMenus) / sizeof(kBrowserMenus[0])),
      browser_handle_menu_command,
      g_app.hinstance
    );
    if (!g_app.accel) {
      g_app.accel = load_accelerators(
        kBrowserAccelEntries,
        (int)(sizeof(kBrowserAccelEntries) / sizeof(kBrowserAccelEntries[0]))
      );
    }
    if (g_app.menubar_win && g_app.accel)
      send_message(g_app.menubar_win, kMenuBarMessageSetAccelerators, 0, g_app.accel);
  }

  show_window(win, true);
  move_to_top(win);
  set_focus(win);

  if (initial_url && initial_url[0])
    browser_navigate(win, initial_url, true);

  return true;
}

static void browser_handle_menu_command(uint16_t id) {
  window_t *active = browser_get_active_window();

  switch (id) {
    case ID_MENU_FILE_NEW:
      browser_open_window(NULL);
      return;

    case ID_MENU_FILE_OPEN:
      browser_run_open(active);
      return;

    case ID_MENU_FILE_SAVE:
      browser_run_save(active);
      return;

    case ID_MENU_FILE_QUIT:
      browser_destroy_all_windows();
#ifndef BUILD_AS_GEM
      if (!g_app.windows) ui_request_quit();
#endif
      return;

    case ID_MENU_BROWSER_SETTINGS:
    case ID_MENU_HELP_ABOUT:
      if (active)
        send_message(active, evCommand, MAKEDWORD(id, kMenuBarNotificationItemClick), NULL);
      return;

    default:
      return;
  }
}

static result_t browser_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evCommand) {
    if (HIWORD(wparam) == kMenuBarNotificationItemClick ||
        HIWORD(wparam) == kAcceleratorNotification) {
      browser_handle_menu_command(LOWORD(wparam));
      return true;
    }
  }
  return win_menubar(win, msg, wparam, lparam);
}

static result_t browser_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  browser_state_t *st = (browser_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      st = (browser_state_t *)calloc(1, sizeof(*st));
      if (!st) return false;
      st->request_id = HTTP_INVALID_REQUEST;
      st->history_index = -1;
      st->win = win;
      win->userdata = st;
      browser_track_window(st);

      browser_settings_init(st);
      browser_settings_load(st);

      browser_rebuild_toolbar(win);

      window_t *body = create_window(
        "",
        WINDOW_NOTITLE,
        MAKERECT(0, 0, win->frame.w, win->frame.h),
        win,
        win_multiedit,
        0,
        NULL
      );
      if (body) body->id = ID_BODY_VIEW;

      browser_update_layout(win);
      browser_set_body_text(win, "Type a URL and press Enter.");
      browser_navigate(win, st->home_url, true);
      return true;
    }

    case evSetFocus:
      if (st) g_app.active = st;
      return false;

    case evActivate:
      if (st && wparam != WA_INACTIVE) g_app.active = st;
      return false;

    case evClose:
      destroy_window(win);
      return true;

    case evResize:
      browser_rebuild_toolbar(win);
      browser_update_layout(win);
      return false;

    case evCommand:
      if (HIWORD(wparam) == kMenuBarNotificationItemClick ||
          HIWORD(wparam) == kAcceleratorNotification) {
        if (!st) return false;
        switch (LOWORD(wparam)) {
          case ID_MENU_BROWSER_SETTINGS:
            browser_show_settings_window(win, st);
            return true;

          case ID_MENU_HELP_ABOUT:
            browser_show_about_dialog(win);
            return true;

          default:
            break;
        }
      }
      if (HIWORD(wparam) == edUpdate && LOWORD(wparam) == ID_TB_ADDR) {
        window_t *src = (window_t *)lparam;
        browser_navigate(win, src ? src->title : "", true);
        return true;
      }
      return false;

    case tbButtonClick:
      if (!st) return false;
      if ((int)wparam == ID_TB_BACK && st->history_index > 0) {
        st->history_index--;
        browser_navigate(win, st->history[st->history_index], false);
        return true;
      }
      if ((int)wparam == ID_TB_FWD && st->history_index + 1 < st->history_count) {
        st->history_index++;
        browser_navigate(win, st->history[st->history_index], false);
        return true;
      }
      if ((int)wparam == ID_TB_HOME) {
        browser_navigate(win, st->home_url, true);
        return true;
      }
      if ((int)wparam == ID_TB_REFRESH) {
        if (st->current_url[0])
          browser_navigate(win, st->current_url, false);
        return true;
      }
      return false;

    case evHttpDone: {
      if (!st) return false;
      http_request_id_t req = (http_request_id_t)wparam;
      http_response_t *resp = (http_response_t *)lparam;

      st->loading = false;
      if (req == st->request_id) st->request_id = HTTP_INVALID_REQUEST;

      if (!resp) {
        browser_set_body_text(win, "Request failed (no response object).");
        return true;
      }

      free(st->html_raw);
      st->html_raw = NULL;
      free(st->render_text);
      st->render_text = NULL;

      if (resp->error) {
        size_t need = strlen("Request error: ") + strlen(resp->error) + 1;
        st->render_text = (char *)malloc(need);
        if (st->render_text)
          snprintf(st->render_text, need, "Request error: %s", resp->error);
        else
          st->render_text = strdup("Request error.");
        browser_set_body_text(win, st->render_text);
        http_response_free(resp);
        return true;
      }

      browser_apply_html(win, resp->body, resp->body_len, st->current_url);

      http_response_free(resp);
      return true;
    }

    case evDestroy:
      if (st) {
        if (st->settings_win && is_window(st->settings_win))
          destroy_window(st->settings_win);
        if (st->request_id != HTTP_INVALID_REQUEST)
          http_cancel(st->request_id);
        browser_history_free(st);
        browser_untrack_window(st);
        free(st->html_raw);
        free(st->render_text);
        free(st);
        win->userdata = NULL;
      }
      if (!g_app.windows && g_app.menubar_win) {
        destroy_window(g_app.menubar_win);
        g_app.menubar_win = NULL;
      }
      if (!g_app.windows && g_app.http_ready) {
        http_shutdown();
        g_app.http_ready = false;
      }
#ifndef BUILD_AS_GEM
      if (!g_app.windows) ui_request_quit();
#endif
      return true;

    default:
      return false;
  }
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  g_app.hinstance = hinstance;
  return browser_open_window(argc > 1 ? argv[1] : NULL);
}

void gem_shutdown(void) {
  browser_destroy_all_windows();

  if (g_app.menubar_win) {
    destroy_window(g_app.menubar_win);
    g_app.menubar_win = NULL;
  }
  if (g_app.accel) {
    free_accelerators(g_app.accel);
    g_app.accel = NULL;
  }
  if (g_app.http_ready) {
    http_shutdown();
    g_app.http_ready = false;
  }
  g_app.windows = NULL;
  g_app.active = NULL;
  g_app.hinstance = 0;
}

GEM_DEFINE("Browser", "0.2", gem_init, gem_shutdown, NULL)

#ifndef BUILD_AS_GEM
GEM_STANDALONE_MAIN("Browser", UI_INIT_DESKTOP, 640, 480,
                    g_app.menubar_win, g_app.accel)
#endif
