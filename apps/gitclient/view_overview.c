// Overview page: one Card per repository in the workspace.
//
// Each card is a Card (vertical stack) holding a title row, the main checkout's branch, the last commit
// subject and a FlowView of Badges (uncommitted files, worktree count). The TileGrid columns, wraps and
// scrolls them, and owns selection and keyboard navigation.

#include "gitclient.h"
#include <orion/commctl/commctl.h>

#define EDGE_ALPHA 0x78   // calmer than the text colour, closer to the badge tint

// ── verdict ────────────────────────────────────────────────────────────────────

static bool tile_dirty(const git_summary_t *t) {
  return t->staged || t->unstaged || t->untracked || t->dirty_worktrees;
}

static sys_color_idx_t tile_role(const git_summary_t *t) {
  if (t->missing)                                          return brTextSecondary;
  if (t->conflicts)                                        return brTextError;
  if (tile_dirty(t))                                       return brTextWarning;
  if (t->ahead || t->behind || t->no_upstream || t->gone || t->other_attention) return brTextInfo;
  return brTextSuccess;
}

static const char *tile_state(const git_summary_t *t) {
  if (t->missing)                                          return "Unavailable";
  if (t->conflicts)                                        return "Conflicts";
  if (t->staged && !t->unstaged && !t->untracked && t->dirty_worktrees <= 1) return "Ready to commit";
  if (tile_dirty(t))                                       return "Uncommitted";
  if (t->ahead && t->behind)                               return "Diverged";
  if (t->ahead)                                            return "Unpushed";
  if (t->behind)                                           return "Behind";
  if (t->no_upstream && !t->initial)                       return "Unpublished";
  if (t->gone)                                             return "Upstream gone";
  if (t->other_attention)                                  return "Needs attention";
  return "Up to date";
}

// ── view construction ──────────────────────────────────────────────────────────

static window_t *make_view(window_t *parent, winproc_t proc, flags_t orientation, int spacing, const char *tooltip) {
  irect16_t frame = {0, 0, 10, 10};
  window_t *view = create_window(tooltip, 0, &frame, parent, proc, g_gc->hinstance, NULL);
  if (view && proc != win_card) window_set_layout(view, orientation, spacing, (irect16_t){0, 0, 0, 0});
  return view;
}

static window_t *make_label(window_t *parent, const char *text, ui_font_t font, sys_color_idx_t role, bool flex) {
  irect16_t frame = {0, 0, 10, CONTROL_HEIGHT};
  window_t *label = create_window(text, 0, &frame, parent, win_label, g_gc->hinstance, NULL);
  if (!label) return NULL;
  label_create_params_t style = { .color_index = role, .font = font, .color_set = true, .truncate = true };
  send_message(label, lbSetStyle, 0, &style);
  if (flex) label->flags |= WINDOW_FLEXSPACE;
  return label;
}

static window_t *make_badge(window_t *parent, const char *text, sys_color_idx_t role) {
  irect16_t frame = {0, 0, 10, BADGE_HEIGHT};
  window_t *badge = create_window(text, 0, &frame, parent, win_badge, g_gc->hinstance, NULL);
  if (badge) send_message(badge, bdSetColor, role, NULL);
  return badge;
}

static void make_badgef(window_t *parent, sys_color_idx_t role, const char *fmt, int n) {
  char text[48]; snprintf(text, sizeof(text), fmt, n);
  make_badge(parent, text, role);
}

