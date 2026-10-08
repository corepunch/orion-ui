// VIEW: the sheet — track headers on the left, bar ruler on top, one lane per
// track. Blocks snap to quarter bars. It is the drop target for tiles dragged
// from the bin (shDragOver / shDrop). Each clip is a child window; dragging
// one lifts that window with window_set_drag_visual, and dropping it outside
// the grid removes the clip.
//
// Coordinates: mouse messages arrive in content space (client + scroll), so
// `mx - hpos` is the client x. Clip windows receive their own client space.
// Painting is in client space.

#include "groove.h"

#define RULER_H    GR_RULER_H
#define BAR_W      88
#define SNAP_W     (BAR_W * GR_SNAP_TICKS / GR_TICKS_BAR)
#define SHEET_SLOP 4

typedef struct {
  int press_clip;
  ipoint16_t press;
  bool own_drag;
  groove_drop_anchor_t drop_anchor;
} sheet_t;

static int  hpos(window_t *win)    { return get_scroll_pos(win, SB_HORZ); }
int  sheet_row_h(window_t *win) { return CLAMP((get_client_rect(win).h - RULER_H) / GR_TRACKS, GR_MIN_ROW, GR_MAX_ROW); }
static int  row_h(window_t *win)   { return sheet_row_h(win); }
static int  floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int  bar_x(window_t *win, int bar)  { return bar * BAR_W - hpos(win); }
static int  position_x(window_t *win, int position) { return position * BAR_W / GR_TICKS_BAR - hpos(win); }
static int  position_at(int x) { return floordiv(x * GR_TICKS_BAR, BAR_W); }
static int  seek_ticks(int content_x) { return floordiv(content_x, SNAP_W) * GR_SNAP_TICKS; }
static int  track_y(window_t *win, int t)  { return RULER_H + t * row_h(win); }
static irect16_t grid_rect(window_t *win)  { irect16_t cr = get_client_rect(win); return R(0, RULER_H, cr.w, row_h(win) * GR_TRACKS); }

// Hue of the track's earliest clip, so the lane and header match the cards they hold.
uint32_t track_color(int t) {
  static const category_t fallback[GR_TRACKS] = {0, 7, 1, 2, 4, 3, 13, 15};
  const song_t *s = &g_app->song;
  int best = -1;
  for (int i = 0; i < s->nclips; i++)
    if (s->clips[i].track == t && (best < 0 || s->clips[i].position < s->clips[best].position)) best = i;
  const block_t *b = best >= 0 ? block_get(s->clips[best].block) : NULL;
  return category_color(b ? b->cat : fallback[t]);
}

static void sync_scroll(window_t *win) {
  scroll_info_t si = { .fMask = SIF_RANGE | SIF_PAGE | SIF_POS, .nMin = 0, .nMax = GR_BARS * BAR_W,
                       .nPage = get_client_rect(win).w, .nPos = hpos(win) };
  set_scroll_info(win, SB_HORZ, &si, false);
}

ipoint16_t clip_cell_size(window_t *sheet, const block_t *b) {
  return (ipoint16_t){ b->bars * BAR_W, row_h(sheet) };
}

static irect16_t clip_rect(window_t *win, int track, int position, int bars) {
  return R(position_x(win, position), track_y(win, track), bars * BAR_W, row_h(win));
}

static int child_count(window_t *win) {
  int n = 0;
  for (window_t *c = win->children; c; c = c->next) n++;
  return n;
}

static window_t *child_at(window_t *win, int index) {
  for (window_t *c = win->children; c; c = c->next)
    if (index-- == 0) return c;
  return NULL;
}

void card_place(window_t *card, irect16_t cell) {
  if (!card) return;
  int ox = card->frame.x, oy = card->frame.y;
  bool moved = ox != cell.x || oy != cell.y;
  if (moved) move_window(card, cell.x, cell.y);
  if (card->frame.w != cell.w || card->frame.h != cell.h)
    resize_window(card, cell.w, cell.h);
  if (moved && window_is_lifted(card))
    window_set_drag_visual(card, window_lift_delta(card).x + ox - cell.x, window_lift_delta(card).y + oy - cell.y);
}

