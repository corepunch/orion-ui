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
#define CHIP_H     18
#define MAX_TILES  64

enum { C_RED = 0xFF5A5AE8, C_AMBER = 0xFF2EA8E5, C_BLUE = 0xFFF0A04C, C_GREEN = 0xFF6AC24C, C_GREY = 0xFF8C8C8C, C_WHITE = 0xFFFFFFFF };
#define TINT(c, a) (((uint32_t)(a) << 24) | ((c) & 0x00FFFFFFu))

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

static void ellipsize(ui_font_t font, const char *src, int max_w, char *out, size_t n) {
  snprintf(out, n, "%s", src);
  if (max_w <= 0) { out[0] = 0; return; }
  if (text_strwidth(font, out) <= max_w) return;
  size_t len = strlen(out);
  while (len > 1) { out[--len] = 0; char tmp[300]; snprintf(tmp, sizeof(tmp), "%s...", out); if (text_strwidth(font, tmp) <= max_w) { snprintf(out, n, "%s", tmp); return; } }
}

static void text_at(ui_font_t font, const char *s, int x, int y, int max_w, uint32_t col) {
  char buf[300]; ellipsize(font, s, max_w, buf, sizeof(buf)); draw_text(font, buf, x, y, col);
}

// Draws a tinted pill; returns its width (0 when it does not fit before max_x).
static int chip(int x, int y, int max_x, const char *label, uint32_t color) {
  int w = text_strwidth(FONT_SMALL, label) + 14; if (x + w > max_x) return 0;
  fill_rounded_rect(TINT(color, 0x40), R(x, y, w, CHIP_H), 5);
  draw_text(FONT_SMALL, label, x + 7, y + (CHIP_H - text_char_height(FONT_SMALL)) / 2, color);
  return w + 5;
}

static void paint_tile(board_t *b, const slot_t *s, int oy, bool focused) {
  const gc_tile_t *t = &b->tiles[s->tile];
  irect16_t r = rect_offset(s->r, 0, -oy); uint32_t col = tile_color(t);
  bool sel = s->tile == b->selected, hot = s->tile == b->hover;
  if (sel) fill_rounded_rect(focused ? get_sys_color(brAccent) : get_sys_color(brTextDisabled), rect_inset(r, -2), 9);
  fill_rounded_rect(hot ? get_sys_color(brButtonHover) : get_sys_color(brControlBg), r, 7);
  fill_rounded_rect(col, R(r.x, r.y + 8, 4, r.h - 16), 2);

  int x = r.x + 16, right = r.x + r.w - 12, ty = r.y + 9, tc = get_sys_color(brTextNormal), dim = get_sys_color(brTextDisabled);
  int ch = text_char_height(FONT_SMALL), sw = text_strwidth(FONT_SMALL, tile_state(t));
  int name_w = right - sw - 12 - x, nw = text_strwidth(FONT_SYSTEM, t->repo); if (nw > name_w) nw = name_w;
  text_at(FONT_SYSTEM, t->repo, x, ty, name_w, tc);
  draw_text(FONT_SMALL, tile_state(t), right - sw, ty + 1, col);
  if (t->linked) {
    const char *d = t->dir; size_t rl = strlen(t->repo); if (!strncmp(d, t->repo, rl) && (d[rl] == '-' || d[rl] == '_')) d += rl + 1;
    char wt[120]; snprintf(wt, sizeof(wt), "/ %s", d);
    int wx = x + nw + 6; text_at(FONT_SMALL, wt, wx, ty + 1, right - sw - 10 - wx, dim);
  }

  int y2 = ty + 24;
  draw_sysicon("git-fork", x - 2, y2 - 2, 16, dim);
  if (t->missing) text_at(FONT_SMALL, "Folder missing or not a repository", x + 18, y2, right - x - 18, dim);
  else {
    char meta[200]; snprintf(meta, sizeof(meta), "%s%s%s", t->branch, t->when[0] ? "  -  " : "", t->when);
    text_at(FONT_SMALL, meta, x + 18, y2, right - x - 18, tc);
    text_at(FONT_SMALL, t->initial ? "No commits yet" : t->subject, x, y2 + ch + 3, right - x, dim);
  }

  int cx = x, cy = r.y + r.h - CHIP_H - 10, mx = right; char c[48];
  if (t->missing) return;
  if (t->conflicts)  { snprintf(c, sizeof(c), "%d conflict%s", t->conflicts, t->conflicts == 1 ? "" : "s"); cx += chip(cx, cy, mx, c, C_RED); }
  if (t->staged)     { snprintf(c, sizeof(c), "%d staged", t->staged);        cx += chip(cx, cy, mx, c, C_GREEN); }
  if (t->unstaged)   { snprintf(c, sizeof(c), "%d modified", t->unstaged);    cx += chip(cx, cy, mx, c, C_AMBER); }
  if (t->untracked)  { snprintf(c, sizeof(c), "%d new", t->untracked);        cx += chip(cx, cy, mx, c, C_AMBER); }
  if (t->ahead)      { snprintf(c, sizeof(c), "%d to push", t->ahead);        cx += chip(cx, cy, mx, c, C_BLUE); }
  if (t->behind)     { snprintf(c, sizeof(c), "%d to pull", t->behind);       cx += chip(cx, cy, mx, c, C_BLUE); }
  if (t->no_upstream && !t->initial) cx += chip(cx, cy, mx, "no upstream", C_BLUE);
  if (t->stashes)    { snprintf(c, sizeof(c), "%d stashed", t->stashes);      cx += chip(cx, cy, mx, c, C_GREY); }
  if (cx == x) chip(cx, cy, mx, "clean", C_GREEN);
}

