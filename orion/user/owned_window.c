// WinAPI ownership is independent of child layout and coordinate spaces.
#include "user.h"
#include "messages.h"
#include <stdio.h>

window_t *window_lifetime_parent(window_t *win) {
  return win->owner ? win->owner : win->mdi_client ? get_root_window(win->mdi_client) : NULL;
}

window_t *window_owner_root(window_t *win) {
  if (!win) return NULL;
  win = get_root_window(win);
  while (window_lifetime_parent(win)) win = window_lifetime_parent(win);
  return win;
}

bool window_is_visible(const window_t *win) {
  if (!win || !(win->flags & WINDOW_STATE_VISIBLE)) return false;
  if (win->parent) return window_is_visible(win->parent);
  if (win->mdi_client) return window_is_visible(win->mdi_client);
  return !win->owner || window_is_visible(win->owner);
}

bool window_is_disabled(const window_t *win) {
  if (!win) return false;
  for (const window_t *p = win; p; p = p->parent)
    if (p->flags & WINDOW_STATE_DISABLED) return true;
  window_t *root = get_root_window((window_t *)win);
  return root->mdi_client && window_is_disabled(root->mdi_client);
}

bool window_is_modal_blocked(const window_t *win) {
  if (!win) return false;
  window_t *root = get_root_window((window_t *)win);
  window_t *modal = g_ui_runtime.modal_window;
  if (!modal || !modal->owner) return false;
  if (window_owner_root(root) != window_owner_root(modal)) return false;
  for (window_t *p = root; p; p = window_lifetime_parent(p)) if (p == modal) return false;
  return true;
}

bool window_is_enabled(const window_t *win) {
  return win && !window_is_disabled(win) && !window_is_modal_blocked(win);
}

bool window_set_owner(window_t *win, window_t *owner) {
  if (owner && is_window(owner)) owner = get_root_window(owner);
  bool valid = is_window(win) && !win->parent && !win->mdi_client && (!owner || is_window(owner));
  for (window_t *p = owner; valid && p; p = window_lifetime_parent(p)) if (p == win) valid = false;
  if (!valid) {
    fprintf(stderr, "[window] owner assignment rejected win=%p owner=%p\n", (void *)win, (void *)owner);
    fflush(stderr);
    return false;
  }
  win->owner = owner;
  if (owner) {
    win->hinstance = owner->hinstance;
    win->flags |= WINDOW_NOTRAYBUTTON;
  }
  window_order_owned();
  request_composite();
  return true;
}

static void owned_append(window_t **head, window_t **tail, window_t *win) {
  win->next = NULL;
  if (*tail) (*tail)->next = win; else *head = win;
  *tail = win;
}

static void owned_append_children(window_t *owner, window_t **remaining, window_t **head, window_t **tail) {
  for (;;) {
    window_t **link = NULL;
    int best_band = 3;
    for (window_t **candidate = remaining; *candidate; candidate = &(*candidate)->next) {
      window_t *win = *candidate;
      window_t *parent = window_lifetime_parent(win);
      if (!parent || window_owner_root(win) != owner) continue;
      bool parent_ordered = false;
      for (window_t *p = *head; p; p = p->next) if (p == parent) parent_ordered = true;
      int band = win->flags & WINDOW_DIALOG ? 2 : win->flags & (WINDOW_TOOLWINDOW | WINDOW_ALWAYSONTOP) ? 1 : 0;
      if (parent_ordered && band < best_band) { link = candidate; best_band = band; }
    }
    if (!link) return;
    window_t *win = *link;
    *link = win->next;
    owned_append(head, tail, win);
  }
}

void window_order_owned(void) {
  window_t *remaining = g_ui_runtime.windows, *head = NULL, *tail = NULL;
  while (remaining) {
    window_t **link = &remaining;
    while (*link && window_lifetime_parent(*link)) link = &(*link)->next;
    if (!*link) {
      fprintf(stderr, "[window] ownership graph contains an orphan or cycle\n");
      fflush(stderr);
      if (tail) tail->next = remaining; else head = remaining;
      break;
    }
    window_t *win = *link;
    *link = win->next;
    owned_append(&head, &tail, win);
    owned_append_children(win, &remaining, &head, &tail);
  }
  g_ui_runtime.windows = head;
}
