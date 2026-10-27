// The MDI client owns document windows; each document keeps its own render
// surface and command root, while clipping and lifetime follow the client.
// In maximized mode a tab bar switches between the frame itself (the home tab)
// and the open documents, and closes them.
#include "user.h"
#include "dock.h"
#include "rect.h"
#include "toolbar.h"
#include <orion/commctl/commctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MDI_MAX_DOCUMENTS 256
enum { MDI_ACTION_CLOSE = 1 };

typedef struct {
  window_t *active;
  window_t *host, *tabs; // tab bar host and its TabView; NULL until first shown
  window_t *documents[MDI_MAX_DOCUMENTS]; // in the order they were opened
  int count;
  bool home, switching; // home: the frame's tab is selected and documents are hidden
  irect16_t screen;
  unsigned cascade;
} mdi_client_t;

static const toolbar_item_t mdi_tab_actions[] = {
  {.type = TOOLBAR_ITEM_BUTTON, .ident = MDI_ACTION_CLOSE, .icon = "pane-close", .tooltip = "Close active tab"},
};

static bool mdi_tabs_in_client(window_t *client, const mdi_client_t *state) {
  return state && is_window(state->host) && state->host->parent == client &&
         window_has_state(state->host, WINDOW_STATE_VISIBLE);
}

static irect16_t mdi_client_bounds(window_t *client) {
  return rect_offset(get_client_rect(client), window_screen_x(client), window_screen_y(client));
}

// The area documents occupy: the client below its tab bar.
irect16_t mdi_client_screen_rect(window_t *client) {
  mdi_client_t *state = client->userdata;
  irect16_t area = mdi_client_bounds(client);
  return mdi_tabs_in_client(client, state) ? rect_trim_top(area, state->host->frame.h) : area;
}

// Documents in the order they were opened: the order of their tabs and of Next Window.
static int mdi_documents(window_t *client, window_t *exclude, bool visible_only, window_t **documents) {
  mdi_client_t *state = client->userdata;
  int count = 0;
  for (int i = 0; state && i < state->count; i++) {
    window_t *win = state->documents[i];
    if (win != exclude && (!visible_only || window_is_visible(win))) documents[count++] = win;
  }
  return count;
}

// evClose returns true when the document cancels its closing (unsaved changes).
static bool mdi_close_document(window_t *document) {
  bool handled = send_message(document, evClose, 0, NULL);
  if (!is_window(document)) return true;
  if (handled) return false;
  destroy_window(document);
  return true;
}

// The home tab hides the documents to uncover the frame; any document tab shows them again.
static void mdi_set_home(window_t *client, bool home) {
  mdi_client_t *state = client->userdata;
  if (!state || state->home == home) return;
  state->home = home;
  window_t *documents[MDI_MAX_DOCUMENTS];
  int count = mdi_documents(client, NULL, false, documents);
  state->switching = true;
  for (int i = 0; i < count; i++) show_window(documents[i], !home);
  state->switching = false;
  if (home) set_focus(get_root_window(client));
  else if (is_window(state->active)) move_to_top(state->active);
}

static void mdi_sync_tabs(window_t *client, window_t *exclude);

