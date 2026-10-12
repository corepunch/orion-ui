#include "page_host.h"
#include "accel.h"
#include "dock.h"
#include "messages.h"
#include "toolbar.h"
#include <orion/commctl/menubar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t command, focus_id;
  window_t *page, *focus;
} activity_entry_t;

typedef struct page_host_s {
  window_t *bar;
  int count;
  bool changing, routing;
  activity_entry_t entries[];
} page_host_t;

static page_host_t *activity_state(window_t *host) {
  return host && host->pages ? host->pages->page_host : NULL;
}

static bool activity_error(window_t *host, uint32_t command, const char *reason) {
  fprintf(stderr, "[page_host] rejected win=%u command=%u reason=%s\n",
          host ? host->id : 0, command, reason);
  fflush(stderr);
  return false;
}

static activity_entry_t *activity_entry(window_t *host, uint32_t command) {
  page_host_t *st = activity_state(host);
  for (int i = 0; st && i < st->count; i++)
    if (st->entries[i].command == command) return &st->entries[i];
  return NULL;
}

static bool activity_contains(window_t *page, window_t *win) {
  for (; win; win = win->parent) if (win == page) return true;
  return false;
}

static bool activity_focusable(window_t *page, window_t *win) {
  if (!win || !is_window(win) || (win->flags & WINDOW_NOTABSTOP)) return false;
  for (window_t *w = win; w; w = w->parent) {
    if (!window_has_state(w, WINDOW_STATE_VISIBLE) || window_has_state(w, WINDOW_STATE_DISABLED)) return false;
    if (w == page) return true;
  }
  return false;
}

static window_t *activity_first_focus(window_t *page, window_t *win) {
  if (!window_has_state(win, WINDOW_STATE_VISIBLE) || window_has_state(win, WINDOW_STATE_DISABLED)) return NULL;
  for (window_t *c = win->children; c; c = c->next) {
    window_t *focus = activity_first_focus(page, c);
    if (focus) return focus;
  }
  return activity_focusable(page, win) ? win : NULL;
}

window_t *page_host_page(window_t *host, uint32_t command) {
  activity_entry_t *entry = activity_entry(host, command);
  if (!entry || !entry->page) { activity_error(host, command, "page_unavailable"); return NULL; }
  return entry->page;
}

uint32_t page_host_selection(window_t *host) {
  page_host_t *st = activity_state(host);
  for (int i = 0; st && i < st->count; i++)
    if (st->entries[i].page && st->entries[i].page == window_active_page(host)) return st->entries[i].command;
  return 0;
}

bool page_host_select(window_t *host, uint32_t command) {
  page_host_t *st = activity_state(host);
  activity_entry_t *entry = activity_entry(host, command);
  if (!st || !entry || !entry->page || !st->bar) return activity_error(host, command, "page_or_bar_unavailable");
  if (st->changing) return activity_error(host, command, "selection_in_progress");
  page_host_remember_focus(g_ui_runtime.focused);
  st->changing = true;
  bool changed = window_active_page(host) != entry->page;
  if (changed) {
    for (int i = 0; i < st->count; i++) {
      window_t *page = st->entries[i].page;
      if (page && page != entry->page) show_window(page, false);
    }
    show_window(entry->page, true);
    if (!set_host_page(host, entry->page)) { st->changing = false; return false; }
  }
  for (int i = 0; i < st->count; i++)
    send_message(st->bar, tbCheckButton, st->entries[i].command, (void *)(intptr_t)(st->entries[i].command == command));
  window_t *focus = entry->focus;
  if (!activity_focusable(entry->page, focus)) focus = entry->focus_id ? get_window_item(entry->page, entry->focus_id) : NULL;
  if (!activity_focusable(entry->page, focus)) focus = activity_first_focus(entry->page, entry->page);
  set_focus(focus ? focus : entry->page);
  st->changing = false;
  if (changed) send_message(host, evPageChanged, command, entry->page);
  return true;
}