// One child per clip, in song order. The tail is dropped when a clip is
// removed; a window under visual drag is left alive until the drag ends.
static void sync_clips(window_t *win) {
  int n = g_app->song.nclips;
  while (child_count(win) > n) {
    window_t *tail = child_at(win, child_count(win) - 1);
    if (!tail || window_is_lifted(tail)) break;
    destroy_window(tail);
  }
  while (child_count(win) < n) {
    if (!create_window("", GR_CARD_FLAGS, MAKERECT(0, 0, 1, 1), win, win_block_card, 0, NULL)) {
      fprintf(stderr, "[sh] clip allocation failed index=%d\n", child_count(win));
      fflush(stderr);
      break;
    }
  }
  int i = 0;
  for (window_t *c = win->children; c && i < n; c = c->next, i++) {
    const clip_t *cl = &g_app->song.clips[i];
    send_message(c, grCardSetBlock, cl->block, NULL);
    send_message(c, grCardSetState, i == g_app->selected_clip ? CTRL_SELECTED : CTRL_NORMAL, NULL);
    layout_measure_t measure = {0};
    send_message(c, evMeasure, 0, &measure);
    ipoint16_t size = {measure.desired_w, measure.desired_h};
    int end = song_clip_end(&g_app->song, i);
    if (!window_is_lifted(c)) size.x = position_x(win, end) - position_x(win, cl->position);
    if (window_has_state(c, WINDOW_STATE_VISIBLE) != (size.x > 0)) show_window(c, size.x > 0);
    size.x = MAX(1, size.x);
    card_place(c, R(position_x(win, cl->position), track_y(win, cl->track), size.x, size.y));
  }
}

// The pointer must enter the target; the chosen anchor determines placement.
static void drag_target(window_t *win, int cx, int cy) {
  drag_t *d = &g_app->drag;
  const sheet_t *st = win->userdata;
  int bars = block_get(d->block)->bars;
  d->track = -1;
  d->valid = false;
  if (!rect_contains_point(grid_rect(win), (ipoint16_t){ (int16_t)cx, (int16_t)cy })) return;
  cx += hpos(win);
  cy -= RULER_H;
  if (st->drop_anchor == GR_DROP_ANCHOR_SAMPLE) {
    irect16_t sample = R(cx - d->grab.x, cy - d->grab.y, bars * BAR_W, row_h(win));
    irect16_t center = rect_center(sample, 0, 0);
    // Candidate centers account for the full width of multi-bar samples.
    cx = center.x - (sample.w - SNAP_W) / 2;
    cy = center.y;
  }
  d->track = CLAMP(floordiv(cy, row_h(win)), 0, GR_TRACKS - 1);
  d->position = CLAMP(floordiv(cx, SNAP_W) * GR_SNAP_TICKS, 0, (GR_BARS - bars) * GR_TICKS_BAR);
  d->valid = song_can_place(&g_app->song, d->track, d->position, bars * GR_TICKS_BAR, d->from_clip);
}

static void drag_clear(window_t *win) {
  window_t *card = child_at(win, g_app->drag.from_clip);
  if (card) window_clear_drag_visual(card);
  g_app->drag = (drag_t){ .track = -1, .from_clip = -1 };
  invalidate_window(win);
}

static void paint_ruler(window_t *win, int cur_bar) {
  irect16_t cr = get_client_rect(win);
  fill_rect(get_sys_color(brPanelDark), R(0, 0, cr.w, RULER_H));
  for (int b = 0; b < GR_BARS; b++) {
    char num[8];
    snprintf(num, sizeof(num), "%d", b + 1);
    if (b == cur_bar) fill_rect(color_with_alpha(get_sys_color(brAccent), 0x50), R(bar_x(win, b), 0, BAR_W, RULER_H));
    fill_rect(color_with_alpha(get_sys_color(brLightEdge), b % 4 ? 0x30 : 0x80), R(bar_x(win, b), b % 4 ? RULER_H - 6 : 2, 1, b % 4 ? 6 : RULER_H - 2));
    draw_text(FONT_SMALLEST, num, bar_x(win, b) + 5, 5, get_sys_color(b % 4 ? brTextSecondary : brTextNormal));
    for (int step = GR_SNAP_TICKS; step < GR_TICKS_BAR; step += GR_SNAP_TICKS)
      fill_rect(color_with_alpha(get_sys_color(brLightEdge), 0x20), R(position_x(win, b * GR_TICKS_BAR + step), RULER_H - 4, 1, 4));
  }
  set_clip_rect(win, R(0, 0, win->frame.w, win->frame.h));
}

