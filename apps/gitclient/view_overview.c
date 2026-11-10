// Overview page: every checkout of every repository in the workspace, and the
// launch targets of the selected checkout.
//
// The rows live in the transient tables of the "workspace" database
// (gitclient.orion); the TableViews group checkouts by project and list the
// selected checkout's launch.xml targets as a detail view.

#include "gitclient.h"
#include <orion/commctl/commctl.h>

// ── row model ──────────────────────────────────────────────────────────────────

static bool row_dirty(const git_summary_t *t) { return t->staged || t->unstaged || t->untracked; }

static const char *row_tone(const git_summary_t *t) {
  if (t->missing)                                               return "missing";
  if (t->conflicts)                                             return "conflict";
  if (row_dirty(t))                                             return "dirty";
  if (t->ahead || t->behind || (t->no_upstream && !t->initial) || t->gone || t->prunable) return "remote";
  return "sync";
}

static void append(char *out, size_t n, const char *fmt, int value) {
  size_t len = strlen(out);
  if (len && len + 2 < n) { memcpy(out + len, ", ", 3); len += 2; }
  if (len < n) snprintf(out + len, n - len, fmt, value);
}

static void row_state(const git_summary_t *t, char *out, size_t n) {
  out[0] = 0;
  if (t->missing)   { snprintf(out, n, "%s", ui_tr("Folder missing")); return; }
  if (t->conflicts) append(out, n, ui_tr(t->conflicts == 1 ? "%d conflict" : "%d conflicts"), t->conflicts);
  if (t->staged)    append(out, n, ui_tr("%d staged"), t->staged);
  if (t->unstaged)  append(out, n, ui_tr("%d modified"), t->unstaged);
  if (t->untracked) append(out, n, ui_tr("%d new"), t->untracked);
  if (t->ahead)     append(out, n, ui_tr("%d to push"), t->ahead);
  if (t->behind)    append(out, n, ui_tr("%d to pull"), t->behind);
  if (out[0]) return;
  const char *word = t->prunable ? "Prunable" : t->gone ? "Upstream gone"
                   : t->no_upstream && !t->initial ? "Unpublished" : "Up to date";
  snprintf(out, n, "%s", ui_tr(word));
}

static void row_name(const git_summary_t *t, char *out, size_t n) {
  if (!t->linked) { snprintf(out, n, "%s", ui_tr("Main checkout")); return; }
  const char *dir = t->dir; size_t len = strlen(t->repo);
  if (!strncmp(dir, t->repo, len) && (dir[len] == '-' || dir[len] == '_') && dir[len + 1]) dir += len + 1;
  snprintf(out, n, "%s", dir);
}

static void overview_clear_table(database_t *db, int table) {
  result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(table, 0), NULL);
  for (result_node_t *r = rows; r; r = r->next)
    send_db_message(db, dbDelete, (uint32_t)table, (void *)(intptr_t)(*(int **)r->data)[0]);
  free_result_list(rows);
}

static void insert_targets(database_t *db, int checkout_id, const gc_launch_t *launch) {
  for (int i = 0; i < launch->count; i++) {
    const gc_launch_target_t *t = &launch->targets[i];
    db_target_t rec = {0};
    rec.checkout_id = checkout_id;
    snprintf(rec.name, sizeof(rec.name), "%s", t->name);
    snprintf(rec.kind, sizeof(rec.kind), "%s", gc_launch_kind_name(t->kind));
    if (!gc_launch_command(t, NULL, rec.command, sizeof(rec.command))) rec.command[0] = 0;
    if (!send_db_message(db, dbInsert, TABLE_TARGETS, &rec)) return;
  }
}

