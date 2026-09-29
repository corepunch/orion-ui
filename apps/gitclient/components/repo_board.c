// RepoBoard — tiled multi-repository / multi-worktree overview.
// Shows only what needs a decision: uncommitted files and unpushed/unpulled commits.

#include "repo_board.h"
#include <orion/user/rect.h>

#define TILE_MIN_W 232
#define TILE_MAX_W 340
#define TILE_H     104
#define GAP        10
#define PAD        14
#define SUMMARY_H  44
#define BADGE_H    18
#define MAX_TILES  64
#define TILE_RADIUS 7
#define STRIPE_W    4
#define TILE_PAD    12
#define SELECT_RING 2

// Status semantics come from the active theme, never from literal colours.
#define C_RED   get_sys_color(brTextError)
#define C_AMBER get_sys_color(brTextWarning)
#define C_BLUE  get_sys_color(brTextInfo)
#define C_GREEN get_sys_color(brTextSuccess)
#define C_GREY  get_sys_color(brTextDisabled)

typedef struct { irect16_t r; int tile; } slot_t;

typedef struct {
  gc_tile_t tiles[MAX_TILES]; int count;
  int  selected, hover;
  bool attention_only;
  slot_t slots[MAX_TILES];   int slot_count;
  irect16_t filter_btn; int total_h;
} board_t;

static uint32_t tile_color(const gc_tile_t *t) {
  if (t->missing)                                   return C_GREY;
  if (t->conflicts)                                 return C_RED;
  if (t->staged || t->unstaged || t->untracked)     return C_AMBER;
  if (t->ahead || t->behind || t->no_upstream || t->gone) return C_BLUE;
  return C_GREEN;
}

static const char *tile_state(const gc_tile_t *t) {
  if (t->missing)                                   return "Unavailable";
  if (t->conflicts)                                 return "Conflicts";
  if (t->staged && !t->unstaged && !t->untracked)   return "Ready to commit";
  if (t->staged || t->unstaged || t->untracked)     return "Uncommitted";
  if (t->ahead && t->behind)                        return "Diverged";
  if (t->ahead)                                     return "Unpushed";
  if (t->behind)                                    return "Behind";
  if (t->no_upstream && !t->initial)                return "Unpublished";
  if (t->gone)                                      return "Upstream gone";
  return "Up to date";
}

static bool visible(const board_t *b, int i) { return !b->attention_only || gc_tile_needs_attention(&b->tiles[i]); }

static int columns(int cw) { int c = (cw - 2 * PAD + GAP) / (TILE_MIN_W + GAP); return c < 1 ? 1 : c; }

static int content_width(window_t *win) {
  irect16_t cr = get_client_rect(win); return cr.w;
}

static void layout(window_t *win, board_t *b) {
  int cw = content_width(win), cols = columns(cw);
  int tw = (cw - 2 * PAD - (cols - 1) * GAP) / cols; if (tw > TILE_MAX_W) tw = TILE_MAX_W; if (tw < 120) tw = 120;
  b->slot_count = 0; int n = 0;
  for (int i = 0; i < b->count; i++) {
    if (!visible(b, i)) continue;
    b->slots[b->slot_count++] = (slot_t){ R(PAD + (n % cols) * (tw + GAP), SUMMARY_H + (n / cols) * (TILE_H + GAP), tw, TILE_H), i };
    n++;
  }
  b->total_h = n ? SUMMARY_H + ((n + cols - 1) / cols) * (TILE_H + GAP) + PAD : SUMMARY_H + 120;
}

static void sync_scroll(window_t *win, board_t *b) {
  irect16_t cr = get_client_rect(win);
  int max_y = b->total_h - cr.h; if (max_y < 0) max_y = 0;
  if ((int)win->vscroll.pos > max_y) win->vscroll.pos = (uint32_t)max_y;
  scroll_info_t si = { .fMask = SIF_ALL, .nMin = 0, .nMax = b->total_h, .nPage = cr.h, .nPos = (int)win->vscroll.pos };
  set_scroll_info(win, SB_VERT, &si, false);
}