bool page_host_command(window_t *host, uint32_t wparam, void *lparam) {
  uint16_t code = HIWORD(wparam), command = LOWORD(wparam);
  if (code != 0 && code != btnClicked && code != kAcceleratorNotification && code != kMenuBarNotificationItemClick) return false;
  if (activity_entry(host, command)) {
    page_host_select(host, command);
    return true;
  }
  window_t *page = window_active_page(host);
  if (!page || !page->pages || !command) return false;
  page_host_t *st = activity_state(host);
  if (!st || st->routing) return false;
  st->routing = true;
  bool handled = send_message(page, evCommand, wparam, lparam);
  if (is_window(host) && activity_state(host) == st) st->routing = false;
  return handled;
}

void page_host_remember_focus(window_t *win) {
  for (window_t *host = win; host; host = host->parent) {
    page_host_t *st = activity_state(host);
    if (!st || st->changing) continue;
    for (int i = 0; i < st->count; i++)
      if (activity_contains(st->entries[i].page, win)) st->entries[i].focus = win;
  }
}

void page_host_forget_window(window_t *win) {
  for (window_t *host = win->parent; host; host = host->parent) {
    page_host_t *st = activity_state(host);
    if (!st) continue;
    if (st->bar == win) st->bar = NULL;
    for (int i = 0; i < st->count; i++) {
      if (st->entries[i].page == win) {
        st->entries[i].page = NULL;
        if (st->bar) send_message(st->bar, tbCheckButton, st->entries[i].command, NULL);
      }
      if (st->entries[i].focus == win) st->entries[i].focus = NULL;
    }
  }
  if (win->pages) { free(win->pages->page_host); win->pages->page_host = NULL; }
}

static result_t activity_page_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: case evDestroy: return true;
    case evPaint: return false;
    default: return false;
  }
}

static dock_side_t activity_side(window_t *host, dock_side_t previous) {
  irect16_t area = get_client_rect(host);
  return area.w > area.h ? DOCK_LEFT : area.h > area.w ? DOCK_BOTTOM : previous;
}

void page_host_layout(window_t *host) {
  page_host_t *st = activity_state(host);
  if (!st || !st->bar || !st->bar->dock) return;
  dock_side_t side = activity_side(host, st->bar->dock->side);
  if (side != st->bar->dock->side) dock_set_side(st->bar, side);
}

