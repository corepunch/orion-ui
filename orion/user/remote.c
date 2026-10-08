#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <platform/platform.h>
#include "user.h"
#include "draw.h"
#include <orion/kernel/kernel.h>

// Remote-control queries (list_windows, get_rect, get_ctrl_rect, get_text, get_value,
// click_ctrl), answered on the main thread from the live window tree. ui_rc_poll() runs
// from get_message() just before it blocks, so every message loop — the main one and
// modal dialogs — services the RC server without wiring it up itself.

static window_t *rc_find_window(const char *title) {
  for (window_t *w = g_ui_runtime.windows; w; w = w->next)
    if (strcmp(w->title, title) == 0) return w;
  return NULL;
}

// Parses "<ctrl_id> <title>" and resolves both; writes the error response and returns NULL on failure.
static window_t *rc_find_ctrl(const char *args, char *resp, int resplen) {
  int ctrl_id; char title[512];
  if (sscanf(args, "%d %511[^\t\n]", &ctrl_id, title) != 2 || !rc_find_window(title)) {
    snprintf(resp, (size_t)resplen, "err no window\n");
    return NULL;
  }
  window_t *c = get_window_item(rc_find_window(title), (uint32_t)ctrl_id);
  if (!c) snprintf(resp, (size_t)resplen, "err no ctrl\n");
  return c;
}

// Screen-space centre of a control, following ancestor content views and drag lifts.
static ipoint16_t rc_screen_center(window_t *c) {
  ipoint16_t p = {c->frame.x + c->frame.w / 2, c->frame.y + c->frame.h / 2};
  for (window_t *cur = c; cur->parent; cur = cur->parent) {
    window_t *parent = cur->parent;
    int inset = window_screen_y(cur) - window_screen_y(parent) - cur->frame.y;
    p = window_content_to_client(parent, p);
    p.x += parent->frame.x;
    p.y += parent->frame.y + inset;
  }
  for (window_t *a = c; a; a = a->parent) {
    ipoint16_t d = window_lift_delta(a);
    p.x += d.x; p.y += d.y;
  }
  return p;
}

static bool rc_clickable(const window_t *c) {
  for (const window_t *a = c; a; a = a->parent)
    if (!window_has_state(a, WINDOW_STATE_VISIBLE) || window_has_state(a, WINDOW_STATE_DISABLED)) return false;
  return true;
}

static void ui_rc_query(const char *req, char *resp, int resplen) {
  if (strcmp(req, "list_windows") == 0) {
    char *p = resp;
    int left = resplen - 1;
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
      int n = snprintf(p, (size_t)left, "window %d %d %d %d %s\n",
                       (int)w->frame.x, (int)w->frame.y, (int)w->frame.w, (int)w->frame.h, w->title);
      if (n <= 0 || n >= left) break;
      p += n; left -= n;
    }
    snprintf(p, (size_t)(left + 1), "ok\n");
    return;
  }
  if (strcmp(req, "get_focus") == 0) {
    window_t *f = g_ui_runtime.focused;
    snprintf(resp, (size_t)resplen, "focused %s\nok\n", f ? f->title : "");
    return;
  }
  if (strncmp(req, "get_rect ", 9) == 0) {
    window_t *w = rc_find_window(req + 9);
    if (w) snprintf(resp, (size_t)resplen, "rect %d %d %d %d\nok\n", (int)w->frame.x, (int)w->frame.y, (int)w->frame.w, (int)w->frame.h);
    else snprintf(resp, (size_t)resplen, "err no window\n");
    return;
  }
  if (strncmp(req, "get_ctrl_rect ", 14) == 0) {
    window_t *c = rc_find_ctrl(req + 14, resp, resplen);
    if (c) snprintf(resp, (size_t)resplen, "rect %d %d %d %d\nok\n", window_screen_x(c), window_screen_y(c), (int)c->frame.w, (int)c->frame.h);
    return;
  }
  if (strncmp(req, "get_text ", 9) == 0) {
    window_t *c = rc_find_ctrl(req + 9, resp, resplen);
    if (c) snprintf(resp, (size_t)resplen, "text %s\nok\n", c->title);
    return;
  }
  if (strncmp(req, "get_value ", 10) == 0) {
    window_t *c = rc_find_ctrl(req + 10, resp, resplen);
    if (c) snprintf(resp, (size_t)resplen, "value %u\nok\n", (unsigned)c->value);
    return;
  }
  if (strncmp(req, "click_ctrl ", 11) == 0) {
    window_t *c = rc_find_ctrl(req + 11, resp, resplen);
    if (!c) return;
    if (!rc_clickable(c)) { snprintf(resp, (size_t)resplen, "err ctrl hidden or disabled\n"); return; }
    ipoint16_t p = rc_screen_center(c);
    axPostMessageW(NULL, kEventLeftButtonDown, MAKEDWORD(p.x, p.y), NULL);
    axPostMessageW(NULL, kEventLeftButtonUp,   MAKEDWORD(p.x, p.y), NULL);
    snprintf(resp, (size_t)resplen, "ok\n");
    return;
  }
  snprintf(resp, (size_t)resplen, "err unknown query\n");
}

void ui_rc_poll(void) {
  static bool attached;
  if (!attached) { axRCSetQueryHandler(ui_rc_query); attached = true; }
  char path[1024];
  if (axRCPopScreenshot(path, sizeof(path))) ui_request_screenshot(path, 90, false);
  axRCProcessQuery();
}