static void fill_rows(void) {
  gc_state_t *gc = g_gc; database_t *db = gc->workspace_db;
  if (!db) return;
  overview_clear_table(db, TABLE_TARGETS);
  overview_clear_table(db, TABLE_CHECKOUTS);
  for (int i = 0; i < gc->checkout_count; i++) {
    const git_summary_t *t = &gc->checkouts[i];
    if (gc->attention_only && !gc_checkout_needs_attention(t)) continue;
    db_checkout_t rec = {0};
    snprintf(rec.project, sizeof(rec.project), "%s", t->repo);
    row_name(t, rec.name, sizeof(rec.name));
    snprintf(rec.branch, sizeof(rec.branch), "%s", t->missing ? "" : t->branch);
    row_state(t, rec.state, sizeof(rec.state));
    snprintf(rec.tone, sizeof(rec.tone), "%s", row_tone(t));
    snprintf(rec.path, sizeof(rec.path), "%s", t->path);
    snprintf(rec.root, sizeof(rec.root), "%s", t->root);
    gc_launch_t *launch = calloc(1, sizeof(*launch));
    int targets = launch && !t->missing ? gc_launch_load(t->path, launch) : 0;
    if (targets < 0) snprintf(rec.target, sizeof(rec.target), "%s", ui_tr("launch.xml has errors"));
    else if (targets > 0) snprintf(rec.target, sizeof(rec.target), "%s", launch->targets[launch->default_index].name);
    const db_checkout_t *row = (const db_checkout_t *)send_db_message(db, dbInsert, TABLE_CHECKOUTS, &rec);
    if (row && targets > 0) insert_targets(db, row->id, launch);
    free(launch);
  }
  // Reinserted checkouts get new keys, so the targets follow the selection again.
  if (gc->checkouts_win) {
    send_message(gc->checkouts_win, tvRefresh, 0, NULL);
    tableview_handle_master_selection(get_root_window(gc->checkouts_win), gc->checkouts_win);
  }
}

// ── summary strip ──────────────────────────────────────────────────────────────

static window_t *make_label(window_t *parent, const char *text) {
  irect16_t frame = {0, 0, 10, CONTROL_HEIGHT};
  window_t *label = create_window(text, 0, &frame, parent, win_label, g_gc->hinstance, NULL);
  if (!label) return NULL;
  label_create_params_t style = { .color_index = brTextNormal, .font = LABEL_TEXT, .color_set = true, .truncate = true };
  send_message(label, lbSetStyle, 0, &style);
  return label;
}

static void make_badge_text(window_t *parent, sys_color_idx_t role, const char *text) {
  irect16_t frame = {0, 0, 10, BADGE_HEIGHT};
  window_t *badge = create_window(text, 0, &frame, parent, win_badge, g_gc->hinstance, NULL);
  if (badge) send_message(badge, bdSetColor, role, NULL);
}

static void make_badge(window_t *parent, sys_color_idx_t role, const char *fmt, int n) {
  char text[64]; snprintf(text, sizeof(text), fmt, n);
  make_badge_text(parent, role, text);
}

typedef struct { int repos, trees, dirty, push, pull, conflicts; } overview_counts_t;

static overview_counts_t count_rows(void) {
  overview_counts_t c = {0};
  for (int i = 0; i < g_gc->checkout_count; i++) {
    const git_summary_t *t = &g_gc->checkouts[i];
    c.repos += !t->linked; c.trees++;
    c.dirty += row_dirty(t); c.push += t->ahead > 0; c.pull += t->behind > 0; c.conflicts += t->conflicts > 0;
  }
  return c;
}

static void rebuild_summary(void) {
  gc_state_t *gc = g_gc;
  if (!gc->summary_win) return;
  while (gc->summary_win->children) destroy_window(gc->summary_win->children);
  overview_counts_t c = count_rows();
  char text[96]; snprintf(text, sizeof(text), ui_tr("%d repositories, %d worktrees"), c.repos, c.trees);
  window_t *summary = gc->summary_win;
  make_label(summary, text);
  if (c.conflicts) make_badge(summary, brTextError,   ui_tr("%d with conflicts"), c.conflicts);
  if (c.dirty)     make_badge(summary, brTextWarning, ui_tr("%d uncommitted"),    c.dirty);
  if (c.push)      make_badge(summary, brTextInfo,    ui_tr("%d to push"),        c.push);
  if (c.pull)      make_badge(summary, brTextInfo,    ui_tr("%d to pull"),        c.pull);
  if (!c.conflicts && !c.dirty && !c.push && !c.pull && gc->checkout_count)
    make_badge_text(summary, brTextSuccess, ui_tr("everything is in sync"));
  irect16_t frame = {0, 0, 170, BUTTON_HEIGHT};
  gc->filter_btn = create_window(ui_tr(gc->attention_only ? "Showing: needs attention" : "Showing: all"), BUTTON_PUSHLIKE, &frame,
                                 summary, win_button, gc->hinstance, NULL);
  if (gc->filter_btn) { gc->filter_btn->value = gc->attention_only; gc->filter_btn->layout.layout_fixed_w = 170; }
  window_layout_sync(summary->parent ? summary->parent : summary);
}

