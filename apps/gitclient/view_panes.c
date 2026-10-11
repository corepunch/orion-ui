#include "gitclient.h"

bool gc_pane_command(uint16_t id) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->main_win) return false;
  const uint32_t hosts[] = {ID_OVERVIEW_PAGE_PANES, ID_CHANGES_PAGE_PANES, ID_HISTORY_PAGE_PANES, ID_GITHUB_PAGE_PANES};
  if (gc->tab < 0 || gc->tab >= ARRAY_LEN(hosts)) return false;
  window_t *host = get_window_item(window_active_page(gc->main_win), hosts[gc->tab]);
  window_t *pane = pane_host_active(host);
  if (!pane) return false;
  int count = send_message(pane, tcGetCount, 0, NULL);
  if (!count) return false;
  int selected = send_message(pane, tcGetSelection, 0, NULL);
  if (id == ID_VIEW_NEXT_TAB) return send_message(pane, tcSetSelection, (selected + 1) % count, NULL);
  int panes = pane_host_count(host), current = 0;
  while (current < panes && pane_host_pane(host, current) != pane) current++;
  window_t *next = pane;
  for (int step = 1; step < panes; step++) {
    window_t *candidate = pane_host_pane(host, (current + step) % panes);
    if (window_has_state(candidate, WINDOW_STATE_VISIBLE) && send_message(candidate, tcGetCount, 0, NULL)) {
      next = candidate; break;
    }
  }
  if (id == ID_VIEW_NEXT_PANE)
    return send_message(next, tcSetSelection, send_message(next, tcGetSelection, 0, NULL), NULL);
  pane_edge_t edge;
  switch (id) {
    case ID_VIEW_MOVE_TAB:    edge = PANE_CENTER; break;
    case ID_VIEW_SPLIT_LEFT:  edge = PANE_LEFT; break;
    case ID_VIEW_SPLIT_RIGHT: edge = PANE_RIGHT; break;
    case ID_VIEW_SPLIT_UP:    edge = PANE_TOP; break;
    case ID_VIEW_SPLIT_DOWN:  edge = PANE_BOTTOM; break;
    default: return false;
  }
  window_t *target = edge != PANE_CENTER && count > 1 ? pane : next;
  if (edge != PANE_CENTER && target == pane && count == 1) return false;
  window_t *view = (window_t *)send_message(pane, tcGetPage, selected, NULL);
  return pane_host_move_tab(host, view, target, edge, -1);
}
