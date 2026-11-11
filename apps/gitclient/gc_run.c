// Launch target runs. Each target that ran keeps a tab in the Overview's
// terminal pane holding a TerminalView (the Terminal app's emulator, from the
// terminal_components plugin) that runs its command in a PTY. The pane stays
// hidden until the Terminal button shows it; the targets list marks a running
// target with a stop icon, and clicking that icon stops it.

#include "gitclient.h"
#include <orion/commctl/commctl.h>
#include <orion/user/page.h>
#include "apps/terminal/components/terminal_view.h"

static gc_run_t *run_find(const char *checkout, const char *target) {
  gc_state_t *gc = g_gc;
  for (int i = 0; i < gc->run_count; i++)
    if (!strcmp(gc->runs[i].checkout, checkout) && !strcmp(gc->runs[i].target, target)) return &gc->runs[i];
  return NULL;
}

// Closing a view in the pane destroys its tab; forget runs whose view is gone.
static void runs_prune(void) {
  gc_state_t *gc = g_gc; int kept = 0;
  for (int i = 0; i < gc->run_count; i++) if (is_window(gc->runs[i].term)) gc->runs[kept++] = gc->runs[i];
  gc->run_count = kept;
}

bool gc_run_is_running(const char *checkout, const char *target) {
  if (!g_gc || !checkout || !target) return false;
  runs_prune();
  gc_run_t *r = run_find(checkout, target);
  return r && send_message(r->term, termIsRunning, 0, NULL);
}

static const db_target_t *selected_target(void) {
  window_t *list = g_gc->targets_win;
  return list ? (const db_target_t *)send_message(list, tvGetSelectedRecord, 0, NULL) : NULL;
}

// The stop icon of each running target, and Run/Stop on the button.
void gc_run_sync_targets(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->workspace_db) return;
  result_node_t *rows = (result_node_t *)send_db_message(gc->workspace_db, dbFetch, MAKEDWORD(TABLE_TARGETS, 0), NULL);
  bool changed = false;
  for (result_node_t *n = rows; n; n = n->next) {
    db_target_t rec = **(db_target_t **)n->data;
    bool running = gc_run_is_running(rec.checkout, rec.name);
    if (rec.running == running) continue;
    rec.running = running;
    changed |= send_db_message(gc->workspace_db, dbUpdate, TABLE_TARGETS, &rec) != 0;
  }
  free_result_list(rows);
  if (changed && gc->targets_win) send_message(gc->targets_win, tvRefresh, 0, NULL);
  const db_target_t *t = selected_target();
  if (gc->run_button_win) {
    bool stop = t && t->running;
    set_window_title(gc->run_button_win, stop ? ui_tr("Stop") : ui_tr("Run"));
    send_message(gc->run_button_win, btnSetIconName, 0, (void *)(stop ? "gc-stop-circle" : "gc-play"));
    enable_window(gc->run_button_win, t != NULL);
  }
}

// The pane that holds the run tabs: the declared one, or (after its last view
// was closed and the pane collapsed) a new pane split below the targets.
static window_t *terminal_pane(void) {
  gc_state_t *gc = g_gc;
  if (gc->terminal_tabs_win && is_window(gc->terminal_tabs_win) && page_is_pane(gc->terminal_tabs_win))
    return gc->terminal_tabs_win;
  window_t *below = gc->targets_win;
  while (below && !page_is_pane(below)) below = below->parent;
  gc->terminal_tabs_win = below ? page_split_pane(gc->overview_page_win, below, PANE_BOTTOM) : NULL;
  if (!gc->terminal_tabs_win) { fprintf(stderr, "[gc] no pane for the target terminal\n"); fflush(stderr); return NULL; }
  window_set_state(gc->terminal_tabs_win, WINDOW_STATE_VISIBLE, gc->terminal_shown);
  return gc->terminal_tabs_win;
}

static void pane_relayout(window_t *pane) {
  if (pane->parent) { send_message(pane->parent, evResize, 0, NULL); invalidate_window(pane->parent); }
}

void gc_run_show_terminal(bool show) {
  gc_state_t *gc = g_gc;
  window_t *pane = gc ? terminal_pane() : NULL;
  if (!pane) return;
  gc->terminal_shown = show;
  if (window_has_state(pane, WINDOW_STATE_VISIBLE) != show) {
    window_set_state(pane, WINDOW_STATE_VISIBLE, show);
    pane_relayout(pane);
  }
  if (gc->terminal_button_win) send_message(gc->terminal_button_win, btnSetCheck, show, NULL);
}

static void select_tab(gc_run_t *r) {
  window_t *pane = r->tab->parent;
  int n = send_message(pane, tcGetCount, 0, NULL);
  for (int i = 0; i < n; i++)
    if ((window_t *)send_message(pane, tcGetPage, i, NULL) == r->tab) { send_message(pane, tcSetSelection, i, NULL); return; }
}

