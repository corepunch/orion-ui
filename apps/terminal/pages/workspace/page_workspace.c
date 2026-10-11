#include "../../vgat.h"
#include "build/generated/apps/terminal/terminal.h"

typedef struct {
  window_t *panes[2];
  unsigned next_tab[2];
  int active_pane;
} terminal_workspace_t;

static result_t terminal_tab_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  switch (msg) {
    case evCreate: case evDestroy: return true;
    case evPaint: return false;
    default: return false;
  }
}

window_t *terminal_workspace_terminal(window_t *page, int pane) {
  terminal_workspace_t *st = page ? page->userdata : NULL;
  if (!st || pane < 0 || pane >= 2 || !st->panes[pane]) return NULL;
  window_t *tabs = st->panes[pane];
  if (!send_message(tabs, tcGetCount, 0, NULL)) return NULL;
  int index = send_message(tabs, tcGetSelection, 0, NULL);
  window_t *tab = (window_t *)send_message(tabs, tcGetPage, index, NULL);
  return get_window_item(tab, ID_SESSION_TAB_TERMINAL);
}

static bool terminal_new_tab(terminal_workspace_t *st, int pane) {
  window_t *tabs = st->panes[pane];
  window_t *tab = create_window_from_form(&terminal_session_tab_form, 0, 0,
                                          tabs, terminal_tab_proc, tabs->hinstance, NULL);
  if (!tab) return false;
  snprintf(tab->title, sizeof(tab->title), "Terminal %u", ++st->next_tab[pane]);
  int index = send_message(tabs, tcGetCount, 0, NULL) - 1;
  return send_message(tabs, tcSetSelection, index, NULL);
}

bool terminal_workspace_command(window_t *page, uint32_t command) {
  terminal_workspace_t *st = page ? page->userdata : NULL;
  if (!st || !st->panes[st->active_pane]) return false;
  window_t *tabs = st->panes[st->active_pane];
  int count = send_message(tabs, tcGetCount, 0, NULL);
  int index = send_message(tabs, tcGetSelection, 0, NULL);
  switch (command) {
    case ID_TAB_NEW: case ID_WORKSPACE_PAGE_NEW_TAB:
      return terminal_new_tab(st, st->active_pane);
    case ID_TAB_CLOSE: case ID_WORKSPACE_PAGE_CLOSE_TAB:
      if (!count) return false;
      if (count == 1 && !terminal_new_tab(st, st->active_pane)) return false;
      if (!send_message(tabs, tcDeletePage, index, NULL)) return false;
      return send_message(tabs, tcSetSelection, send_message(tabs, tcGetSelection, 0, NULL), NULL);
    case ID_TAB_NEXT: case ID_WORKSPACE_PAGE_NEXT_TAB:
      return count && send_message(tabs, tcSetSelection, (index + 1) % count, NULL);
    case ID_TAB_PREVIOUS: case ID_WORKSPACE_PAGE_PREVIOUS_TAB:
      return count && send_message(tabs, tcSetSelection, (index + count - 1) % count, NULL);
    default: return false;
  }
}

result_t terminal_workspace_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  terminal_workspace_t *st = win->userdata;
  switch (msg) {
    case evCreate:
      st = allocate_window_data(win, sizeof(*st));
      if (!st) return false;
      st->panes[0] = get_window_item(win, ID_WORKSPACE_PAGE_LEFT_TABS);
      st->panes[1] = get_window_item(win, ID_WORKSPACE_PAGE_RIGHT_TABS);
      for (int i = 0; i < 2; i++)
        if (!st->panes[i] || !send_message(st->panes[i], tcSetStyle, TAB_STYLE_RESTORE_FOCUS, NULL) ||
            !terminal_new_tab(st, i)) return false;
      st->active_pane = 0;
      return true;
    case evDescendantFocus:
      for (window_t *p = lparam; st && p && p != win; p = p->parent)
        for (int i = 0; i < 2; i++) if (p == st->panes[i]) st->active_pane = i;
      return true;
    case evCommand: return terminal_workspace_command(win, LOWORD(wparam));
    case evPaint: return false;
    case evDestroy: free(st); win->userdata = NULL; return true;
    default: return false;
  }
}
