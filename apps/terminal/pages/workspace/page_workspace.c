#include "../../vgat.h"
#include "build/generated/apps/terminal/terminal.h"

typedef struct {
  unsigned next_tab;
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
  window_t *tabs = st ? page_pane_at(page, pane) : NULL;
  if (!tabs || !send_message(tabs, tcGetCount, 0, NULL)) return NULL;
  int index = send_message(tabs, tcGetSelection, 0, NULL);
  window_t *tab = (window_t *)send_message(tabs, tcGetPage, index, NULL);
  return get_window_item(tab, ID_SESSION_TAB_TERMINAL);
}

static bool terminal_new_tab(terminal_workspace_t *st, window_t *tabs) {
  window_t *tab = create_window_from_form(&terminal_session_tab_form, 0, 0,
                                          tabs, terminal_tab_proc, tabs->hinstance, NULL);
  if (!tab) return false;
  snprintf(tab->title, sizeof(tab->title), "Terminal %u", ++st->next_tab);
  int index = send_message(tabs, tcGetCount, 0, NULL) - 1;
  return send_message(tabs, tcSetSelection, index, NULL);
}

bool terminal_workspace_command(window_t *page, uint32_t command) {
  terminal_workspace_t *st = page ? page->userdata : NULL;
  window_t *tabs = st ? page_active_pane(page) : NULL;
  if (!tabs) return false;
  int count = send_message(tabs, tcGetCount, 0, NULL);
  int index = send_message(tabs, tcGetSelection, 0, NULL);
  switch (command) {
    case ID_TAB_NEW:
      return terminal_new_tab(st, tabs);
    case ID_TAB_CLOSE: {
      if (!count) return false;
      window_t *tab = (window_t *)send_message(tabs, tcGetPage, index, NULL);
      // Keep one usable session when closing the final tab in the whole workspace.
      if (count == 1 && page_pane_count(page) == 1 && !terminal_new_tab(st, tabs)) return false;
      return page_close_tab(page, tab);
    }
    case ID_TAB_NEXT:
      return count && send_message(tabs, tcSetSelection, (index + 1) % count, NULL);
    case ID_TAB_PREVIOUS:
      return count && send_message(tabs, tcSetSelection, (index + count - 1) % count, NULL);
    case ID_TAB_SPLIT_RIGHT:
    case ID_TAB_SPLIT_DOWN: {
      bool right = command == ID_TAB_SPLIT_RIGHT;
      if (!terminal_new_tab(st, tabs)) return false;
      window_t *tab = (window_t *)send_message(tabs, tcGetPage, count, NULL);
      if (page_move_tab(page, tab, tabs, right ? PANE_RIGHT : PANE_BOTTOM, -1)) return true;
      page_close_tab(page, tab);
      send_message(tabs, tcSetSelection, index, NULL);
      return false;
    }
    case ID_TAB_MOVE:
    case ID_TAB_FOCUS_NEXT: {
      int panes = page_pane_count(page), current = 0;
      while (current < panes && page_pane_at(page, current) != tabs) current++;
      window_t *next = page_pane_at(page, (current + 1) % panes);
      if (command == ID_TAB_MOVE) {
        if (!count) return false;
        return page_move_tab(page, (window_t *)send_message(tabs, tcGetPage, index, NULL), next, PANE_CENTER, -1);
      }
      return send_message(next, tcSetSelection, send_message(next, tcGetSelection, 0, NULL), NULL);
    }
    default: return false;
  }
}

result_t terminal_workspace_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)lparam;
  terminal_workspace_t *st = win->userdata;
  switch (msg) {
    case evCreate:
      st = allocate_window_data(win, sizeof(*st));
      if (!st) return false;
      if (!page_pane_count(win)) return false;
      for (int i = 0; i < page_pane_count(win); i++)
        if (!terminal_new_tab(st, page_pane_at(win, i))) return false;
      return true;
    case evCommand: return terminal_workspace_command(win, LOWORD(wparam));
    case evPaneAction:
      switch (wparam) {
        case PANE_ACTION_SPLIT_RIGHT: terminal_workspace_command(win, ID_TAB_SPLIT_RIGHT); return true;
        case PANE_ACTION_SPLIT_DOWN:  terminal_workspace_command(win, ID_TAB_SPLIT_DOWN); return true;
        case PANE_ACTION_CLOSE:       terminal_workspace_command(win, ID_TAB_CLOSE); return true;
        default: return false;
      }
    case evPaint: return false;
    case evDestroy: free(st); win->userdata = NULL; return true;
    default: return false;
  }
}