// Holds the document TabView wherever the tab bar shows and routes its
// notifications to the MDI client.
static result_t win_mdi_tab_host(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  window_t *client = win->userdata, *tabs = win->children;
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      tabs = create_window("", WINDOW_NOTITLE | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP, MAKERECT(0, 0, 1, 1),
                           win, "TabView", win->hinstance, NULL);
      return tabs && send_message(tabs, tcSetStyle, TAB_STYLE_PANE, NULL) &&
             send_message(tabs, tcSetToolbar, ARRAY_LEN(mdi_tab_actions), (void *)mdi_tab_actions);
    case evResize:
      if (tabs) { tabs->frame = get_client_rect(win); send_message(tabs, evResize, 0, NULL); }
      return true;
    case evPaint: return false;
    case evCommand:
      if (!is_window(client) || !tabs) return false;
      if (lparam == tabs && HIWORD(wparam) == tcnSelChange) {
        int index = (int)send_message(tabs, tcGetSelection, 0, NULL);
        window_t *target = (window_t *)send_message(tabs, tcGetItemData, (uint32_t)index, NULL);
        mdi_set_home(client, target == get_root_window(client));
        if (is_window(target) && target->mdi_client == client) { move_to_top(target); set_focus(target); }
        mdi_sync_tabs(client, NULL);
        return true;
      }
      if (HIWORD(wparam) == tcnAction && lparam && ((tab_action_t *)lparam)->pane == tabs) {
        mdi_client_t *state = client->userdata;
        window_t *document = state->home ? NULL : mdi_active_document(client);
        if (LOWORD(((tab_action_t *)lparam)->command) == MDI_ACTION_CLOSE && document) mdi_close_document(document);
        return true;
      }
      return false;
    case evDestroy:
      win->userdata = NULL;
      return true;
  }
  return false;
}

static void mdi_destroy_tab_host(mdi_client_t *state) {
  window_t *host = state->host;
  state->host = state->tabs = NULL;
  if (!is_window(host)) return;
  show_window(host, false); // a docked band gives its space back to the workspace first
  destroy_window(host);
}

// The tab bar lives in the application chrome, under the menu bar, while the
// frame is maximized (the whole application shows its documents as tabs, as
// 1C:Enterprise does); otherwise it sits atop the client.
static window_t *mdi_tab_host_parent(window_t *client) {
  window_t *chrome = get_application_workspace_owner(client->hinstance);
  return chrome && chrome->dock_host && window_is_maximized(get_root_window(client)) ? chrome : client;
}

static bool mdi_create_tab_host(window_t *client, mdi_client_t *state, window_t *parent) {
  state->host = create_window("", WINDOW_NOTITLE | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP, MAKERECT(0, 0, 1, 1),
                              parent, win_mdi_tab_host, client->hinstance, client);
  if (!state->host) {
    fprintf(stderr, "[mdi] tab bar creation failed client=%u parent=%u\n", client->id, parent->id);
    fflush(stderr); return false;
  }
  state->tabs = state->host->children;
  show_window(state->host, false);
  if (parent != client) return dock_window(state->host, DOCK_TOP, DOCK_EDGE(DOCK_TOP), DOCK_NOFLOAT, TAB_CONTROL_HEIGHT, 0);
  state->host->frame = rect_split_top(get_client_rect(client), TAB_CONTROL_HEIGHT);
  send_message(state->host, evResize, 0, NULL);
  return true;
}

static void mdi_sync_tabs(window_t *client, window_t *exclude) {
  mdi_client_t *state = client && client->role == WINDOW_ROLE_MDICLIENT ? client->userdata : NULL;
  if (!state) return;
  window_t *documents[MDI_MAX_DOCUMENTS], *active = state->active, *frame = get_root_window(client);
  window_t *parent = mdi_tab_host_parent(client);
  tab_item_t items[MDI_MAX_DOCUMENTS + 1] = {{frame->title, (uintptr_t)frame}};
  int count = mdi_documents(client, exclude, false, documents), selected = 0;
  bool shown = count > 0 && (state->home || (is_window(active) && active != exclude && window_is_maximized(active)));
  if (!shown && state->home) mdi_set_home(client, false);
  if (state->host && (!is_window(state->host) || state->host->parent != parent)) mdi_destroy_tab_host(state);
  if (!state->host && (!shown || !mdi_create_tab_host(client, state, parent))) return;
  for (int i = 0; i < count; i++) {
    items[i + 1] = (tab_item_t){documents[i]->title, (uintptr_t)documents[i]};
    if (documents[i] == active && !state->home) selected = i + 1;
  }
  send_message(state->tabs, tcSetItems, (uint32_t)count + 1, items);
  send_message(state->tabs, tcSetSelection, (uint32_t)selected, NULL);
  if (shown == window_has_state(state->host, WINDOW_STATE_VISIBLE)) return;
  show_window(state->host, shown);
  if (parent == client)
    for (int i = 0; i < count; i++) if (window_is_maximized(documents[i])) update_maximized_window(documents[i]);
  invalidate_window(client);
  request_composite();
}