static void build_card(window_t *grid, const git_summary_t *t) {
  char tip[700];
  snprintf(tip, sizeof(tip), "%s\n%s%s%s\n%d worktree%s\nDouble-click to open",
           t->path, t->branch, t->upstream[0] ? " -> " : "", t->upstream,
           t->worktrees, t->worktrees == 1 ? "" : "s");
  window_t *card = make_view(grid, win_card, 0, 0, tip);
  if (!card) return;
  int card_inset = get_theme()->card_ring_width + get_theme()->card_padding_y;
  card->layout.layout_padding = (irect16_t){card_inset, card_inset, card_inset, card_inset};
  sys_color_idx_t verdict = tile_role(t);
  send_message(card, cdSetEdgeColor, 0, (void *)(uintptr_t)color_with_alpha(get_sys_color(verdict), EDGE_ALPHA));

  window_t *header = create_window_from_form(&gitclient_overview_card_header_form, 0, 0, card, win_stack, g_gc->hinstance, NULL);
  if (!header) return;
  char name[256]; snprintf(name, sizeof(name), "%s", t->repo);
  if (t->linked) {
    const char *dir = t->dir; size_t n = strlen(t->repo);
    if (!strncmp(dir, t->repo, n) && (dir[n] == '-' || dir[n] == '_')) dir += n + 1;
    snprintf(name, sizeof(name), "%s / %s", t->repo, dir);
  }
  window_t *title = get_window_item(header, ID_OVERVIEW_CARD_HEADER_TITLE);
  window_t *status = get_window_item(header, ID_OVERVIEW_CARD_HEADER_STATUS);
  window_t *close = get_window_item(header, ID_OVERVIEW_CARD_HEADER_CLOSE);
  set_window_item_text(header, ID_OVERVIEW_CARD_HEADER_TITLE, "%s", name);
  set_window_item_text(header, ID_OVERVIEW_CARD_HEADER_STATUS, "%s", tile_state(t));
  label_create_params_t title_style = {.color_index = brTextNormal, .font = FONT_SYSTEM, .color_set = true, .truncate = true};
  label_create_params_t status_style = {.color_index = verdict, .font = FONT_SMALL, .color_set = true, .truncate = true};
  send_message(title, lbSetStyle, 0, &title_style);
  send_message(status, lbSetStyle, 0, &status_style);
  send_message(close, btnSetIconName, 0, "lucide-x");
  send_message(close, btnSetTooltip, 0, "Close repository");
  close->layout.layout_fixed_w = control_predefined_height(close->flags);
  for (window_t *c = close->parent->children; c; c = c->next) c->layout.v_align = LAYOUT_ALIGN_CENTER;
  close->layout.v_align = LAYOUT_ALIGN_START;

  if (t->missing) { make_label(card, "Folder missing or not a repository", FONT_SMALL, brTextSecondary, false); return; }
  char where[200]; snprintf(where, sizeof(where), "%s%s%s", t->branch, t->when[0] ? "  -  " : "", t->when);
  make_label(card, where, FONT_SMALL, brTextNormal, false);
  make_label(card, t->initial ? "No commits yet" : t->subject, FONT_SMALL, brTextSecondary, false);

  window_t *badges = make_view(card, win_flow, WINDOW_STACK_HORIZONTAL, 5, "");
  if (t->conflicts) make_badgef(badges, brTextError,   t->conflicts == 1 ? "%d conflict" : "%d conflicts", t->conflicts);
  if (t->staged)    make_badgef(badges, brTextSuccess, "%d staged",   t->staged);
  if (t->unstaged)  make_badgef(badges, brTextWarning, "%d modified", t->unstaged);
  if (t->untracked) make_badgef(badges, brTextWarning, "%d new",      t->untracked);
  if (t->ahead)     make_badgef(badges, brTextInfo,    "%d to push",  t->ahead);
  if (t->behind)    make_badgef(badges, brTextInfo,    "%d to pull",  t->behind);
  if (t->no_upstream && !t->initial) make_badge(badges, "no upstream", brTextInfo);
  if (t->stashes)   make_badgef(badges, brTextSecondary, "%d stashed", t->stashes);
  if (t->worktrees > 0) make_badgef(badges, brTextSecondary, t->worktrees == 1 ? "%d worktree" : "%d worktrees", t->worktrees);
  if (t->dirty_worktrees > 1) make_badgef(badges, brTextWarning, "%d dirty", t->dirty_worktrees);
  else if (t->dirty_worktrees == 1 && !t->staged && !t->unstaged && !t->untracked && !t->conflicts)
    make_badge(badges, "uncommitted", brTextWarning);
  if (!badges->children) make_badge(badges, "clean", brTextSuccess);
}

