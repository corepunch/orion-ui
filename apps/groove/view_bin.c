// VIEW: one page of the block bin — the tiles of a single instrument family.
// Each tile is a child window. Pressing it auditions the block; dragging lifts
// that same window with window_set_drag_visual.

#include "groove.h"

#define TILE_H     56
#define TILE_GAP   8
#define TILE_PAD   12
#define BAR_TILE_W 104
#define BIN_SLOP   5
#define MAX_TILES  32

typedef struct {
  category_t cat;
  int ids[MAX_TILES], count;
} bin_t;

typedef struct {
  int block;
  ipoint16_t press;
  bool down, hover;
} tile_t;

static int vpos(window_t *win) { return get_scroll_pos(win, SB_VERT); }

static void tile_screen(window_t *win, int mx, int my, int *sx, int *sy) {
  *sx = window_screen_x(win) + mx;
  *sy = window_screen_y(win) + my;
}

// Content-space rows, frames in the bin's viewport (content y minus scroll).
static void layout_tiles(window_t *win, bin_t *st) {
  irect16_t cr = get_client_rect(win);
  int x = TILE_PAD, y = TILE_PAD, i = 0, bottom = 0;
  for (window_t *c = win->children; c && i < st->count; c = c->next, i++) {
    int w = BAR_TILE_W + (block_get(st->ids[i])->bars - 1) * 56;
    if (x + w > cr.w - TILE_PAD && x > TILE_PAD) { x = TILE_PAD; y += TILE_H + TILE_GAP; }
    card_place(c, R(x, y - vpos(win), w, TILE_H));
    bottom = y + TILE_H;
    x += w + TILE_GAP;
  }
  int content_h = (st->count ? bottom : 0) + TILE_PAD;
  scroll_info_t si = { .fMask = SIF_RANGE | SIF_PAGE | SIF_POS, .nMin = 0, .nMax = content_h, .nPage = cr.h, .nPos = vpos(win) };
  set_scroll_info(win, SB_VERT, &si, false);
}

static result_t win_tile(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  tile_t *st = win->userdata;
  if (!g_app && msg != evDestroy) return false;
  switch (msg) {
    case evCreate:
      if (!(st = allocate_window_data(win, sizeof(*st)))) {
        fprintf(stderr, "[bin] tile allocation failed win=%u\n", (unsigned)win->id);
        fflush(stderr);
        return false;
      }
      st->block = (int)(intptr_t)lparam;
      return true;
    case evPaint: {
      const block_t *b = block_get(st->block);
      bool lit = st->hover && !win->drag_visual;
      draw_clip(win, b, get_client_rect(win), category_color(b->cat), lit ? CTRL_HOVER : CTRL_NORMAL);
      return true;
    }
    case evMouseMove: {
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
      bool inside = rect_contains_point(get_client_rect(win), (ipoint16_t){ (int16_t)mx, (int16_t)my });
      if (inside != st->hover) { st->hover = inside; invalidate_window(win); }
      track_mouse(win);
      if (!st->down) return true;
      int sx, sy;
      tile_screen(win, mx, my, &sx, &sy);
      if (!win->drag_visual && abs(mx - st->press.x) + abs(my - st->press.y) > BIN_SLOP) {
        GR_TRACE("bin drag win=%p selected=%d block=%d", (void *)win, g_app->selected_clip, st->block);
        g_app->drag = (drag_t){ .active = true, .block = st->block, .from_clip = -1, .track = -1 };
        window_set_drag_visual(win, mx - st->press.x, my - st->press.y);
      }
      if (win->drag_visual) {
        window_set_drag_visual(win, mx - st->press.x, my - st->press.y);
        send_message(g_app->sheet, shDragOver, MAKEDWORD(sx, sy), NULL);
      }
      return true;
    }
    case evMouseLeave:
      if (st->hover) { st->hover = false; invalidate_window(win); }
      return true;
    case evLeftButtonDown:
      GR_TRACE("bin press win=%p selected=%d block=%d", (void *)win, g_app->selected_clip, st->block);
      st->down = true;
      st->press = (ipoint16_t){ (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam) };
      set_capture(win);
      return true;
    case evLeftButtonUp: {
      if (!st->down) return false;
      GR_TRACE("bin release win=%p selected=%d block=%d dragging=%d", (void *)win, g_app->selected_clip, st->block, win->drag_visual);
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam), sx, sy;
      tile_screen(win, mx, my, &sx, &sy);
      if (win->drag_visual) send_message(g_app->sheet, shDrop, MAKEDWORD(sx, sy), NULL);
      else app_preview(st->block);
      window_clear_drag_visual(win);
      st->down = false;
      set_capture(NULL);
      return true;
    }
    case evPointerCancel:
      if (!st->down) return false;
      if (win->drag_visual) send_message(g_app->sheet, shDragEnd, 0, NULL);
      window_clear_drag_visual(win);
      st->down = false;
      set_capture(NULL);
      return true;
    case evDestroy:
      if (st && st->down) set_capture(NULL);
      free(st);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}

result_t win_bin(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  bin_t *st = win->userdata;
  if (!g_app && msg != evDestroy) return false;
  switch (msg) {
    case evCreate:
      if (!(st = allocate_window_data(win, sizeof(*st)))) {
        fprintf(stderr, "[bin] allocation failed win=%u\n", (unsigned)win->id);
        fflush(stderr);
        return false;
      }
      st->cat = (category_t)(intptr_t)lparam;
      st->count = blocks_in_category(st->cat, st->ids, MAX_TILES);
      for (int i = 0; i < st->count; i++) {
        if (!create_window("", GR_CARD_FLAGS, MAKERECT(0, 0, 1, 1), win, win_tile, 0, (void *)(intptr_t)st->ids[i])) {
          fprintf(stderr, "[bin] tile allocation failed cat=%d index=%d\n", (int)st->cat, i);
          fflush(stderr);
        }
      }
      return true;
    case evPaint:
      layout_tiles(win, st);
      fill_rect(get_sys_color(brControlBg), get_client_rect(win));
      return false;
    case evVScroll:
    case evResize:
      layout_tiles(win, st);
      invalidate_window(win);
      return msg == evVScroll;
    case evDestroy:
      free(st);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}
