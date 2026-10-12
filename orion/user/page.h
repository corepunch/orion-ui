#ifndef __PAGE_H__
#define __PAGE_H__

#include "user.h"

typedef enum { PANE_CENTER, PANE_LEFT, PANE_RIGHT, PANE_TOP, PANE_BOTTOM } pane_edge_t;
typedef struct {
  window_t *pane;
  pane_edge_t edge;
  int index;
  irect16_t rect; // preview in Page content coordinates
} pane_drop_t;

// Page owns its split tree and tab groups; content windows retain their identity.
int page_pane_count(window_t *page);
window_t *page_pane_at(window_t *page, int index);
window_t *page_active_pane(window_t *page);
window_t *page_split_pane(window_t *page, window_t *pane, pane_edge_t edge);
bool page_move_tab(window_t *page, window_t *view, window_t *target, pane_edge_t edge, int before);
bool page_close_tab(window_t *page, window_t *view);
bool page_get_drop_preview(window_t *page, pane_drop_t *preview);

// Window-system integration; application page procedures need no forwarding.
bool page_handle_message(window_t *page, uint32_t msg, uint32_t wparam, void *lparam, result_t *result);
void page_paint_overlay(window_t *page, irect16_t clip);

#endif
