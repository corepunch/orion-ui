#ifndef __EXAMPLES_BROWSER_BROWSER_H__
#define __EXAMPLES_BROWSER_BROWSER_H__

#include <stdbool.h>
#include <stddef.h>

#include "../../ui.h"

#define ID_TB_BACK    1001
#define ID_TB_FWD     1002
#define ID_TB_HOME    1003
#define ID_TB_ADDR    1004
#define ID_BODY_VIEW  1005

#define ID_MENU_BROWSER_SETTINGS  2001

typedef struct {
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
} browser_state_t;

char *browser_html_to_plain_text(const char *html, size_t len);

bool browser_history_push(browser_state_t *st, const char *url);
void browser_history_free(browser_state_t *st);

void browser_set_body_text(window_t *win, const char *text);
void browser_sync_nav_buttons(window_t *win);
void browser_rebuild_toolbar(window_t *win);
void browser_update_layout(window_t *win);
void browser_navigate(window_t *win, const char *typed_url, bool push_history);

void browser_settings_init(browser_state_t *st);
bool browser_settings_load(browser_state_t *st);
bool browser_settings_save(const browser_state_t *st);
bool browser_show_settings_dialog(window_t *parent, browser_state_t *st);

#endif