// ── page ───────────────────────────────────────────────────────────────────────

database_t *gc_workspace_db_create(void) {
  DB_CLASS(db_schema_xml);
  database_t *db = create_database_with_schema("workspace", "db_schema_xml", &gitclient_workspace_schema, NULL);
  if (!db) { fprintf(stderr, "[gc] could not create the workspace database\n"); fflush(stderr); return NULL; }
  register_database("workspace", db);
  return db;
}

result_t gc_page_overview_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (!g_gc) return false;
  if (msg == evCreate) {
    g_gc->overview_page_win = win;
    g_gc->summary_win   = get_window_item(win, ID_OVERVIEW_PAGE_SUMMARY);
    g_gc->checkouts_win = get_window_item(win, ID_OVERVIEW_PAGE_CHECKOUTS);
    g_gc->targets_win   = get_window_item(win, ID_OVERVIEW_PAGE_TARGETS);
    return true;
  }
  if (msg == evDestroy && g_gc->overview_page_win == win) {
    g_gc->overview_page_win = g_gc->summary_win = g_gc->checkouts_win = g_gc->targets_win = g_gc->filter_btn = NULL;
    return true;
  }
  return false;
}

static int row_cmp(const void *pa, const void *pb) {
  const git_summary_t *a = pa, *b = pb;
  int c = strcasecmp(a->repo, b->repo); if (c) return c;
  if ((c = strcmp(a->root, b->root))) return c;
  if (a->linked != b->linked) return a->linked ? 1 : -1;
  return strcasecmp(a->dir, b->dir);
}

static void overview_show(void) {
  gc_state_t *gc = g_gc;
  qsort(gc->checkouts, (size_t)gc->checkout_count, sizeof(gc->checkouts[0]), row_cmp);
  rebuild_summary();
  fill_rows();
  if (gc->overview_page_win) window_layout_sync(gc->overview_page_win);   // the strip's height comes from its children
  if (gc->tab == GC_TAB_OVERVIEW) gc_update_status();
}

void gc_overview_refresh_sync(void) {
  gc_state_t *gc = g_gc; if (!gc) return;
  gc->checkout_count = git_workspace_checkouts(gc->workspace, gc->workspace_count, gc->checkouts, GC_MAX_CHECKOUTS);
  overview_show();
}

void gc_overview_refresh(void) {
  gc_state_t *gc = g_gc; if (!gc) return;
  if (gc->overview_loading) { gc->overview_stale = true; return; }
  if (gc->workspace_count <= 0) { gc->checkout_count = 0; overview_show(); return; }
  if (git_workspace_checkouts_async(gc->workspace, gc->workspace_count, gc->main_win)) gc->overview_loading = true;
  else { fprintf(stderr, "[gc] background load not started (roots=%d); reading on the UI thread\n", gc->workspace_count); fflush(stderr); gc_overview_refresh_sync(); }
}

void gc_overview_loaded(git_workspace_result_t *res) {
  gc_state_t *gc = g_gc; if (!gc) { free(res); return; }
  gc->overview_loading = false;
  if (res) {
    gc->checkout_count = res->count;
    memcpy(gc->checkouts, res->rows, (size_t)res->count * sizeof(gc->checkouts[0]));
    free(res);
    overview_show();
  }
  if (gc->overview_stale) { gc->overview_stale = false; gc_overview_refresh(); }
}

static const db_checkout_t *selected_checkout(void) {
  window_t *list = g_gc ? g_gc->checkouts_win : NULL;
  return list ? (const db_checkout_t *)send_message(list, tvGetSelectedRecord, 0, NULL) : NULL;
}

void gc_overview_status(char *out, size_t n) {
  overview_counts_t c = count_rows();
  int len = snprintf(out, n, ui_tr("%d repositories, %d worktrees | %d uncommitted | %d to push | %d to pull"),
                     c.repos, c.trees, c.dirty, c.push, c.pull);
  if (len < 0 || (size_t)len >= n) return;
  if (c.conflicts) len += snprintf(out + len, n - (size_t)len, " | %s", ui_tr("CONFLICTS"));
  const db_target_t *target = g_gc->targets_win ? (const db_target_t *)send_message(g_gc->targets_win, tvGetSelectedRecord, 0, NULL) : NULL;
  const db_checkout_t *row = selected_checkout();
  if ((size_t)len < n && (target || row))
    snprintf(out + len, n - (size_t)len, "   -   %s", target && target->command[0] ? target->command : row ? row->path : "");
}

