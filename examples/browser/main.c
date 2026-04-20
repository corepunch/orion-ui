#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../ui.h"
#include "../../gem_magic.h"

#include "browser.h"

typedef struct {
  window_t *browser_win;
  window_t *menubar_win;
  bool http_ready;
} app_state_t;

static app_state_t g_app = {0};

static const menu_item_t kBrowserSettingsItems[] = {
  {"Preferences...", ID_MENU_BROWSER_SETTINGS},
};

static const menu_def_t kBrowserMenus[] = {
  {"Settings", kBrowserSettingsItems, 1},
};

static void browser_handle_menu_command(uint16_t id) {
  if (!g_app.browser_win) return;
  if (id == ID_MENU_BROWSER_SETTINGS) {
    send_message(g_app.browser_win, evCommand, MAKEDWORD(id, kMenuBarNotificationItemClick), NULL);
  }
}

static result_t browser_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evCommand && HIWORD(wparam) == kMenuBarNotificationItemClick) {
    browser_handle_menu_command(LOWORD(wparam));
    return true;
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
      win->userdata = st;

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

    case evResize:
      browser_rebuild_toolbar(win);
      browser_update_layout(win);
      return false;

    case evCommand:
      if (HIWORD(wparam) == kMenuBarNotificationItemClick &&
          LOWORD(wparam) == ID_MENU_BROWSER_SETTINGS) {
        if (st) browser_show_settings_window(win, st);
        return true;
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

      // Copy body to a null-terminated buffer for libxml2.
      st->html_raw = (char *)malloc(resp->body_len + 1);
      if (!st->html_raw) {
        browser_set_body_text(win, "Out of memory while storing response.");
        http_response_free(resp);
        return true;
      }
      memcpy(st->html_raw, resp->body, resp->body_len);
      st->html_raw[resp->body_len] = '\0';

      st->render_text = browser_html_to_plain_text(st->html_raw, resp->body_len);
      if (!st->render_text) st->render_text = strdup("(failed to render text)");

      char *page_title = browser_html_extract_title(st->html_raw, resp->body_len);
      if (page_title) {
        snprintf(win->title, sizeof(win->title), "%s - Browser", page_title);
        free(page_title);
      } else if (st->current_url[0]) {
        snprintf(win->title, sizeof(win->title), "%s - Browser", st->current_url);
      } else {
        snprintf(win->title, sizeof(win->title), "Browser (MVP)");
      }

      browser_set_body_text(win, st->render_text);
      invalidate_window(win);
      browser_sync_nav_buttons(win);

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
        free(st->html_raw);
        free(st->render_text);
        free(st);
        win->userdata = NULL;
      }
      if (g_app.http_ready) {
        http_shutdown();
        g_app.http_ready = false;
      }
      if (g_app.menubar_win) {
        destroy_window(g_app.menubar_win);
        g_app.menubar_win = NULL;
      }
      g_app.browser_win = NULL;
      return true;

    default:
      return false;
  }
}

static bool browser_open(hinstance_t hinstance) {
  if (!g_app.http_ready) {
    if (!http_init()) {
      return false;
    }
    g_app.http_ready = true;
  }

  rect_t wr = center_window_rect((rect_t){0, 0, 480, 320 + TITLEBAR_HEIGHT}, NULL);
  window_t *win = create_window(
    "Browser (MVP)",
    WINDOW_TOOLBAR,
    &wr,
    NULL,
    browser_proc,
    hinstance,
    NULL
  );
  if (!win) {
    if (g_app.http_ready) {
      http_shutdown();
      g_app.http_ready = false;
    }
    return false;
  }
  g_app.browser_win = win;

  g_app.menubar_win = set_app_menu(
    browser_menubar_proc,
    kBrowserMenus,
    (int)(sizeof(kBrowserMenus) / sizeof(kBrowserMenus[0])),
    browser_handle_menu_command,
    hinstance
  );

  show_window(win, true);
  return true;
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  (void)argc;
  (void)argv;
  return browser_open(hinstance);
}

GEM_DEFINE("Browser", "0.1", gem_init, NULL, NULL)

#ifndef BUILD_AS_GEM
int main(void) {
  if (!ui_init_graphics(UI_INIT_DESKTOP, "Browser", 640, 480)) {
    fprintf(stderr, "Failed to initialize graphics.\n");
    return 1;
  }

  if (!browser_open(0)) {
    ui_shutdown_graphics();
    return 1;
  }

  ui_event_t e;
  while (ui_is_running()) {
    while (get_message(&e)) dispatch_message(&e);
    repost_messages();
  }

  ui_shutdown_graphics();
  return 0;
}
#endif