static int hit_slot(const board_t *b, int x, int y) {
  for (int i = 0; i < b->slot_count; i++) if (rect_contains_point(b->slots[i].r, (ipoint16_t){x, y})) return b->slots[i].tile;
  return -1;
}

static void notify(window_t *win, int tile, int code) {
  window_t *root = get_root_window(win);
  fprintf(stderr, "[rb] notify win=%u tile=%d code=%d\n", (unsigned)win->id, tile, code); fflush(stderr);
  if (root) send_message(root, evCommand, MAKEDWORD((uint16_t)tile, (uint16_t)code), win);
}

static void ensure_visible(window_t *win, board_t *b, int tile) {
  layout(win, b); irect16_t cr = get_client_rect(win);
  for (int i = 0; i < b->slot_count; i++) if (b->slots[i].tile == tile) {
    irect16_t r = b->slots[i].r; int top = r.y - GAP, bot = r.y + r.h + GAP, pos = (int)win->vscroll.pos;
    if (top < pos) pos = top < SUMMARY_H ? 0 : top; else if (bot > pos + cr.h) pos = bot - cr.h;
    win->vscroll.pos = (uint32_t)(pos < 0 ? 0 : pos); break;
  }
  sync_scroll(win, b);
}

static void select_tile(window_t *win, board_t *b, int tile, bool announce) {
  if (tile < 0 || tile >= b->count) return;
  bool changed = tile != b->selected; b->selected = tile;
  ensure_visible(win, b, tile); invalidate_window(win);
  if (announce && changed) notify(win, tile, GC_BOARD_SELECT);
}

static void move_selection(window_t *win, board_t *b, int dx, int dy) {
  layout(win, b); if (!b->slot_count) return;
  int cur = -1; for (int i = 0; i < b->slot_count; i++) if (b->slots[i].tile == b->selected) cur = i;
  if (cur < 0) { select_tile(win, b, b->slots[0].tile, true); return; }
  irect16_t c = b->slots[cur].r; int best = -1, best_score = 1 << 30;
  for (int i = 0; i < b->slot_count; i++) {
    if (i == cur) continue; irect16_t r = b->slots[i].r; int score;
    if (dx) { if ((dx > 0) != (i > cur)) continue; score = abs(i - cur); }
    else { int ddy = (r.y - c.y) * dy; if (ddy <= 0) continue; score = ddy * 4 + abs(r.x - c.x); }
    if (score < best_score) { best_score = score; best = i; }
  }
  if (best >= 0) select_tile(win, b, b->slots[best].tile, true);
}

// ── painting ────────────────────────────────────────────────────────────────

static int badge(irect16_t row, int x, const char *label, uint32_t color) {
  int w = text_strwidth(FONT_SMALL, label) + 14;
  if (x + w > row.x + row.w) return 0;
  return draw_badge(FONT_SMALL, label, x, row.y, row.h, color) + 5;
}