static result_t run_tab_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  return msg == evCreate || msg == evDestroy;
}

static gc_run_t *run_open(const char *checkout, const char *target) {
  gc_state_t *gc = g_gc;
  runs_prune();
  gc_run_t *r = run_find(checkout, target);
  if (r) return r;
  if (gc->run_count >= GC_MAX_RUNS) {
    fprintf(stderr, "[gc] run rejected target=%s: %d runs keep their terminals; close one\n", target, gc->run_count);
    fflush(stderr); return NULL;
  }
  window_t *pane = terminal_pane();
  if (!pane) return NULL;
  window_t *tab = create_window_from_form(&gc_terminal_tab_form, 0, 0, pane, run_tab_proc, gc->hinstance, NULL);
  window_t *term = tab ? get_window_item(tab, ID_TERMINAL_TAB_TERMINAL) : NULL;
  if (!term || !window_is_class(term, TERMINAL_VIEW_CLASS)) {
    fprintf(stderr, "[gc] run rejected target=%s: no TerminalView (is terminal_components built?)\n", target);
    fflush(stderr);
    if (tab) page_close_tab(gc->overview_page_win, tab);
    return NULL;
  }
  const char *dir = strrchr(checkout, '/');
  snprintf(tab->title, sizeof(tab->title), "%s - %s", target, dir && dir[1] ? dir + 1 : checkout);
  r = &gc->runs[gc->run_count++];
  memset(r, 0, sizeof(*r));
  snprintf(r->checkout, sizeof(r->checkout), "%s", checkout);
  snprintf(r->target, sizeof(r->target), "%s", target);
  r->tab = tab; r->term = term;
  return r;
}

bool gc_run_start(const char *checkout, const char *target, const char *command) {
  if (!g_gc || !checkout || !checkout[0] || !target || !command || !command[0]) {
    fprintf(stderr, "[gc] run rejected: checkout=%s target=%s command=%s\n", checkout ? checkout : "(null)",
            target ? target : "(null)", command ? command : "(null)");
    fflush(stderr); return false;
  }
  gc_run_t *r = run_open(checkout, target);
  if (!r) return false;
  select_tab(r);
  if (send_message(r->term, termIsRunning, 0, NULL)) return true;
  r->stopping = false;
  terminal_exec_t ex = {command, checkout};
  bool ok = send_message(r->term, termExec, 0, &ex);
  if (!ok) gc_run_show_terminal(true);   // the view says why it did not start
  gc_run_sync_targets();
  return ok;
}

// The first click asks the program's process group to end; a second one kills it.
bool gc_run_stop(const char *checkout, const char *target) {
  runs_prune();
  gc_run_t *r = g_gc && checkout && target ? run_find(checkout, target) : NULL;
  if (!r || !send_message(r->term, termIsRunning, 0, NULL)) {
    fprintf(stderr, "[gc] stop rejected target=%s: not running\n", target ? target : "(null)"); fflush(stderr);
    return false;
  }
  bool force = r->stopping;
  r->stopping = true;
  return send_message(r->term, termKill, force, NULL);
}

static void run_toggle_selected(void) {
  const db_target_t *t = selected_target();
  if (!t) { fprintf(stderr, "[gc] run: no target selected\n"); fflush(stderr); return; }
  char checkout[512], name[64], command[1024];
  snprintf(checkout, sizeof(checkout), "%s", t->checkout);
  snprintf(name, sizeof(name), "%s", t->name);
  snprintf(command, sizeof(command), "%s", t->command);
  if (gc_run_is_running(checkout, name)) gc_run_stop(checkout, name);
  else gc_run_start(checkout, name, command);
}

static void run_exited(window_t *term) {
  gc_state_t *gc = g_gc;
  for (int i = 0; i < gc->run_count; i++) {
    gc_run_t *r = &gc->runs[i];
    if (r->term != term) continue;
    int code = send_message(term, termGetExitCode, 0, NULL);
    if (code != 0 && !r->stopping) { select_tab(r); gc_run_show_terminal(true); }   // a failure shows its output
    r->stopping = false;
  }
  gc_run_sync_targets();
}

bool gc_run_handle_command(uint32_t wparam, void *lparam) {
  gc_state_t *gc = g_gc; uint16_t code = HIWORD(wparam); window_t *src = lparam;
  if (!gc || !src) return false;
  if (code == termnExited && window_is_class(src, TERMINAL_VIEW_CLASS)) { run_exited(src); return true; }
  if (src == gc->targets_win && (code == RVN_DBLCLK || code == RVN_ICONCLICK)) { run_toggle_selected(); return true; }
  if (src == gc->targets_win && code == RVN_SELCHANGE) { gc_run_sync_targets(); return false; }
  if (code != btnClicked) return false;
  if (src == gc->run_button_win)      { run_toggle_selected(); return true; }
  if (src == gc->terminal_button_win) { gc_run_show_terminal(!gc->terminal_shown); return true; }
  return false;
}