static void paint_sheet(window_t *win) {
  irect16_t cr = get_client_rect(win), grid = grid_rect(win);
  const song_t *s = &g_app->song;
  const drag_t *d = &g_app->drag;
  int bar = bar_frames_for_bpm(s->bpm);
  sync_clips(win);
  fill_rect(get_sys_color(brWorkspaceBg), cr);
  set_clip_rect(win, grid);
  for (int t = 0; t < GR_TRACKS; t++) {
    irect16_t lane = R(grid.x, track_y(win, t), grid.w, row_h(win));
    fill_rect(get_sys_color(t % 2 ? brColumnViewBg : brWindowDarkBg), lane);
    fill_rect(color_with_alpha(track_color(t), 0x0e), lane);
  }
  for (int position = 0; position <= GR_BARS * GR_TICKS_BAR; position += GR_SNAP_TICKS) {
    int alpha = position % GR_TICKS_BAR ? 0x0a : position % (4 * GR_TICKS_BAR) ? 0x14 : 0x38;
    fill_rect(color_with_alpha(get_sys_color(brLightEdge), alpha), R(position_x(win, position), grid.y, 1, grid.h));
  }
  float saved[16];
  memcpy(saved, get_sprite_matrix(), sizeof saved);
  for (window_t *c = win->children; c; c = c->next) {
    if (!window_has_state(c, WINDOW_STATE_VISIBLE) || window_is_lifted(c)) continue;
    send_message(c, evPaint, 0, NULL);
  }
  end_draw_transform(saved);
  set_clip_rect(win, grid);
  if (d->active && d->track >= 0) {
    const block_t *b = block_get(d->block);
    stroke_rounded_rect(d->valid ? category_color(b->cat) : get_sys_color(brTextError),
                        clip_rect(win, d->track, d->position, b->bars), get_theme()->card_corner_radius, 2);
  }
  int px = (int)((double)s->pos / bar * BAR_W) - hpos(win);
  fill_rect(get_sys_color(brAccent), R(px - 1, RULER_H, 2, grid.h));
  set_clip_rect(win, R(0, 0, win->frame.w, win->frame.h));
  paint_ruler(win, (int)(s->pos / bar));
  fill_rect(get_sys_color(brAccent), R(px - 1, 0, 2, RULER_H));
  set_clip_rect(win, R(0, 0, win->frame.w, win->frame.h));
}

