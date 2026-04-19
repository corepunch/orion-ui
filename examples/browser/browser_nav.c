#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browser.h"

static void normalize_url(const char *in, char *out, size_t out_sz) {
  if (!out || out_sz == 0) return;
  out[0] = '\0';
  if (!in) return;

  while (*in && isspace((unsigned char)*in)) in++;
  size_t n = strlen(in);
  while (n > 0 && isspace((unsigned char)in[n - 1])) n--;
  if (n == 0) return;

  if (strstr(in, "://")) {
    snprintf(out, out_sz, "%.*s", (int)n, in);
  } else {
    snprintf(out, out_sz, "https://%.*s", (int)n, in);
  }
}

void browser_navigate(window_t *win, const char *typed_url, bool push_history) {
  browser_state_t *st = (browser_state_t *)win->userdata;
  if (!st) return;

  char url[sizeof(st->current_url)];
  normalize_url(typed_url, url, sizeof(url));
  if (!url[0]) return;

  if (st->request_id != HTTP_INVALID_REQUEST) {
    http_cancel(st->request_id);
    st->request_id = HTTP_INVALID_REQUEST;
  }

  strncpy(st->current_url, url, sizeof(st->current_url) - 1);
  st->current_url[sizeof(st->current_url) - 1] = '\0';

  if (push_history) browser_history_push(st, st->current_url);

  set_window_item_text(win, ID_TB_ADDR, "%s", st->current_url);

  st->loading = true;
  free(st->html_raw);
  st->html_raw = NULL;
  free(st->render_text);
  st->render_text = NULL;

  browser_set_body_text(win, "Loading...");

  st->request_id = http_request_async(win, st->current_url, NULL, NULL);
  if (st->request_id == HTTP_INVALID_REQUEST) {
    st->loading = false;
    browser_set_body_text(win, "Request failed to start.");
  }

  browser_sync_nav_buttons(win);
}