static void rebuild_summary(void) {
  gc_state_t *gc = g_gc;
  if (!gc->summary_win) return;
  while (gc->summary_win->children) destroy_window(gc->summary_win->children);
  int repos = 0, trees = 0, dirty = 0, push = 0, pull = 0, conf = 0;
  for (int i = 0; i < gc->tile_count; i++) {
    const git_summary_t *t = &gc->tiles[i];
    repos++; trees += t->worktrees;
    dirty += tile_dirty(t) ? 1 : 0; push += t->ahead ? 1 : 0; pull += t->behind ? 1 : 0; conf += t->conflicts ? 1 : 0;
  }
  char text[80]; snprintf(text, sizeof(text), "%d repositories, %d worktrees", repos, trees);
  window_t *summary = gc->summary_win;
  make_label(summary, text, FONT_SYSTEM, brTextNormal, false);
  if (conf)  make_badgef(summary, brTextError,   "%d with conflicts", conf);
  if (dirty) make_badgef(summary, brTextWarning, "%d uncommitted",    dirty);
  if (push)  make_badgef(summary, brTextInfo,    "%d to push",        push);
  if (pull)  make_badgef(summary, brTextInfo,    "%d to pull",        pull);
  if (!conf && !dirty && !push && !pull && gc->tile_count) make_badge(summary, "everything is in sync", brTextSuccess);
  irect16_t space = {0, 0, 1, 1};
  window_t *spring = create_window("", 0, &space, summary, win_space, gc->hinstance, NULL);
  if (spring) spring->flags |= WINDOW_FLEXSPACE;
  irect16_t frame = {0, 0, 150, BUTTON_HEIGHT};
  gc->filter_btn = create_window(gc->attention_only ? "Showing: needs attention" : "Showing: all", BUTTON_PUSHLIKE, &frame,
                                 summary, win_button, gc->hinstance, NULL);
  if (gc->filter_btn) { gc->filter_btn->value = gc->attention_only; gc->filter_btn->layout.layout_fixed_w = 170; }
  for (window_t *c = summary->children; c; c = c->next) c->layout.v_align = LAYOUT_ALIGN_CENTER;
  window_layout_sync(summary);
}

static void rebuild_board(void) {
  gc_state_t *gc = g_gc;
  if (!gc->board_win) return;
  char keep[512] = "";
  int sel = (int)send_message(gc->board_win, tgGetSelection, 0, NULL);
  if (sel >= 0 && sel < GC_MAX_TILES) snprintf(keep, sizeof(keep), "%s", gc->tiles[gc->visible_tiles[sel]].path);
  send_message(gc->board_win, tgClear, 0, NULL);
  int shown = 0, restore = 0;
  for (int i = 0; i < gc->tile_count; i++) {
    if (gc->attention_only && !gc_tile_needs_attention(&gc->tiles[i])) continue;
    if (keep[0] && !strcmp(keep, gc->tiles[i].path)) restore = shown;
    gc->visible_tiles[shown++] = i;
    build_card(gc->board_win, &gc->tiles[i]);
  }
  window_layout_sync(gc->board_win);
  if (shown) send_message(gc->board_win, tgSetSelection, (uint32_t)restore, NULL);
}

// ── page ───────────────────────────────────────────────────────────────────────

result_t gc_page_overview_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (msg != evCreate || !g_gc) return false;
  g_gc->summary_win = get_window_item(win, ID_OVERVIEW_PAGE_SUMMARY);
  g_gc->board_win   = get_window_item(win, ID_OVERVIEW_PAGE_BOARD);
  return true;
}

static int tile_cmp(const void *pa, const void *pb) {
  const git_summary_t *a = pa, *b = pb;
  int c = strcasecmp(a->repo, b->repo); if (c) return c;
  if (a->linked != b->linked) return a->linked ? 1 : -1;
  return strcasecmp(a->dir, b->dir);
}

void gc_overview_refresh(void) {
  gc_state_t *gc = g_gc; if (!gc) return;
  gc->tile_count = git_workspace_scan(gc->workspace, gc->workspace_count, gc->tiles, GC_MAX_TILES);
  qsort(gc->tiles, (size_t)gc->tile_count, sizeof(gc->tiles[0]), tile_cmp);
  rebuild_summary();
  rebuild_board();
  window_layout_sync(gc->overview_page_win);   // the strip's height comes from its children
  if (gc->tab == GC_TAB_OVERVIEW) gc_update_status();
}

