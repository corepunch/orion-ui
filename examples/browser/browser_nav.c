#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browser.h"

static void trim_copy(char *dst, size_t dst_sz, const char *src) {
  if (!dst || dst_sz == 0) return;
  dst[0] = '\0';
  if (!src) return;

  while (*src && isspace((unsigned char)*src)) src++;
  size_t n = strlen(src);
  while (n > 0 && isspace((unsigned char)src[n - 1])) n--;
  if (n == 0) return;

  snprintf(dst, dst_sz, "%.*s", (int)n, src);
}

static bool looks_like_local_path(const char *path) {
  if (!path || !path[0]) return false;
  if (path[0] == '/' || path[0] == '.') return true;
  if (isalpha((unsigned char)path[0]) && path[1] == ':') return true;
  return axPathExists(path) ? true : false;
}

static void browser_set_error_text(window_t *win, const char *prefix,
                                   const char *detail) {
  browser_state_t *st = (browser_state_t *)win->userdata;
  char text[2048];

  if (!st) return;

  free(st->html_raw);
  st->html_raw = NULL;
  free(st->render_text);
  st->render_text = NULL;

  if (detail && detail[0])
    snprintf(text, sizeof(text), "%s: %s", prefix, detail);
  else
    snprintf(text, sizeof(text), "%s", prefix);

  st->render_text = strdup(text);
  browser_set_body_text(win, st->render_text ? st->render_text : text);
  snprintf(win->title, sizeof(win->title), "%s - Browser", prefix);
  invalidate_window(win);
  browser_sync_nav_buttons(win);
}

bool browser_is_file_url(const char *url) {
  return url && strncmp(url, "file://", 7) == 0;
}

bool browser_url_to_local_path(const char *url, char *path, size_t path_sz) {
  if (!path || path_sz == 0) return false;
  path[0] = '\0';
  if (!url || !browser_is_file_url(url)) return false;

  snprintf(path, path_sz, "%s", url + 7);
  return path[0] != '\0';
}

bool browser_local_path_to_url(const char *path, char *url, size_t url_sz) {
  if (!url || url_sz == 0) return false;
  url[0] = '\0';
  if (!path || !path[0]) return false;

  if (browser_is_file_url(path)) {
    snprintf(url, url_sz, "%s", path);
    return true;
  }

  snprintf(url, url_sz, "file://%s", path);
  return true;
}

static void normalize_url(const char *in, char *out, size_t out_sz) {
  char trimmed[1024];

  if (!out || out_sz == 0) return;
  out[0] = '\0';
  if (!in) return;

  trim_copy(trimmed, sizeof(trimmed), in);
  if (!trimmed[0]) return;

  if (strstr(trimmed, "://")) {
    snprintf(out, out_sz, "%s", trimmed);
  } else if (looks_like_local_path(trimmed)) {
    browser_local_path_to_url(trimmed, out, out_sz);
  } else {
    snprintf(out, out_sz, "https://%s", trimmed);
  }
}

void browser_apply_html(window_t *win, const char *html, size_t len,
                        const char *fallback_title) {
  browser_state_t *st = (browser_state_t *)win->userdata;
  char *page_title;

  if (!st || !html) return;

  free(st->html_raw);
  st->html_raw = NULL;
  free(st->render_text);
  st->render_text = NULL;

  st->html_raw = (char *)malloc(len + 1);
  if (!st->html_raw) {
    browser_set_error_text(win, "Out of memory while storing page", NULL);
    return;
  }

  memcpy(st->html_raw, html, len);
  st->html_raw[len] = '\0';

  st->render_text = browser_html_to_plain_text(st->html_raw, len);
  if (!st->render_text) st->render_text = strdup("(failed to render text)");

  page_title = browser_html_extract_title(st->html_raw, len);
  if (page_title) {
    snprintf(win->title, sizeof(win->title), "%s - Browser", page_title);
    free(page_title);
  } else if (fallback_title && fallback_title[0]) {
    snprintf(win->title, sizeof(win->title), "%s - Browser", fallback_title);
  } else if (st->current_url[0]) {
    snprintf(win->title, sizeof(win->title), "%s - Browser", st->current_url);
  } else {
    snprintf(win->title, sizeof(win->title), "Browser (MVP)");
  }

  browser_set_body_text(win, st->render_text ? st->render_text : "(failed to render text)");
  invalidate_window(win);
  browser_sync_nav_buttons(win);
}

bool browser_load_local_file(window_t *win, const char *url_or_path,
                             bool push_history) {
  browser_state_t *st = (browser_state_t *)win->userdata;
  char path[sizeof(st->current_url)];
  char url[sizeof(st->current_url)];
  char *html;
  FILE *fp;
  long sz;
  size_t got;

  if (!st || !url_or_path || !url_or_path[0]) return false;

  if (browser_is_file_url(url_or_path)) {
    if (!browser_url_to_local_path(url_or_path, path, sizeof(path)))
      return false;
    snprintf(url, sizeof(url), "%s", url_or_path);
  } else {
    snprintf(path, sizeof(path), "%s", url_or_path);
    if (!browser_local_path_to_url(path, url, sizeof(url))) return false;
  }

  fp = fopen(path, "rb");
  if (!fp) {
    browser_set_error_text(win, "Failed to open file", strerror(errno));
    return false;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    browser_set_error_text(win, "Failed to seek file", strerror(errno));
    return false;
  }
  sz = ftell(fp);
  if (sz < 0 || fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    browser_set_error_text(win, "Failed to read file", strerror(errno));
    return false;
  }

  html = (char *)malloc((size_t)sz + 1);
  if (!html) {
    fclose(fp);
    browser_set_error_text(win, "Out of memory while loading file", NULL);
    return false;
  }

  got = fread(html, 1, (size_t)sz, fp);
  fclose(fp);
  if (got != (size_t)sz) {
    free(html);
    browser_set_error_text(win, "Failed to read file", strerror(errno));
    return false;
  }
  html[got] = '\0';

  snprintf(st->current_url, sizeof(st->current_url), "%s", url);
  if (push_history) browser_history_push(st, st->current_url);
  set_window_item_text(win, ID_TB_ADDR, "%s", st->current_url);
  st->loading = false;
  st->request_id = HTTP_INVALID_REQUEST;

  browser_apply_html(win, html, got, path);
  free(html);
  return true;
}

bool browser_save_html_file(window_t *win, const char *path) {
  browser_state_t *st = (browser_state_t *)win->userdata;
  FILE *fp;
  size_t len;

  if (!st || !path || !path[0] || !st->html_raw) return false;

  fp = fopen(path, "wb");
  if (!fp) return false;

  len = strlen(st->html_raw);
  if (len > 0 && fwrite(st->html_raw, 1, len, fp) != len) {
    fclose(fp);
    return false;
  }

  fclose(fp);
  return true;
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

  if (browser_is_file_url(st->current_url)) {
    if (st->request_id != HTTP_INVALID_REQUEST) {
      http_cancel(st->request_id);
      st->request_id = HTTP_INVALID_REQUEST;
    }
    browser_load_local_file(win, st->current_url, false);
    return;
  }

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
