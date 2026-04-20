#ifndef __EXAMPLES_BROWSER_BROWSER_H__
#define __EXAMPLES_BROWSER_BROWSER_H__

#include <stdbool.h>
#include <stddef.h>

#include "../../ui.h"

#define ID_TB_BACK    1001
#define ID_TB_FWD     1002
#define ID_TB_HOME    1003
#define ID_TB_REFRESH 1006
#define ID_TB_ADDR    1004
#define ID_BODY_VIEW  1005

#define ID_MENU_FILE_NEW           2001
#define ID_MENU_FILE_OPEN          2002
#define ID_MENU_FILE_SAVE          2003
#define ID_MENU_FILE_QUIT          2004
#define ID_MENU_BROWSER_SETTINGS   2005
#define ID_MENU_HELP_ABOUT         2006

typedef struct browser_state_s {
  char current_url[1024];
  char home_url[1024];
  bool loading;
  http_request_id_t request_id;
  char *html_raw;
  char *render_text;
  char **history;
  int history_count;
  int history_cap;
  int history_index;
  window_t *win;
  window_t *settings_win;
  struct browser_state_s *next;
  struct browser_state_s *prev;
} browser_state_t;

char *browser_html_to_plain_text(const char *html, size_t len);
char *browser_html_extract_title(const char *html, size_t len);

bool browser_history_push(browser_state_t *st, const char *url);
void browser_history_free(browser_state_t *st);

void browser_set_body_text(window_t *win, const char *text);
void browser_sync_nav_buttons(window_t *win);
void browser_rebuild_toolbar(window_t *win);
void browser_update_layout(window_t *win);
void browser_apply_html(window_t *win, const char *html, size_t len,
                        const char *fallback_title);
bool browser_is_file_url(const char *url);
bool browser_url_to_local_path(const char *url, char *path, size_t path_sz);
bool browser_local_path_to_url(const char *path, char *url, size_t url_sz);
bool browser_load_local_file(window_t *win, const char *url_or_path,
                             bool push_history);
bool browser_save_html_file(window_t *win, const char *path);
void browser_navigate(window_t *win, const char *typed_url, bool push_history);

void browser_settings_init(browser_state_t *st);
bool browser_settings_load(browser_state_t *st);
bool browser_settings_save(const browser_state_t *st);
bool browser_show_settings_window(window_t *parent, browser_state_t *st);
bool browser_pick_open_path(window_t *parent, char *out_path, size_t out_sz);
bool browser_pick_save_path(window_t *parent, char *out_path, size_t out_sz);
void browser_show_about_dialog(window_t *parent);

#endif