void gc_overview_open(const char *path) {
  gc_state_t *gc = g_gc;
  if (!gc || !path || !path[0]) { fprintf(stderr, "[gc] overview_open rejected path=%s\n", path ? path : "(null)"); fflush(stderr); return; }
  const git_summary_t *t = NULL;
  for (int i = 0; i < gc->checkout_count; i++) if (!strcmp(gc->checkouts[i].path, path)) t = &gc->checkouts[i];
  if (!t) { fprintf(stderr, "[gc] overview_open: %s is not in the workspace (rows=%d)\n", path, gc->checkout_count); fflush(stderr); return; }
  if (t->missing) { message_box(gc->main_win, ui_tr("This folder is missing or is no longer a git repository."), ui_tr("Open Repository"), MB_OK); return; }
  char open_path[512]; snprintf(open_path, sizeof(open_path), "%s", path);
  bool dirty = row_dirty(t) || t->conflicts;
  gc_open_repo(open_path);
  if (gc->repo) gc_set_view_mode(dirty ? GC_TAB_CHANGES : GC_TAB_HISTORY);
}

bool gc_overview_remove_selected(void) {
  const db_checkout_t *row = selected_checkout();
  if (!row) { fprintf(stderr, "[gc] remove from workspace: no checkout selected\n"); fflush(stderr); return false; }
  char root[512]; snprintf(root, sizeof(root), "%s", row->root);
  if (!gc_workspace_remove(root)) {
    fprintf(stderr, "[gc] remove from workspace: %s is not a workspace root\n", root); fflush(stderr); return false;
  }
  gc_state_t *gc = g_gc;
  int kept = 0;   // the cached rows answer at once; no git call is needed to forget a root
  for (int i = 0; i < gc->checkout_count; i++) if (strcmp(gc->checkouts[i].root, root)) gc->checkouts[kept++] = gc->checkouts[i];
  gc->checkout_count = kept;
  overview_show();
  return true;
}

void gc_overview_fetch_all(void) {
  gc_state_t *gc = g_gc; if (!gc || gc->fetching_all) return;
  char roots[GC_MAX_RECENT_REPOS][512]; int n = 0;
  for (int i = 0; i < gc->checkout_count && n < GC_MAX_RECENT_REPOS; i++) {
    if (gc->checkouts[i].linked || gc->checkouts[i].missing) continue;
    snprintf(roots[n++], sizeof(roots[0]), "%s", gc->checkouts[i].path);
  }
  if (!n) { for (; n < gc->workspace_count && n < GC_MAX_RECENT_REPOS; n++) snprintf(roots[n], sizeof(roots[0]), "%s", gc->workspace[n]); }
  if (git_fetch_all_async(roots, n, gc->main_win)) {
    gc->fetching_all = true;
    char msg[96]; snprintf(msg, sizeof(msg), ui_tr("Fetching %d repositories..."), n);
    send_message(gc->main_win, evStatusBar, 0, msg);
  } else message_box(gc->main_win, ui_tr("There are no repositories to fetch."), ui_tr("Fetch All"), MB_OK);
}

// Handles notifications from the overview controls; returns true when consumed.
bool gc_overview_handle_command(uint32_t wparam, void *lparam) {
  gc_state_t *gc = g_gc; uint16_t code = HIWORD(wparam);
  if (!gc || !lparam) return false;
  window_t *src = lparam;
  if (src == gc->checkouts_win && code == RVN_DBLCLK) {
    const db_checkout_t *row = selected_checkout();
    if (row) gc_overview_open(row->path);
    return true;
  }
  if ((src == gc->checkouts_win || src == gc->targets_win) && code == RVN_SELCHANGE) { gc_update_status(); return true; }
  if (code == btnClicked && src == gc->filter_btn) {
    gc->attention_only = !gc->attention_only;
    rebuild_summary();
    fill_rows();
    window_layout_sync(gc->overview_page_win);
    return true;
  }
  return false;
}
