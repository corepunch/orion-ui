// VIEW: the playlist editor — PLEDIT.BMP frame tiled to the window height,
// the track list in the system font with PLEDIT.TXT colours. Tap a track to
// play it, drag to scroll, touch and hold to pick a track up and reorder it.
// Files dropped on the list are inserted where they land.

#include "winamp.h"

#define BOTTOM_H 38
#define TOP_H    20
#define LEFT_W   12
#define RIGHT_W  20
#define DRAG_SLOP 6
#define HOLD_MS   350

typedef struct {
  wa_canvas_t canvas;
  uint16_t pressed;          // bottom-bar command under the finger
  bool inside, scrolling, tracking, reordering;
  int down_y, down_scroll, down_row, drag_row;
  uint32_t hold_timer;       // pending touch-and-hold that picks up down_row
} pl_view_t;

// Bottom bar: ADD REM SEL MISC, then the mini transport. y is relative to the
// bar; a negative x counts from the right edge. SEL rescans Documents.
static const wa_region_t kBottom[] = {
  { ID_PLAYBACK_OPEN,  {   14, 10, 25, 18 } },
  { ID_LIST_REMOVE,    {   43, 10, 25, 18 } },
  { ID_LIST_RESCAN,    {   72, 10, 25, 18 } },
  { 0,                 {  101, 10, 25, 18 } },   // MISC: no menu on a phone
  { ID_PLAYBACK_PREV,  { -144, 22, 10,  9 } },
  { ID_PLAYBACK_PLAY,  { -133, 22, 10,  9 } },
  { ID_PLAYBACK_PAUSE, { -122, 22, 10,  9 } },
  { ID_PLAYBACK_STOP,  { -111, 22, 10,  9 } },
  { ID_PLAYBACK_NEXT,  { -100, 22, 10,  9 } },
  { ID_PLAYBACK_OPEN,  {  -89, 22, 10,  9 } },
};

static irect16_t bottom_rect(const wa_canvas_t *c, const wa_region_t *r) {
  int x = r->r.x < 0 ? c->w + r->r.x : r->r.x;
  return R(x, c->h - BOTTOM_H + r->r.y, r->r.w, r->r.h);
}

static int hit_bottom(const wa_canvas_t *c, ipoint16_t p) {
  for (int i = 0; i < (int)ARRAY_LEN(kBottom); i++) {
    if (!kBottom[i].id) continue;
    if (rect_contains_point(bottom_rect(c, &kBottom[i]), p)) return i + 1;
  }
  return 0;
}

static int row_height(void) {
  return MAX(text_char_height(FONT_SMALL) + 4, (int)lroundf(13 * g_app->pt_per_px));
}

static irect16_t list_rect(const wa_canvas_t *c) {
  float pt = g_app->pt_per_px;
  return R((int)lroundf(LEFT_W * pt), (int)lroundf(TOP_H * pt),
           (int)lroundf((c->w - LEFT_W - RIGHT_W) * pt), (int)lroundf((c->h - TOP_H - BOTTOM_H) * pt));
}

static int visible_rows(const wa_canvas_t *c) { return MAX(1, list_rect(c).h / row_height()); }

static void clamp_scroll(const wa_canvas_t *c) {
  wa_playlist_t *pl = &g_app->list;
  pl->scroll = MAX(0, MIN(pl->scroll, pl->count - visible_rows(c)));
}