void gc_overview_status(char *out, size_t n) {
  gc_state_t *gc = g_gc; int trees = 0, dirty = 0, push = 0, pull = 0, conf = 0;
  for (int i = 0; i < gc->tile_count; i++) {
    const git_summary_t *t = &gc->tiles[i];
    trees += t->worktrees; dirty += tile_dirty(t) ? 1 : 0; push += t->ahead ? 1 : 0; pull += t->behind ? 1 : 0; conf += t->conflicts ? 1 : 0;
  }
  int sel = gc->board_win ? (int)send_message(gc->board_win, tgGetSelection, 0, NULL) : -1;
  int len = snprintf(out, n, "%d repositories, %d worktrees | %d uncommitted | %d to push | %d to pull%s",
                     gc->tile_count, trees, dirty, push, pull, conf ? " | CONFLICTS" : "");
  if (sel >= 0 && sel < gc->tile_count && len > 0 && (size_t)len < n)
    snprintf(out + len, n - (size_t)len, "   -   %s", gc->tiles[gc->visible_tiles[sel]].path);
}

void gc_overview_open(int index) {
  gc_state_t *gc = g_gc;
  if (!gc || index < 0 || index >= gc->tile_count) {
    fprintf(stderr, "[gc] overview_open rejected index=%d count=%d\n", index, gc ? gc->tile_count : -1); fflush(stderr); return;
  }
  const git_summary_t *t = &gc->tiles[index];
  if (t->missing) { message_box(gc->main_win, "This folder is missing or is no longer a git repository.", "Open Repository", MB_OK); return; }
  char path[512]; snprintf(path, sizeof(path), "%s", t->path);
  bool dirty = t->staged || t->unstaged || t->untracked || t->conflicts;
  gc_open_repo(path);
  if (gc->repo) gc_set_view_mode(dirty ? GC_TAB_CHANGES : GC_TAB_HISTORY);
}

void gc_overview_fetch_all(void) {
  gc_state_t *gc = g_gc; if (!gc || gc->fetching_all) return;
  char roots[GC_MAX_RECENT_REPOS][512]; int n = 0;
  for (int i = 0; i < gc->tile_count && n < GC_MAX_RECENT_REPOS; i++) {
    if (gc->tiles[i].linked || gc->tiles[i].missing) continue;
    snprintf(roots[n++], sizeof(roots[0]), "%s", gc->tiles[i].path);
  }
  if (!n) { for (; n < gc->workspace_count && n < GC_MAX_RECENT_REPOS; n++) snprintf(roots[n], sizeof(roots[0]), "%s", gc->workspace[n]); }
  if (git_fetch_all_async(roots, n, gc->main_win)) {
    gc->fetching_all = true;
    char msg[96]; snprintf(msg, sizeof(msg), "Fetching %d repositories...", n);
    send_message(gc->main_win, evStatusBar, 0, msg);
  } else message_box(gc->main_win, "There are no repositories to fetch.", "Fetch All", MB_OK);
}

// Handles notifications from the overview controls; returns true when consumed.
bool gc_overview_handle_command(uint32_t wparam, void *lparam) {
  gc_state_t *gc = g_gc; uint16_t code = HIWORD(wparam);
  if (!gc || !lparam) return false;
  if (code == btnClicked && LOWORD(wparam) == ID_OVERVIEW_CARD_HEADER_CLOSE) {
    window_t *card = lparam;
    while (card && card->parent != gc->board_win) card = card->parent;
    if (!card || !gc->board_win) return false;
    int index = 0;
    for (window_t *c = gc->board_win->children; c && c != card; c = c->next) index++;
    if (index >= gc->tile_count) return true;
    char path[512]; snprintf(path, sizeof(path), "%s", gc->tiles[gc->visible_tiles[index]].path);
    if (gc_workspace_remove(path)) {
      send_message(gc->board_win, tgClear, 0, NULL);
      gc_overview_refresh();
    }
    return true;
  }
  if ((code == tgnSelChange || code == tgnActivate) && (window_t *)lparam == gc->board_win) {
    int index = (int)LOWORD(wparam);
    if (index < 0 || index >= gc->tile_count) return true;
    if (code == tgnActivate) gc_overview_open(gc->visible_tiles[index]); else gc_update_status();
    return true;
  }
  if (code == btnClicked && (window_t *)lparam == gc->filter_btn) {
    gc->attention_only = !gc->attention_only;
    rebuild_summary();
    rebuild_board();
    window_layout_sync(gc->overview_page_win);
    return true;
  }
  return false;
}
