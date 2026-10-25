#ifndef __PAGE_HOST_H__
#define __PAGE_HOST_H__

#include "user.h"

typedef struct page_binding_s {
  uint32_t command;
  uint32_t bar_id;
  const char *name;
  const form_def_t *form;
  uint32_t focus_id;
} page_binding_t;

// Runtime behavior only; navigation and form bindings come from .orion.
typedef struct {
  const char *name;
  winproc_t proc;
} page_handler_t;

bool page_host_init(window_t *host, const form_def_t *form,
                    const page_handler_t *handlers, int count);
bool page_host_select(window_t *host, uint32_t command);
window_t *page_host_page(window_t *host, uint32_t command);
uint32_t page_host_selection(window_t *host);
// Back/forward through the pages selected so far (direction -1 back, +1 forward).
// Navigating sends evPageChanged like any selection; a new selection drops the forward entries.
bool page_host_can_go(window_t *host, int direction);
bool page_host_go(window_t *host, int direction);
// Collapse the ActivityBar to an icon rail (its labels hidden) or expand it again (≈ toggleSidebar:).
bool page_host_set_sidebar_collapsed(window_t *host, bool collapsed);
bool page_host_sidebar_collapsed(window_t *host);

// Window-system integration.
bool page_host_command(window_t *host, uint32_t wparam, void *lparam);
void page_host_remember_focus(window_t *win);
void page_host_forget_window(window_t *win);
void page_host_layout(window_t *host);

#endif