result_t win_sheet(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  sheet_t *st = win->userdata;
  if (!g_app && msg != evDestroy) return false;
  switch (msg) {
    case evCreate:
      if (!(st = allocate_window_data(win, sizeof(*st)))) {
        fprintf(stderr, "[sh] allocation failed win=%u\n", (unsigned)win->id);
        fflush(stderr);
        return false;
      }
      g_app->sheet = win;
      st->press_clip = -1;
      sync_scroll(win);
      return true;
    case evResize: sync_scroll(win); sync_clips(win); tracks_sync(); invalidate_window(win); return false;
    case evHScroll: sync_clips(win); invalidate_window(win); return true;
    case evPaint: paint_sheet(win); return true;
    case evHitTest: return true; // clips paint, the sheet keeps the pointer
    case evQueryDrag: { // clips drag; a swipe over empty lanes scrolls
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
      if (my < RULER_H) return false;
      return song_clip_at(&g_app->song, (my - RULER_H) / row_h(win), position_at(mx)) >= 0 ? DRAG_NOW : DRAG_NONE;
    }
    case evLeftButtonDown: {
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
      if (my < RULER_H) { app_seek_position(seek_ticks(mx)); return true; }
      int track = (my - RULER_H) / row_h(win), position = position_at(mx);
      st->press_clip = track >= 0 && track < GR_TRACKS ? song_clip_at(&g_app->song, track, position) : -1;
      st->press = (ipoint16_t){ (int16_t)mx, (int16_t)my };
      app_select_clip(st->press_clip);
      if (st->press_clip < 0) { app_seek_position(seek_ticks(mx)); return true; }
      const clip_t *c = &g_app->song.clips[st->press_clip];
      ipoint16_t grab = { mx - c->position * BAR_W / GR_TICKS_BAR, my - track_y(win, c->track) };
      g_app->drag = (drag_t){ .block = c->block, .from_clip = st->press_clip, .grab = grab,
                              .track = c->track, .position = c->position, .valid = true };
      set_capture(win);
      return true;
    }
    case evMouseMove:
      if (!st || st->press_clip < 0) return false;
      {
        int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
        if (!st->own_drag && abs(mx - st->press.x) + abs(my - st->press.y) > SHEET_SLOP) {
          st->own_drag = true;
          g_app->drag.active = true;
          window_t *card = child_at(win, st->press_clip);
          if (card) {
            ipoint16_t size = clip_cell_size(win, block_get(g_app->drag.block));
            resize_window(card, size.x, size.y);
            window_set_drag_visual(card, mx - st->press.x, my - st->press.y);
          }
        }
        if (st->own_drag) {
          window_t *card = child_at(win, st->press_clip);
          if (card) window_set_drag_visual(card, mx - st->press.x, my - st->press.y);
          drag_target(win, mx - hpos(win), my);
          invalidate_window(win);
        }
      }
      return true;
    case evLeftButtonUp:
      if (!st || st->press_clip < 0) return false;
      if (!st->own_drag) app_preview(g_app->drag.block);
      if (st->own_drag) drag_target(win, (int16_t)LOWORD(wparam) - hpos(win), (int16_t)HIWORD(wparam));
      if (st->own_drag && g_app->drag.track >= 0 && g_app->drag.valid) app_drop(&g_app->drag);
      bool removed = st->own_drag && g_app->drag.track < 0;
      st->press_clip = -1;
      st->own_drag = false;
      set_capture(NULL);
      drag_clear(win);
      if (removed) app_command(ID_DELETE); // dragged off the grid; the pressed clip is selected
      return true;
    case evRightButtonDown: {
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
      if (my < RULER_H) return false;
      int idx = song_clip_at(&g_app->song, (my - RULER_H) / row_h(win), position_at(mx));
      if (idx < 0) return false;
      app_select_clip(idx);
      app_command(ID_DELETE);
      return true;
    }

    case shSetDropAnchor:
      if (wparam != GR_DROP_ANCHOR_SAMPLE && wparam != GR_DROP_ANCHOR_POINTER) {
        fprintf(stderr, "[sh] invalid drop anchor win=%u anchor=%u\n", win->id, wparam);
        fflush(stderr);
        return false;
      }
      st->drop_anchor = (groove_drop_anchor_t)wparam;
      invalidate_window(win);
      return true;
    case shDragOver:
    case shDrop: {
      int cx = (int)LOWORD(wparam) - window_screen_x(win), cy = (int)HIWORD(wparam) - window_screen_y(win);
      drag_target(win, cx, cy);
      if (msg == shDrop && g_app->drag.track >= 0 && g_app->drag.valid) app_drop(&g_app->drag);
      if (msg == shDrop) drag_clear(win); else invalidate_window(win);
      return true;
    }
    case shDragEnd: drag_clear(win); return true;
    case evPointerCancel:
      if (st && st->press_clip >= 0) {
        st->press_clip = -1;
        st->own_drag = false;
        set_capture(NULL);
        drag_clear(win);
      }
      return true;
    case evDestroy:
      if (g_app && g_app->sheet == win) g_app->sheet = NULL;
      if (st && st->press_clip >= 0) set_capture(NULL);
      free(st);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}
