// The MDI client owns document windows; each document keeps its own render
// surface and command root, while clipping and lifetime follow the client.
#include "user.h"
#include "dock.h"
#include "rect.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct { window_t *active; irect16_t screen; unsigned cascade; } mdi_client_t;

irect16_t mdi_client_screen_rect(window_t *client) {
  return rect_offset(get_client_rect(client), window_screen_x(client), window_screen_y(client));
}

window_t *mdi_active_document(window_t *client) {
  mdi_client_t *state = client && client->role == WINDOW_ROLE_MDICLIENT ? client->userdata : NULL;
  return state && is_window(state->active) ? state->active : NULL;
}

void mdi_activate_document(window_t *document) {
  if (!document || !document->mdi_client) return;
  mdi_client_t *state = document->mdi_client->userdata;
  if (!state || state->active == document) return;
  window_t *previous = state->active;
  state->active = document;
  if (is_window(previous)) { send_message(previous, evMDIActivate, false, document); invalidate_window(previous); }
  send_message(document, evMDIActivate, true, previous);
  invalidate_window(document);
}

void mdi_cycle_document(window_t *client, int direction) {
  window_t *documents[256]; int count = 0, current = 0;
  window_t *active = mdi_active_document(client);
  for (window_t *win = g_ui_runtime.windows; win && count < 256; win = win->next)
    if (win->mdi_client == client && window_is_visible(win)) {
      if (win == active) current = count;
      documents[count++] = win;
    }
  if (!count) return;
  for (int i = 1; i < count; i++) {
    window_t *item = documents[i]; int j = i;
    while (j > 0 && documents[j - 1]->id > item->id) { documents[j] = documents[j - 1]; j--; }
    documents[j] = item;
  }
  for (int i = 0; i < count; i++) if (documents[i] == active) current = i;
  window_t *next = documents[(current + (direction < 0 ? count - 1 : 1)) % count];
  move_to_top(next); set_focus(next);
}

// A dragged document stays clipped by the client, but its caption may not
// leave it: a caption hidden under the frame's toolbar could not be grabbed again.
ipoint16_t mdi_constrain_document_origin(window_t *document, ipoint16_t origin) {
  if (!document || !document->mdi_client) return origin;
  irect16_t area = mdi_client_screen_rect(document->mdi_client);
  int caption = MAX(1, titlebar_height(document));
  origin.x = MAX(area.x + MDI_MIN_VISIBLE_CAPTION - document->frame.w, MIN(origin.x, area.x + area.w - MDI_MIN_VISIBLE_CAPTION));
  origin.y = MAX(area.y, MIN(origin.y, area.y + area.h - caption));
  return origin;
}

void mdi_forget_document(window_t *document) {
  if (!document || !document->mdi_client) return;
  mdi_client_t *state = document->mdi_client->userdata;
  if (state && state->active == document) {
    state->active = NULL;
    for (window_t *win = g_ui_runtime.windows; win; win = win->next)
      if (win != document && win->mdi_client == document->mdi_client && window_is_visible(win)) state->active = win;
  }
}

static void mdi_layout(window_t *client) {
  mdi_client_t *state = client->userdata;
  if (!state || !client->parent) return;
  client->frame = dock_content_rect(client->parent);
  irect16_t screen = mdi_client_screen_rect(client), before = state->screen;
  state->screen = screen;
  for (window_t *doc = g_ui_runtime.windows; doc; doc = doc->next) {
    if (doc->mdi_client != client) continue;
    if (window_is_maximized(doc)) update_maximized_window(doc);
    else if (before.w && before.h) move_window(doc, doc->frame.x + screen.x - before.x, doc->frame.y + screen.y - before.y);
  }
  request_composite();
}

void mdi_layout_clients(window_t *frame) {
  for (window_t *win = frame ? frame->children : NULL; win; win = win->next)
    if (win->role == WINDOW_ROLE_MDICLIENT) mdi_layout(win);
}

bool mdi_attach_document(window_t *document, window_t *client) {
  if (!is_window(document) || document->parent || document->owner ||
      !is_window(client) || client->role != WINDOW_ROLE_MDICLIENT || !client->userdata || get_root_window(client) == document) {
    fprintf(stderr, "[mdi] invalid document parent document=%p client=%p\n", (void *)document, (void *)client);
    fflush(stderr); return false;
  }
  mdi_layout(client);
  mdi_client_t *state = client->userdata;
  irect16_t area = mdi_client_screen_rect(client);
  document->mdi_client = client;
  document->hinstance = client->hinstance;
  document->flags |= WINDOW_NOTRAYBUTTON;
  int offset = (int)(state->cascade++ % 5) * DEFAULT_WINDOW_CASCADE_X;
  irect16_t frame = rect_center(area, MIN(document->frame.w, area.w), MIN(document->frame.h, area.h));
  move_window(document, MIN(frame.x + offset, area.x + area.w - frame.w), MIN(frame.y + offset, area.y + area.h - frame.h));
  resize_window(document, frame.w, frame.h);
  mdi_activate_document(document);
  window_order_owned();
  return true;
}

bool mdi_close_all(window_t *client) {
  for (;;) {
    window_t *document = g_ui_runtime.windows;
    while (document && document->mdi_client != client) document = document->next;
    if (!document) return true;
    bool handled = send_message(document, evClose, 0, NULL);
    if (!is_window(document)) continue;
    if (handled) return false;
    destroy_window(document);
  }
}

void mdi_arrange_documents(window_t *client, bool tile) {
  int count = 0, index = 0;
  for (window_t *w = g_ui_runtime.windows; w; w = w->next) if (w->mdi_client == client) count++;
  if (!count) return;
  irect16_t area = mdi_client_screen_rect(client);
  for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
    if (w->mdi_client != client) continue;
    if (window_is_maximized(w)) restore_window(w);
    int offset = tile ? 0 : (index % 5) * DEFAULT_WINDOW_CASCADE_Y;
    int width = tile ? area.w / count : MAX(100, area.w - 4 * DEFAULT_WINDOW_CASCADE_Y);
    move_window(w, area.x + (tile ? index * width : offset), area.y + offset);
    resize_window(w, width, MAX(100, area.h - (tile ? 0 : 4 * DEFAULT_WINDOW_CASCADE_Y)));
    index++;
  }
}

result_t win_mdi_client(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam;
  switch (msg) {
    case evCreate:
      win->role = WINDOW_ROLE_MDICLIENT;
      return allocate_window_data(win, sizeof(mdi_client_t)) != NULL;
    case evPaint: return true;
    case evArrange:
    case evResize: mdi_layout(win); return true;
    case evHitTest:
      if (lparam) *(window_t **)lparam = NULL;
      return true;
    case evDestroy:
      free(win->userdata); win->userdata = NULL;
      return true;
  }
  return false;
}