static void paint_frame(pl_view_t *v) {
  wa_canvas_t *c = &v->canvas;
  int w = c->w, h = c->h;
  canvas_tile(c, SKIN_PLEDIT, R(127, 0, 25, TOP_H), R(25, 0, w - 50, TOP_H));
  canvas_blit(c, SKIN_PLEDIT, R(0, 0, 25, TOP_H), 0, 0);
  canvas_blit(c, SKIN_PLEDIT, R(26, 0, 100, TOP_H), (w - 100) / 2, 0);
  canvas_blit(c, SKIN_PLEDIT, R(153, 0, 25, TOP_H), w - 25, 0);
  canvas_tile(c, SKIN_PLEDIT, R(0, 42, LEFT_W, 29), R(0, TOP_H, LEFT_W, h - TOP_H - BOTTOM_H));
  canvas_tile(c, SKIN_PLEDIT, R(31, 42, RIGHT_W, 29), R(w - RIGHT_W, TOP_H, RIGHT_W, h - TOP_H - BOTTOM_H));
  canvas_blit(c, SKIN_PLEDIT, R(0, 72, 125, BOTTOM_H), 0, h - BOTTOM_H);
  canvas_tile(c, SKIN_PLEDIT, R(179, 0, 25, BOTTOM_H), R(125, h - BOTTOM_H, MAX(0, w - 275), BOTTOM_H));
  canvas_blit(c, SKIN_PLEDIT, R(126, 72, 150, BOTTOM_H), w - 150, h - BOTTOM_H);
  canvas_fill(c, R(LEFT_W, TOP_H, w - LEFT_W - RIGHT_W, h - TOP_H - BOTTOM_H), g_app->skin.pl_normal_bg);
  // Scroll thumb.
  wa_playlist_t *pl = &g_app->list;
  int rows = visible_rows(c), track = h - TOP_H - BOTTOM_H - 18;
  int range = MAX(1, pl->count - rows);
  int y = TOP_H + (pl->count > rows ? pl->scroll * track / range : 0);
  canvas_blit(c, SKIN_PLEDIT, R(v->scrolling ? 61 : 52, 53, 8, 18), w - 15, y);
  // Running time: "elapsed/total" of the current track.
  char text[32] = "";
  if (g_app->engine.dec) {
    int at = app_elapsed_seconds(), total = app_track_seconds();
    snprintf(text, sizeof(text), "%d:%02d/%d:%02d", at / 60, at % 60, total / 60, total % 60);
  }
  canvas_text(c, text, w - 150 + 8, h - BOTTOM_H + 10, 86, 0);
}

static void paint_rows(window_t *win, pl_view_t *v) {
  wa_canvas_t *c = &v->canvas;
  wa_playlist_t *pl = &g_app->list;
  wa_skin_t *s = &g_app->skin;
  irect16_t list = list_rect(c);
  int rh = row_height(), th = text_char_height(FONT_SMALL), rows = visible_rows(c);
  (void)win;
  for (int r = 0; r < rows; r++) {
    int i = pl->scroll + r;
    if (i >= pl->count) break;
    irect16_t row = R(list.x, list.y + r * rh, list.w, rh);
    if (i == pl->selected) fill_rect(s->pl_selected_bg, row);
    uint32_t color = i == pl->current ? s->pl_current : s->pl_normal;
    int ty = row.y + (rh - th) / 2;
    char dur[16] = "";
    const wa_track_t *t = &pl->items[i];
    if (t->seconds >= 0) snprintf(dur, sizeof(dur), "%d:%02d", t->seconds / 60, t->seconds % 60);
    int dw = text_strwidth(FONT_SMALL, dur);
    char label[512];
    snprintf(label, sizeof(label), "%d. %s", i + 1, t->title);
    draw_text_ellipsized(FONT_SMALL, label, row.x + 3, ty, row.w - dw - 12, color);
    draw_text(FONT_SMALL, dur, row.x + row.w - dw - 3, ty, color);
  }
}

static bool sync_canvas(window_t *win, pl_view_t *v) {
  int h = MAX(WA_PL_MIN_H, (int)floorf(win->frame.h / g_app->pt_per_px));
  return canvas_resize(&v->canvas, WA_W, h);
}

static int row_at(pl_view_t *v, uint32_t wparam) {
  irect16_t list = list_rect(&v->canvas);
  ipoint16_t p = { (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam) };
  if (!rect_contains_point(list, p)) return -1;
  int i = g_app->list.scroll + (p.y - list.y) / row_height();
  return i < g_app->list.count ? i : -1;
}

// Insertion slot for a content-space y: the row boundary nearest to it.
static int slot_at(pl_view_t *v, int y) {
  irect16_t list = list_rect(&v->canvas);
  int rh = row_height();
  int slot = g_app->list.scroll + (y - list.y + rh / 2) / rh;
  return MAX(0, MIN(slot, g_app->list.count));
}

static void cancel_hold(pl_view_t *v) {
  if (v->hold_timer) axCancelTimer(v->hold_timer);
  v->hold_timer = 0;
}

// Follows the finger with the picked-up track; past either end the list scrolls.
static void reorder_to(pl_view_t *v, int y) {
  wa_playlist_t *pl = &g_app->list;
  irect16_t list = list_rect(&v->canvas);
  if (y < list.y && pl->scroll > 0) pl->scroll--;
  if (y >= list.y + list.h) pl->scroll++;
  clamp_scroll(&v->canvas);
  int row = pl->scroll + (y - list.y) / row_height();
  row = MAX(0, MIN(row, pl->count - 1));
  if (row != v->drag_row) { playlist_move(pl, v->drag_row, row); v->drag_row = row; }
}