static void paint_tile(board_t *b, const slot_t *s, int oy, bool focused) {
  const gc_tile_t *t = &b->tiles[s->tile];
  irect16_t card = rect_offset(s->r, 0, -oy); uint32_t verdict = tile_color(t);
  bool selected = s->tile == b->selected, hot = s->tile == b->hover;
  uint32_t text = get_sys_color(brTextNormal), dim = get_sys_color(brTextDisabled);

  if (selected) fill_rounded_rect(focused ? get_sys_color(brAccent) : dim, rect_inset(card, -SELECT_RING), TILE_RADIUS + SELECT_RING);
  // The verdict stripe spans the full card height and follows its rounded left corners; the body
  // covers everything to its right and keeps the card's right corners.
  fill_rounded_rect_corners(verdict, rect_split_left(card, 2 * TILE_RADIUS), TILE_RADIUS, CORNERS_LEFT);
  fill_rounded_rect_corners(get_sys_color(hot ? brButtonHover : brControlBg), rect_trim_left(card, STRIPE_W), TILE_RADIUS, CORNERS_RIGHT);

  irect16_t body = rect_inset_xy(rect_trim_left(card, STRIPE_W), TILE_PAD, 9);
  int line = text_char_height(FONT_SMALL), state_w = text_strwidth(FONT_SMALL, tile_state(t));
  irect16_t title = rect_trim_right(rect_split_top(body, text_char_height(FONT_SYSTEM)), state_w + 12);
  draw_text_ellipsized(FONT_SYSTEM, t->repo, title.x, title.y, title.w, text);
  draw_text(FONT_SMALL, tile_state(t), body.x + body.w - state_w, title.y + 1, verdict);
  if (t->linked) {
    const char *dir = t->dir; size_t n = strlen(t->repo);
    if (!strncmp(dir, t->repo, n) && (dir[n] == '-' || dir[n] == '_')) dir += n + 1;
    char suffix[120]; snprintf(suffix, sizeof(suffix), "/ %s", dir);
    int used = MIN(text_strwidth(FONT_SYSTEM, t->repo), title.w) + 6;
    draw_text_ellipsized(FONT_SMALL, suffix, title.x + used, title.y + 1, title.w - used, dim);
  }

  irect16_t where = R(body.x, title.y + 24, body.w, line);
  draw_sysicon("git-fork", where.x - 2, where.y - 2, 16, dim);
  irect16_t meta = rect_trim_left(where, 18);
  if (t->missing) { draw_text_ellipsized(FONT_SMALL, "Folder missing or not a repository", meta.x, meta.y, meta.w, dim); return; }
  char branch[200]; snprintf(branch, sizeof(branch), "%s%s%s", t->branch, t->when[0] ? "  -  " : "", t->when);
  draw_text_ellipsized(FONT_SMALL, branch, meta.x, meta.y, meta.w, text);
  draw_text_ellipsized(FONT_SMALL, t->initial ? "No commits yet" : t->subject, where.x, where.y + line + 3, where.w, dim);

  irect16_t row = rect_split_bottom(body, BADGE_H); row.y -= 1;
  int x = row.x; char c[48];
  if (t->conflicts) { snprintf(c, sizeof(c), "%d conflict%s", t->conflicts, t->conflicts == 1 ? "" : "s"); x += badge(row, x, c, C_RED); }
  if (t->staged)    { snprintf(c, sizeof(c), "%d staged",   t->staged);    x += badge(row, x, c, C_GREEN); }
  if (t->unstaged)  { snprintf(c, sizeof(c), "%d modified", t->unstaged);  x += badge(row, x, c, C_AMBER); }
  if (t->untracked) { snprintf(c, sizeof(c), "%d new",      t->untracked); x += badge(row, x, c, C_AMBER); }
  if (t->ahead)     { snprintf(c, sizeof(c), "%d to push",  t->ahead);     x += badge(row, x, c, C_BLUE); }
  if (t->behind)    { snprintf(c, sizeof(c), "%d to pull",  t->behind);    x += badge(row, x, c, C_BLUE); }
  if (t->no_upstream && !t->initial) x += badge(row, x, "no upstream", C_BLUE);
  if (t->stashes)   { snprintf(c, sizeof(c), "%d stashed",  t->stashes);   x += badge(row, x, c, C_GREY); }
  if (x == row.x) badge(row, x, "clean", C_GREEN);
}

