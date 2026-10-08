// VIEW: the sound bin holds a card for every block and shows the ones that pass
// the library toolbar's family and genre filters. Each card is a child
// window. Pressing it auditions the block; dragging carries a copy of that
// window with window_set_drag_copy, so the card stays in the bin.

#include "groove.h"

#define TILE_GAP   0
#define TILE_PAD   0
#define MAX_TILES  GR_MAX_BLOCKS

typedef struct {
  int ids[MAX_TILES], count;
} bin_t;

static int vpos(window_t *win) { return get_scroll_pos(win, SB_VERT); }

// Shows the cards that pass the bin's filter, hides the rest.
static int apply_filter(window_t *win, bin_t *st) {
  int i = 0, shown = 0;
  for (window_t *c = win->children; c && i < st->count; c = c->next, i++) {
    bool match = block_visible(st->ids[i]);
    if (match != window_has_state(c, WINDOW_STATE_VISIBLE)) show_window(c, match);
    shown += match;
  }
  return shown;
}

// Content-space rows, frames in the bin's viewport (content y minus scroll).
static void layout_tiles(window_t *win, bin_t *st) {
  irect16_t cr = get_client_rect(win);
  int x = TILE_PAD, y = TILE_PAD, i = 0, bottom = 0, row_height = 0;
  for (window_t *c = win->children; c && i < st->count; c = c->next, i++) {
    if (!window_has_state(c, WINDOW_STATE_VISIBLE)) continue;
    layout_measure_t measure = {0};
    send_message(c, evMeasure, 0, &measure);
    ipoint16_t size = {measure.desired_w, measure.desired_h};
    if (x + size.x > cr.w - TILE_PAD && x > TILE_PAD) { x = TILE_PAD; y += row_height + TILE_GAP; row_height = 0; }
    card_place(c, R(x, y - vpos(win), size.x, size.y));
    row_height = MAX(row_height, size.y);
    bottom = y + row_height;
    x += size.x + TILE_GAP;
  }
  int content_h = bottom + TILE_PAD;
  scroll_info_t si = { .fMask = SIF_RANGE | SIF_PAGE | SIF_POS, .nMin = 0, .nMax = content_h, .nPage = cr.h, .nPos = vpos(win) };
  set_scroll_info(win, SB_VERT, &si, false);
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
      // Family by family, so "All" reads as the old tabs one after another.
      for (int cat = 0; cat < CAT_COUNT; cat++) st->count += blocks_in_category(cat, st->ids + st->count, MAX_TILES - st->count);
      for (int i = 0; i < st->count; i++) {
        if (!create_window("", GR_CARD_FLAGS, MAKERECT(0, 0, 1, 1), win, win_block_card, 0, (void *)(intptr_t)st->ids[i])) {
          fprintf(stderr, "[bin] tile allocation failed index=%d block=%d\n", i, st->ids[i]);
          fflush(stderr);
        }
      }
      apply_filter(win, st);
      return true;
    case evPaint: {
      layout_tiles(win, st);
      irect16_t cr = get_client_rect(win);
      fill_rect(get_sys_color(brControlBg), cr);
      if (!apply_filter(win, st))
        draw_text(FONT_SYSTEM, "No sounds in this family", TILE_PAD + 4, TILE_PAD + 4, get_sys_color(brTextSecondary));
      return false;
    }
    case binFilter:
      apply_filter(win, st);
      set_scroll_info(win, SB_VERT, &(scroll_info_t){ .fMask = SIF_POS, .nPos = 0 }, false);
      layout_tiles(win, st);
      invalidate_window(win);
      return true;
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
