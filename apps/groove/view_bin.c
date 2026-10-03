// VIEW: one page of the block bin — the tiles of a single instrument family.
// Pressing a tile auditions it; dragging starts a drop onto the sheet.

#include "groove.h"

#define TILE_H    56
#define TILE_GAP  8
#define TILE_PAD  12
#define BAR_TILE_W 104
#define BIN_SLOP 5
#define MAX_TILES 32

typedef struct {
  category_t cat;
  int ids[MAX_TILES], count;
  irect16_t rects[MAX_TILES];
  int hover, press;
  ipoint16_t press_pt;
  bool dragging;
} bin_t;

static int vpos(window_t *win) { return get_scroll_pos(win, SB_VERT); }

// Rects are in content space; wraps to a new row when the next tile would overflow the width.
static void layout_tiles(window_t *win, bin_t *st) {
  irect16_t cr = get_client_rect(win);
  int x = TILE_PAD, y = TILE_PAD;
  for (int i = 0; i < st->count; i++) {
    int w = BAR_TILE_W + (block_get(st->ids[i])->bars - 1) * 56;
    if (x + w > cr.w - TILE_PAD && x > TILE_PAD) { x = TILE_PAD; y += TILE_H + TILE_GAP; }
    st->rects[i] = R(x, y, w, TILE_H);
    x += w + TILE_GAP;
  }
  int content_h = (st->count ? st->rects[st->count - 1].y + TILE_H : 0) + TILE_PAD;
  scroll_info_t si = { .fMask = SIF_RANGE | SIF_PAGE | SIF_POS, .nMin = 0, .nMax = content_h, .nPage = cr.h, .nPos = vpos(win) };
  set_scroll_info(win, SB_VERT, &si, false);
}

static int hit_tile(const bin_t *st, int mx, int my) {
  for (int i = 0; i < st->count; i++)
    if (rect_contains_point(st->rects[i], (ipoint16_t){ (int16_t)mx, (int16_t)my })) return i;
  return -1;
}

static void paint_bin(window_t *win, bin_t *st) {
  fill_rect(get_sys_color(brControlBg), get_client_rect(win));
  layout_tiles(win, st);
  for (int i = 0; i < st->count; i++) {
    const block_t *b = block_get(st->ids[i]);
    irect16_t r = rect_offset(st->rects[i], 0, -vpos(win));
    bool lit = i == st->hover || (st->dragging && i == st->press);
    draw_clip(win, b, r, lit ? color_with_alpha(category_color(b->cat), 0xd8) : category_color(b->cat), false);
  }
}

static void end_drag(window_t *win, bin_t *st, bool drop, int sx, int sy) {
  if (st->dragging) send_message(g_app->sheet, drop ? shDrop : shDragEnd, MAKEDWORD(sx, sy), NULL);
  else if (drop && st->press >= 0) app_preview(st->ids[st->press]);
  st->press = -1;
  st->dragging = false;
  set_capture(NULL);
  invalidate_window(win);
}

result_t win_bin(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  bin_t *st = win->userdata;
  if (!g_app && msg != evDestroy) return false;
  switch (msg) {
    case evCreate:
      if (!(st = allocate_window_data(win, sizeof(*st)))) { fprintf(stderr, "[bin] allocation failed win=%u\n", (unsigned)win->id); fflush(stderr); return false; }
      st->cat = (category_t)(intptr_t)lparam;
      st->count = blocks_in_category(st->cat, st->ids, MAX_TILES);
      st->hover = st->press = -1;
      return true;
    case evPaint: paint_bin(win, st); return true;
    case evVScroll: invalidate_window(win); return true;
    case evResize: invalidate_window(win); return false;
    case evMouseMove: {
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
      if (st->press >= 0) {
        if (!st->dragging && abs(mx - st->press_pt.x) + abs(my - st->press_pt.y) > BIN_SLOP) {
          st->dragging = true;
          g_app->drag = (drag_t){ .active = true, .block = st->ids[st->press], .from_clip = -1, .track = -1 };
          GR_TRACE("bin drag start win=%u block=%d", (unsigned)win->id, g_app->drag.block);
        }
        if (st->dragging) send_message(g_app->sheet, shDragOver, MAKEDWORD(window_screen_x(win) + mx, window_screen_y(win) + my - vpos(win)), NULL);
        return true;
      }
      int h = hit_tile(st, mx, my);
      if (h != st->hover) { st->hover = h; invalidate_window(win); }
      track_mouse(win);
      return true;
    }
    case evMouseLeave: st->hover = -1; invalidate_window(win); return true;
    case evLeftButtonDown: {
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam), h = hit_tile(st, mx, my);
      GR_TRACE("bin down win=%u mx=%d my=%d tile=%d", (unsigned)win->id, mx, my, h);
      if (h < 0) return false;
      st->press = h;
      st->press_pt = (ipoint16_t){ (int16_t)mx, (int16_t)my };
      set_capture(win);
      invalidate_window(win);
      return true;
    }
    case evLeftButtonUp:
      if (st->press < 0) return false;
      end_drag(win, st, true, window_screen_x(win) + (int16_t)LOWORD(wparam), window_screen_y(win) + (int16_t)HIWORD(wparam) - vpos(win));
      return true;
    case evPointerCancel:
      if (st->press >= 0) end_drag(win, st, false, 0, 0);
      return true;
    case evDestroy:
      if (st && st->press >= 0) set_capture(NULL);
      free(st);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}