static void paint_summary(board_t *b, int cw, int oy) {
  int repos = 0, dirty = 0, push = 0, pull = 0, conf = 0; const char *last = NULL;
  for (int i = 0; i < b->count; i++) {
    const gc_tile_t *t = &b->tiles[i];
    if (!last || strcmp(last, t->repo)) { repos++; last = t->repo; }
    if (t->staged || t->unstaged || t->untracked) dirty++;
    if (t->ahead) push++; if (t->behind) pull++; if (t->conflicts) conf++;
  }
  int y = 12 - oy, x = PAD; char s[64];
  snprintf(s, sizeof(s), "%d repositories, %d worktrees", repos, b->count);
  draw_text(FONT_SYSTEM, s, x, y, get_sys_color(brTextNormal)); x += text_strwidth(FONT_SYSTEM, s) + 18;
  int cy = y - 1;
  if (conf) { snprintf(s, sizeof(s), "%d with conflicts", conf); x += chip(x, cy, cw, s, C_RED); }
  if (dirty) { snprintf(s, sizeof(s), "%d uncommitted", dirty);  x += chip(x, cy, cw, s, C_AMBER); }
  if (push)  { snprintf(s, sizeof(s), "%d to push", push);       x += chip(x, cy, cw, s, C_BLUE); }
  if (pull)  { snprintf(s, sizeof(s), "%d to pull", pull);       x += chip(x, cy, cw, s, C_BLUE); }
  if (!conf && !dirty && !push && !pull && b->count) chip(x, cy, cw, "everything is in sync", C_GREEN);

  const char *fl = b->attention_only ? "Showing: needs attention" : "Showing: all";
  int fw = text_strwidth(FONT_SMALL, fl) + 20; irect16_t fr = R(cw - PAD - fw, cy, fw, CHIP_H + 2);
  b->filter_btn = rect_offset(fr, 0, oy);
  fill_rounded_rect(b->attention_only ? get_sys_color(brAccent) : get_sys_color(brButtonInner), fr, 6);
  draw_text(FONT_SMALL, fl, fr.x + 10, fr.y + (fr.h - text_char_height(FONT_SMALL)) / 2, b->attention_only ? C_WHITE : get_sys_color(brTextNormal));
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