// A parent routes the pointer to its MDI client first where the client's tab
// bar shows, since the client lies over the frame's pages.
window_t *mdi_client_hit_test(window_t *parent, ipoint16_t point) {
  for (window_t *c = parent ? parent->children : NULL; c; c = c->next) {
    if (c->role != WINDOW_ROLE_MDICLIENT || !window_has_state(c, WINDOW_STATE_VISIBLE)) continue;
    mdi_client_t *state = c->userdata;
    if (mdi_tabs_in_client(c, state) && rect_contains_point(rect_offset(state->host->frame, c->frame.x, c->frame.y), point)) return c;
  }
  return NULL;
}

// A document or frame was renamed: tabs show both titles.
void mdi_window_changed(window_t *win) {
  if (!win) return;
  if (win->mdi_client) mdi_sync_tabs(win->mdi_client, NULL);
  for (window_t *client = win->children; client; client = client->next)
    if (client->role == WINDOW_ROLE_MDICLIENT) mdi_sync_tabs(client, NULL);
}

// A window was maximized or restored. A maximized frame keeps its MDI
// clients in maximized mode: every document maximized, as tabs.
void mdi_window_placement_changed(window_t *win) {
  if (!win) return;
  if (win->mdi_client) mdi_sync_tabs(win->mdi_client, NULL);
  bool maximized = window_is_maximized(win);
  for (window_t *client = win->children; client; client = client->next) {
    if (client->role != WINDOW_ROLE_MDICLIENT || !client->userdata) continue;
    mdi_client_t *state = client->userdata;
    window_t *documents[MDI_MAX_DOCUMENTS];
    int count = mdi_documents(client, NULL, false, documents);
    for (int i = 0; i < count; i++) {
      if (maximized && !window_is_maximized(documents[i]) && (documents[i]->flags & WINDOW_MAXIMIZEBOX)) maximize_window(documents[i]);
      else if (!maximized && window_is_maximized(documents[i])) restore_window(documents[i]);
    }
    if (is_window(state->active) && !state->home) move_to_top(state->active);
    mdi_sync_tabs(client, NULL);
  }
}

window_t *mdi_active_document(window_t *client) {
  mdi_client_t *state = client && client->role == WINDOW_ROLE_MDICLIENT ? client->userdata : NULL;
  return state && is_window(state->active) ? state->active : NULL;
}

void mdi_activate_document(window_t *document) {
  if (!document || !document->mdi_client) return;
  mdi_client_t *state = document->mdi_client->userdata;
  if (!state || state->switching) return;
  bool was_home = state->home;
  mdi_set_home(document->mdi_client, false);
  if (state->active == document) {
    if (was_home) mdi_sync_tabs(document->mdi_client, NULL);
    return;
  }
  window_t *previous = state->active;
  state->active = document;
  if (is_window(previous)) { send_message(previous, evMDIActivate, false, document); invalidate_window(previous); }
  send_message(document, evMDIActivate, true, previous);
  invalidate_window(document);
  // As in Windows MDI, activating another document keeps the client in maximized mode.
  if (is_window(previous) && window_is_maximized(previous) && !window_is_maximized(document) &&
      (document->flags & WINDOW_MAXIMIZEBOX)) maximize_window(document);
  mdi_sync_tabs(document->mdi_client, NULL);
}

void mdi_cycle_document(window_t *client, int direction) {
  window_t *documents[MDI_MAX_DOCUMENTS], *active = mdi_active_document(client);
  int count = mdi_documents(client, NULL, false, documents), current = 0;
  if (!count) return;
  mdi_set_home(client, false);
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
  for (int i = 0; state && i < state->count; i++)
    if (state->documents[i] == document) {
      memmove(state->documents + i, state->documents + i + 1, (size_t)(--state->count - i) * sizeof(window_t *));
      break;
    }
  if (state && state->active == document) state->active = state->count ? state->documents[state->count - 1] : NULL;
  mdi_sync_tabs(document->mdi_client, document);
}