static void paint_summary(board_t *b, int cw, int oy) {
  int repos = 0, dirty = 0, push = 0, pull = 0, conf = 0; const char *last = NULL;
  for (int i = 0; i < b->count; i++) {
    const gc_tile_t *t = &b->tiles[i];
    if (!last || strcmp(last, t->repo)) { repos++; last = t->repo; }
    if (t->staged || t->unstaged || t->untracked) dirty++;
    if (t->ahead) push++; if (t->behind) pull++; if (t->conflicts) conf++;
  }
  irect16_t strip = R(PAD, 12 - oy, cw - 2 * PAD, BADGE_H + 2); char s[64];
  snprintf(s, sizeof(s), "%d repositories, %d worktrees", repos, b->count);
  draw_text(FONT_SYSTEM, s, strip.x, strip.y + 1, get_sys_color(brTextNormal));
  int x = strip.x + text_strwidth(FONT_SYSTEM, s) + 18;
  if (conf)  { snprintf(s, sizeof(s), "%d with conflicts", conf); x += badge(strip, x, s, C_RED) ; }
  if (dirty) { snprintf(s, sizeof(s), "%d uncommitted", dirty);   x += badge(strip, x, s, C_AMBER); }
  if (push)  { snprintf(s, sizeof(s), "%d to push", push);        x += badge(strip, x, s, C_BLUE); }
  if (pull)  { snprintf(s, sizeof(s), "%d to pull", pull);        x += badge(strip, x, s, C_BLUE); }
  if (!conf && !dirty && !push && !pull && b->count) badge(strip, x, "everything is in sync", C_GREEN);

  const char *label = b->attention_only ? "Showing: needs attention" : "Showing: all";
  int fw = text_strwidth(FONT_SMALL, label) + 20;
  irect16_t pill = rect_split_right(strip, fw);
  b->filter_btn = rect_offset(pill, 0, oy);
  fill_rounded_rect(get_sys_color(b->attention_only ? brAccent : brButtonInner), pill, 6);
  draw_text(FONT_SMALL, label, pill.x + 10, pill.y + (pill.h - text_char_height(FONT_SMALL)) / 2,
            get_sys_color(b->attention_only ? brActiveTitlebarText : brTextNormal));
}

static void paint_board(window_t *win, board_t *b) {
  irect16_t cr = get_client_rect(win); int oy = (int)win->vscroll.pos;
  fill_rect(get_sys_color(brPanelDark), cr);
  layout(win, b); sync_scroll(win, b); oy = (int)win->vscroll.pos;
  paint_summary(b, cr.w, oy);
  if (!b->count) {
    const char *m = "No repositories yet";
    const char *h = "Use File > Repositories... to add a folder or scan for repositories.";
    draw_text(FONT_SYSTEM, m, (cr.w - text_strwidth(FONT_SYSTEM, m)) / 2, cr.h / 2 - 12, get_sys_color(brTextNormal));
    draw_text(FONT_SMALL, h, (cr.w - text_strwidth(FONT_SMALL, h)) / 2, cr.h / 2 + 10, get_sys_color(brTextDisabled));
    return;
  }
  if (!b->slot_count) {
    const char *m = "Nothing needs attention. Every worktree is committed and in sync.";
    draw_text(FONT_SYSTEM, m, (cr.w - text_strwidth(FONT_SYSTEM, m)) / 2, cr.h / 2 - 8, get_sys_color(brTextNormal));
    return;
  }
  bool focused = window_has_focus(win);
  for (int i = 0; i < b->slot_count; i++) {
    irect16_t r = rect_offset(b->slots[i].r, 0, -oy);
    if (r.y + r.h < 0 || r.y > cr.h) continue;
    paint_tile(b, &b->slots[i], oy, focused);
  }
}

// ── window proc ─────────────────────────────────────────────────────────────