result_t win_winamp_playlist(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  pl_view_t *v = win->userdata;
  switch (msg) {
    case evCreate:
      v = allocate_window_data(win, sizeof(pl_view_t));
      if (!v || !canvas_resize(&v->canvas, WA_W, WA_PL_MIN_H)) return false;
      if (g_app) g_app->playlist = win;
      return true;
    case evMeasure:
      skin_view_measure(g_app && g_app->show_pl && !g_app->landscape ? WA_PL_MIN_H : 0, lparam);
      return true;
    case evPaint: {
      if (!g_app || !sync_canvas(win, v)) return true;
      clamp_scroll(&v->canvas);
      paint_frame(v);
      for (int i = 0; i < (int)ARRAY_LEN(kBottom); i++) {
        if (!(v->inside && v->pressed == i + 1)) continue;
        irect16_t r = bottom_rect(&v->canvas, &kBottom[i]);
        canvas_fill(&v->canvas, R(r.x, r.y, r.w, 1), g_app->skin.pl_current);   // pressed: lit top edge
      }
      paint_rows(win, v);
      int r = v->reordering ? v->drag_row - g_app->list.scroll : -1;
      if (r >= 0 && r < visible_rows(&v->canvas)) {
        irect16_t list = list_rect(&v->canvas);
        draw_sel_rect(R(list.x, list.y + r * row_height(), list.w, row_height()));
      }
      return true;
    }
    case evDropFile: {
      int slot = slot_at(v, (int16_t)HIWORD(wparam));
      return app_drop_file(lparam, slot < g_app->list.count ? slot : -1, false);
    }
    case evTimer:
      if (wparam != v->hold_timer) return false;
      v->hold_timer = 0;
      if (v->tracking && !v->scrolling && v->down_row >= 0 && v->down_row < g_app->list.count) {
        v->reordering = true;
        v->drag_row = g_app->list.selected = v->down_row;
        invalidate_window(win);
      }
      return true;
    case evQueryDrag:
      return DRAG_NOW;
    case evWheel: {
      int dy = (int16_t)HIWORD((uint32_t)(uintptr_t)lparam);
      g_app->list.scroll -= dy / MAX(1, row_height() / 2);
      clamp_scroll(&v->canvas);
      invalidate_window(win);
      return true;
    }
    case evLeftButtonDown: {
      ipoint16_t p = skin_point(win, &v->canvas, wparam);
      v->pressed = (uint16_t)hit_bottom(&v->canvas, p);
      v->inside = v->pressed != 0;
      v->tracking = !v->pressed;
      v->scrolling = false;
      v->down_y = (int16_t)HIWORD(wparam);
      v->down_scroll = g_app->list.scroll;
      v->down_row = row_at(v, wparam);
      v->reordering = false;
      cancel_hold(v);
      if (v->tracking && v->down_row >= 0) v->hold_timer = axSetTimer(win, HOLD_MS, NULL, false);
      set_capture(win);
      invalidate_window(win);
      return true;
    }
    case evMouseMove: {
      if (v->pressed) {
        v->inside = rect_contains_point(bottom_rect(&v->canvas, &kBottom[v->pressed - 1]), skin_point(win, &v->canvas, wparam));
        invalidate_window(win);
        return true;
      }
      if (!v->tracking) return false;
      if (v->reordering) { reorder_to(v, (int16_t)HIWORD(wparam)); invalidate_window(win); return true; }
      int dy = (int16_t)HIWORD(wparam) - v->down_y;
      if (!v->scrolling && abs(dy) > DRAG_SLOP) { v->scrolling = true; cancel_hold(v); }
      if (v->scrolling) {
        g_app->list.scroll = v->down_scroll - dy / row_height();
        clamp_scroll(&v->canvas);
        invalidate_window(win);
      }
      return true;
    }
    case evLeftButtonUp: {
      bool tap = v->tracking && !v->scrolling && !v->reordering;
      uint16_t pressed = v->pressed;
      bool inside = v->inside;
      cancel_hold(v);
      v->pressed = 0;
      v->inside = v->tracking = v->scrolling = v->reordering = false;
      set_capture(NULL);
      if (pressed && inside) app_command(kBottom[pressed - 1].id);
      else if (tap && v->down_row >= 0 && v->down_row == row_at(v, wparam)) app_play_index(v->down_row);
      invalidate_window(win);
      return true;
    }
    case evPointerCancel:
      if (v) { cancel_hold(v); v->pressed = 0; v->inside = v->tracking = v->scrolling = v->reordering = false; set_capture(NULL); invalidate_window(win); }
      return true;
    case evDestroy:
      if (v) { cancel_hold(v); canvas_free(&v->canvas); free(v); win->userdata = NULL; }
      if (g_app && g_app->playlist == win) g_app->playlist = NULL;
      return true;
    default:
      return false;
  }
}
