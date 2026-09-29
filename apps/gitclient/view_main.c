// Main window — thin router. Page sub-forms own their UI and event logic.

#include "gitclient.h"
#include "gc_actions.h"
#include "pages/changes/page_changes.h"
#include "pages/history/page_history.h"
#include "pages/github/page_github.h"
#include <orion/user/vga_font.h>
#include <orion/commctl/menubar.h>

void gc_set_view_mode(int tab) {
  gc_state_t *gc = g_gc; if (!gc || !gc->main_win) return;
  if (gc->tab != GC_TAB_OVERVIEW && tab == GC_TAB_OVERVIEW) gc->focus_tab = gc->tab ? gc->tab : GC_TAB_CHANGES;
  gc->tab = tab;
  gc_diff_invalidate();
  gc->history_mode = (tab == GC_TAB_HISTORY);
  if (gc->tabs_win) send_message(gc->tabs_win, tcSetSelection, (uint32_t)tab, NULL);

  window_t *page = tab == GC_TAB_OVERVIEW ? gc->overview_page_win :
                   tab == GC_TAB_CHANGES  ? gc->changes_page_win :
                   tab == GC_TAB_HISTORY  ? gc->history_page_win :
                   tab == GC_TAB_GITHUB   ? gc->github_page_win : NULL;
  if (page) set_host_page(gc->main_win, page);

  switch (tab) {
    case GC_TAB_OVERVIEW:
      gc->files_win = NULL;
      gc->diff_win  = NULL;
      gc_overview_refresh();
      if (gc->board_win) set_focus(gc->board_win);
      break;
    case GC_TAB_CHANGES:
      gc->files_win = gc->changes_files_win;
      gc->diff_win  = gc->changes_diff_win;
      gc->selected_commit = -1;
      gc->selected_file   = -1;
      if (gc->changes_files_win)
        send_message(gc->changes_files_win, tvSetFilter, ID_DB_FILES_COMMIT_ID, (void *)(intptr_t)0);
      break;
    case GC_TAB_HISTORY:
      gc->files_win = gc->history_files_win;
      gc->diff_win  = gc->history_diff_win;
      gc->selected_commit = gc->log_win
        ? (int)send_message(gc->log_win, RVM_GETSELECTION, 0, NULL) : -1;
      gc->selected_file = -1;
      break;
    case GC_TAB_GITHUB:
      gc->files_win = NULL;
      gc->diff_win  = NULL;
      page_github_refresh();
      break;
    default: break;
  }

  GC_TRACE("set_view_mode tab=%d diff_win=%p files_win=%p",
           tab, (void *)gc->diff_win, (void *)gc->files_win);

  if (tab == GC_TAB_CHANGES || tab == GC_TAB_HISTORY) gc_diff_refresh();
  gc_update_status();
  invalidate_window(gc->main_win);
}