bool page_host_init(window_t *host, const form_def_t *form,
                    const page_handler_t *handlers, int count) {
  if (!host || host->role != WINDOW_ROLE_HOST || !form || form->role != WINDOW_ROLE_HOST ||
      !form->page_bindings || form->page_binding_count <= 0 || count < 0 || (count && !handlers))
    return activity_error(host, 0, "invalid_host_or_definitions");
  if (activity_state(host)) return activity_error(host, 0, "already_initialized");
  const page_binding_t *defs = form->page_bindings;
  window_t *bar = get_window_item(host, defs[0].bar_id);
  toolbar_state_t *tb = bar ? toolbar_get_state(bar) : NULL;
  if (!bar || bar->parent != host || !tb) return activity_error(host, 0, "invalid_activity_bar");
  bool generate_items = tb->item_count == 0;
  if (!generate_items) {
    int buttons = 0;
    for (int i = 0; i < tb->item_count; i++) buttons += tb->items[i].type == TOOLBAR_ITEM_BUTTON;
    if (buttons != form->page_binding_count) return activity_error(host, 0, "unbound_activity_button");
  }
  uint32_t initial = form->initial_page ? form->initial_page : defs[0].command;
  bool found_initial = false;
  for (int i = 0; i < form->page_binding_count; i++) {
    const page_binding_t *d = &defs[i];
    if (!d->name || !d->form || d->form->role != WINDOW_ROLE_PAGE || !d->command || d->command > UINT16_MAX || d->bar_id != defs[0].bar_id)
      return activity_error(host, d->command, "invalid_page_binding");
    bool button = false;
    for (int k = 0; k < tb->item_count; k++)
      if (tb->items[k].type == TOOLBAR_ITEM_BUTTON && (uint32_t)tb->items[k].ident == d->command &&
          (tb->items[k].style & TBSTYLE_CHECKGROUP)) button = true;
    if (!generate_items && !button) return activity_error(host, d->command, "missing_activity_button");
    for (int j = 0; j < i; j++)
      if (defs[j].command == d->command || strcmp(defs[j].name, d->name) == 0)
        return activity_error(host, d->command, "duplicate_page_binding");
    found_initial |= d->command == initial;
  }
  if (!found_initial) return activity_error(host, initial, "unknown_initial_page");
  for (int i = 0; i < count; i++) {
    bool found = false;
    if (!handlers[i].name || !handlers[i].proc) return activity_error(host, 0, "invalid_page_handler");
    for (int j = 0; j < form->page_binding_count; j++) found |= strcmp(handlers[i].name, defs[j].name) == 0;
    if (!found) return activity_error(host, 0, "unknown_page_handler");
    for (int j = 0; j < i; j++)
      if (strcmp(handlers[i].name, handlers[j].name) == 0) return activity_error(host, 0, "duplicate_page_handler");
  }
  if (generate_items) {
    toolbar_item_t *items = calloc((size_t)form->page_binding_count, sizeof(*items));
    if (!items) return activity_error(host, 0, "navigation_allocation_failed");
    for (int i = 0; i < form->page_binding_count; i++) {
      const form_def_t *page = defs[i].form;
      items[i] = (toolbar_item_t){.type = TOOLBAR_ITEM_BUTTON, .ident = defs[i].command,
        .style = TBSTYLE_CHECKGROUP, .text = page->name, .icon = page->icon,
        .checked_icon = page->checked_icon, .tooltip = page->tooltip ? page->tooltip : page->name};
    }
    send_message(bar, tbSetItems, form->page_binding_count, items);
    bool ok = tb->item_count == form->page_binding_count;
    free(items);
    if (!ok) return activity_error(host, 0, "navigation_creation_failed");
  }
  page_host_t *st = calloc(1, sizeof(*st) + (size_t)form->page_binding_count * sizeof(activity_entry_t));
  if (!host->pages) host->pages = calloc(1, sizeof(*host->pages));
  if (!st || !host->pages) { free(st); return activity_error(host, 0, "allocation_failed"); }
  st->bar = bar;
  st->count = form->page_binding_count;
  st->changing = true;
  host->pages->page_host = st;
  for (int i = 0; i < st->count; i++) {
    winproc_t proc = activity_page_proc;
    for (int j = 0; j < count; j++) if (strcmp(handlers[j].name, defs[i].name) == 0) proc = handlers[j].proc;
    activity_entry_t *entry = &st->entries[i];
    entry->command = defs[i].command; entry->focus_id = defs[i].focus_id;
    entry->page = create_window_from_form(defs[i].form, 0, 0, host, proc, host->hinstance, NULL);
    if (!entry->page) goto fail;
    if (!entry->page->pages) entry->page->pages = calloc(1, sizeof(*entry->page->pages));
    if (!entry->page->pages) goto fail;
    show_window(entry->page, false);
    if (!dock_window(entry->page, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 0)) goto fail;
    if (entry->focus_id && !get_window_item(entry->page, entry->focus_id)) goto fail;
  }
  if (!dock_window(bar, activity_side(host, DOCK_LEFT), DOCK_EDGE(DOCK_LEFT) | DOCK_EDGE(DOCK_BOTTOM),
                   DOCK_TOOLBAR | DOCK_NOFLOAT, 0, 0)) goto fail;
  st->changing = false;
  return page_host_select(host, initial);

fail:
  for (int i = 0; i < st->count; i++) if (st->entries[i].page) destroy_window(st->entries[i].page);
  host->pages->page_host = NULL;
  free(st);
  return activity_error(host, 0, "page_creation_failed");
}