result_t gc_repo_board_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  board_t *b = (board_t *)win->userdata;
  switch (msg) {
    case evCreate:
      b = calloc(1, sizeof(*b)); if (!b) { fprintf(stderr, "[rb] alloc failed win=%u\n", (unsigned)win->id); fflush(stderr); return false; }
      win->userdata = b; b->selected = -1; b->hover = -1;
      win->flags |= WINDOW_VSCROLL; win->vscroll.visible_mode = SB_VIS_AUTO;
      return true;
    case evDestroy: free(b); win->userdata = NULL; return false;
    case evArrange: {
      layout_arrange_t const *a = (layout_arrange_t const *)lparam;
      if (a) { irect16_t r = a->rect; if (r.w < 1) r.w = 1; if (r.h < 1) r.h = 1; win->frame = r; }
      invalidate_window(win); return true;
    }
    case evResize: if (b) { layout(win, b); sync_scroll(win, b); } invalidate_window(win); return false;
    case evVScroll: win->vscroll.pos = wparam; invalidate_window(win); return true;
    case evPaint: if (!b) return true; paint_board(win, b); return true;

    case rbSetTiles: {
      if (!b) return false;
      int n = (int)wparam; if (n > MAX_TILES) { fprintf(stderr, "[rb] win=%u rbSetTiles count=%d exceeds %d\n", (unsigned)win->id, n, MAX_TILES); fflush(stderr); n = MAX_TILES; }
      char keep[512] = ""; if (b->selected >= 0 && b->selected < b->count) snprintf(keep, sizeof(keep), "%s", b->tiles[b->selected].path);
      if (n > 0 && lparam) memcpy(b->tiles, lparam, (size_t)n * sizeof(gc_tile_t)); else n = 0;
      b->count = n; b->selected = -1; b->hover = -1;
      for (int i = 0; i < n; i++) if (keep[0] && !strcmp(keep, b->tiles[i].path)) b->selected = i;
      if (b->selected < 0 && n > 0) b->selected = 0;
      layout(win, b); sync_scroll(win, b); invalidate_window(win); return true;
    }
    case rbGetSelection: return b ? (result_t)b->selected : (result_t)-1;
    case rbSetSelection: if (b) select_tile(win, b, (int)wparam, false); return true;
    case rbSetFilter: if (b) { b->attention_only = wparam != 0; layout(win, b); sync_scroll(win, b); invalidate_window(win); } return true;

    case evMouseMove: {
      if (!b) return false; int h = hit_slot(b, (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam));
      if (h != b->hover) { b->hover = h; invalidate_window(win); }
      track_mouse(win); return false;
    }
    case evMouseLeave: if (b && b->hover >= 0) { b->hover = -1; invalidate_window(win); } return false;
    case evLeftButtonDown: {
      if (!b) return false; set_focus(win);
      int x = (int16_t)LOWORD(wparam), y = (int16_t)HIWORD(wparam);
      if (rect_contains_point(b->filter_btn, (ipoint16_t){x, y})) { b->attention_only = !b->attention_only; layout(win, b); sync_scroll(win, b); invalidate_window(win); return true; }
      int h = hit_slot(b, x, y);
      if (h >= 0) select_tile(win, b, h, true);
      return true;
    }
    case evLeftButtonDoubleClick: {
      if (!b) return false; int h = hit_slot(b, (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam));
      if (h >= 0) { select_tile(win, b, h, true); notify(win, h, GC_BOARD_OPEN); }
      return true;
    }
    case evGetTooltipText: {
      if (!b || !lparam) return false; int h = hit_slot(b, (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)); if (h < 0) return false;
      const gc_tile_t *t = &b->tiles[h];
      snprintf((char *)lparam, 256, "%s\n%s%s%s\nDouble-click to open", t->path, t->branch, t->upstream[0] ? " -> " : "", t->upstream);
      return true;
    }
    case evKeyDown: {
      if (!b) return false;
      switch (wparam) {
        case AX_KEY_LEFTARROW:  move_selection(win, b, -1, 0); return true;
        case AX_KEY_RIGHTARROW: move_selection(win, b,  1, 0); return true;
        case AX_KEY_UPARROW:    move_selection(win, b, 0, -1); return true;
        case AX_KEY_DOWNARROW:  move_selection(win, b, 0,  1); return true;
        case AX_KEY_ENTER:      if (b->selected >= 0) notify(win, b->selected, GC_BOARD_OPEN); return true;
        default: return false;
      }
    }
    default: return false;
  }
}