static void mdi_layout(window_t *client) {
  mdi_client_t *state = client->userdata;
  if (!state || !client->parent) return;
  client->frame = dock_content_rect(client->parent);
  if (is_window(state->host) && state->host->parent == client) {
    state->host->frame = rect_split_top(get_client_rect(client), TAB_CONTROL_HEIGHT);
    send_message(state->host, evResize, 0, NULL);
  }
  irect16_t screen = mdi_client_bounds(client), before = state->screen;
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
  mdi_client_t *state = client->userdata;
  if (state->count == MDI_MAX_DOCUMENTS) {
    fprintf(stderr, "[mdi] document rejected client=%u: %d documents open\n", client->id, state->count);
    fflush(stderr); return false;
  }
  mdi_set_home(client, false);
  mdi_layout(client);
  state->documents[state->count++] = document;
  irect16_t area = mdi_client_screen_rect(client);
  document->mdi_client = client;
  document->hinstance = client->hinstance;
  document->flags |= WINDOW_NOTRAYBUTTON;
  int offset = (int)(state->cascade++ % 5) * DEFAULT_WINDOW_CASCADE_X;
  irect16_t frame = rect_center(area, MIN(document->frame.w, area.w), MIN(document->frame.h, area.h));
  move_window(document, MIN(frame.x + offset, area.x + area.w - frame.w), MIN(frame.y + offset, area.y + area.h - frame.h));
  resize_window(document, frame.w, frame.h);
  mdi_activate_document(document);
  if (window_is_maximized(get_root_window(client)) && !window_is_maximized(document) && (document->flags & WINDOW_MAXIMIZEBOX))
    maximize_window(document);
  window_order_owned();
  return true;
}

bool mdi_close_all(window_t *client) {
  for (;;) {
    window_t *document = g_ui_runtime.windows;
    while (document && document->mdi_client != client) document = document->next;
    if (!document) return true;
    if (!mdi_close_document(document)) return false;
  }
}

void mdi_arrange_documents(window_t *client, bool tile) {
  int count = 0, index = 0;
  for (window_t *w = g_ui_runtime.windows; w; w = w->next) if (w->mdi_client == client) count++;
  if (!count) return;
  for (window_t *w = g_ui_runtime.windows; w; w = w->next)
    if (w->mdi_client == client && window_is_maximized(w)) restore_window(w);
  irect16_t area = mdi_client_screen_rect(client);
  for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
    if (w->mdi_client != client) continue;
    int offset = tile ? 0 : (index % 5) * DEFAULT_WINDOW_CASCADE_Y;
    int width = tile ? area.w / count : MAX(100, area.w - 4 * DEFAULT_WINDOW_CASCADE_Y);
    move_window(w, area.x + (tile ? index * width : offset), area.y + offset);
    resize_window(w, width, MAX(100, area.h - (tile ? 0 : 4 * DEFAULT_WINDOW_CASCADE_Y)));
    index++;
  }
}

// The client itself is transparent: only its tab bar paints and takes the pointer.
result_t win_mdi_client(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  mdi_client_t *state = win->userdata;
  switch (msg) {
    case evCreate:
      win->role = WINDOW_ROLE_MDICLIENT;
      return allocate_window_data(win, sizeof(mdi_client_t)) != NULL;
    case evPaint: return false;
    case evArrange:
    case evResize: mdi_layout(win); return true;
    case evHitTest: {
      ipoint16_t point = {(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
      if (mdi_tabs_in_client(win, state) && rect_contains_point(state->host->frame, point)) return false;
      if (lparam) *(window_t **)lparam = NULL;
      return true;
    }
    case evDestroy:
      if (state) mdi_destroy_tab_host(state);
      free(win->userdata); win->userdata = NULL;
      return true;
  }
  return false;
}
