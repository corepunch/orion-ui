// VIEW: the sheet — track headers on the left, bar ruler on top, one lane per
// track. Blocks snap to quarter bars. It is the drop target for tiles dragged
// from the bin (shDragOver / shDrop). Each clip is a child window; dragging
// one lifts that window with window_set_drag_visual.
//
// Coordinates: mouse messages arrive in content space (client + scroll), so
// `mx - hpos` is the client x. Clip windows receive their own client space.
// Painting is in client space.

#include "groove.h"

#define HDR_W      GR_SHEET_HEADER_W
#define RULER_H    22
#define BAR_W      88
#define SNAP_W     (BAR_W * GR_SNAP_TICKS / GR_TICKS_BAR)
#define MIN_ROW    26
#define MAX_ROW    64
#define SHEET_SLOP 4

typedef struct {
  int press_clip;
  ipoint16_t press;
  bool own_drag;
  groove_drop_anchor_t drop_anchor;
} sheet_t;

static int  hpos(window_t *win)    { return get_scroll_pos(win, SB_HORZ); }
static int  row_h(window_t *win)   { return CLAMP((get_client_rect(win).h - RULER_H) / GR_TRACKS, MIN_ROW, MAX_ROW); }
static int  floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int  bar_x(window_t *win, int bar)  { return HDR_W + bar * BAR_W - hpos(win); }
static int  position_x(window_t *win, int position) { return HDR_W + position * BAR_W / GR_TICKS_BAR - hpos(win); }
static int  position_at(int x) { return floordiv(x * GR_TICKS_BAR, BAR_W); }
static int  track_y(window_t *win, int t)  { return RULER_H + t * row_h(win); }
static irect16_t grid_rect(window_t *win)  { irect16_t cr = get_client_rect(win); return R(HDR_W, RULER_H, cr.w - HDR_W, row_h(win) * GR_TRACKS); }
static irect16_t mute_rect(window_t *win, int t) {
  int size = MIN(24, (row_h(win) - 4) / 2);
  irect16_t pair = rect_center(R(0, track_y(win, t), HDR_W, row_h(win)), size, size * 2 + 2);
  return rect_split_top(pair, size);
}
static irect16_t solo_rect(window_t *win, int t) {
  irect16_t mute = mute_rect(win, t);
  return rect_offset(mute, 0, mute.h + 2);
}

static void draw_track_toggle(irect16_t r, const char *icon, bool active, uint32_t on_color) {
  draw_plastic_button(r, active ? CTRL_SELECTED : CTRL_NORMAL, active ? on_color : get_sys_color(brControlBg), icon);
}

// Hue of the track's earliest clip, so the lane and header match the cards they hold.
static uint32_t track_color(int t) {
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
                       .nPage = get_client_rect(win).w - HDR_W, .nPos = hpos(win) };
  set_scroll_info(win, SB_HORZ, &si, false);
}

ipoint16_t clip_cell_size(window_t *sheet, const block_t *b) {
  return (ipoint16_t){ b->bars * BAR_W, row_h(sheet) };
}

static irect16_t clip_rect(window_t *win, int track, int position, int bars) {
  return R(position_x(win, position) + 1, track_y(win, track) + 2, bars * BAR_W - 2, row_h(win) - 4);
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
  if (moved && card->drag_visual)
    window_set_drag_visual(card, card->drag_dx + ox - cell.x, card->drag_dy + oy - cell.y);
}

// One child per clip, in song order. The tail is dropped when a clip is
// removed; a window under visual drag is left alive until the drag ends.
static void sync_clips(window_t *win) {
  int n = g_app->song.nclips;
  while (child_count(win) > n) {
    window_t *tail = child_at(win, child_count(win) - 1);
    if (!tail || tail->drag_visual) break;
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
    if (!c->drag_visual) size.x = position_x(win, end) - position_x(win, cl->position);
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
  cx += hpos(win) - HDR_W;
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

static void paint_headers(window_t *win) {
  irect16_t cr = get_client_rect(win);
  fill_rect(get_sys_color(brPanelDarker), R(0, RULER_H, HDR_W, cr.h - RULER_H));
  for (int t = 0; t < GR_TRACKS; t++) {
    int y = track_y(win, t), rh = row_h(win);
    fill_rect(get_sys_color(brDarkEdge), R(0, y + rh - 1, HDR_W, 1));
    irect16_t m = mute_rect(win, t), s = solo_rect(win, t);
    fill_rect(track_color(t), R(0, y + 2, 3, rh - 5));
    draw_track_toggle(m, "phosphor-speaker-slash-fill", g_app->song.mute[t], WEB(0xff4f6c));
    draw_track_toggle(s, "phosphor-headphones-fill", g_app->song.solo[t], WEB(0xffb21e));
  }
}

static void paint_ruler(window_t *win, int cur_bar) {
  irect16_t cr = get_client_rect(win);
  fill_rect(get_sys_color(brPanelDark), R(0, 0, cr.w, RULER_H));
  set_clip_rect(win, R(HDR_W, 0, cr.w - HDR_W, RULER_H));
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
    if (!window_has_state(c, WINDOW_STATE_VISIBLE) || c->drag_visual) continue;
    send_message(c, evPaint, 0, NULL);
  }
  end_draw_transform(saved);
  set_clip_rect(win, grid);
  if (d->active && d->track >= 0) {
    const block_t *b = block_get(d->block);
    stroke_rounded_rect(d->valid ? category_color(b->cat) : get_sys_color(brTextError),
                        clip_rect(win, d->track, d->position, b->bars), get_theme()->card_corner_radius, 2);
  }
  int px = HDR_W + (int)((double)s->pos / bar * BAR_W) - hpos(win);
  fill_rect(get_sys_color(brAccent), R(px - 1, RULER_H, 2, grid.h));
  set_clip_rect(win, R(0, 0, win->frame.w, win->frame.h));
  paint_ruler(win, (int)(s->pos / bar));
  paint_headers(win);
}

static bool header_click(window_t *win, int cx, int cy) {
  if (cx >= HDR_W || cy < RULER_H) return false;
  for (int t = 0; t < GR_TRACKS; t++) {
    ipoint16_t p = { (int16_t)cx, (int16_t)cy };
    bool m = rect_contains_point(mute_rect(win, t), p), s = rect_contains_point(solo_rect(win, t), p);
    if (!m && !s) continue;
    app_lock();
    if (m) g_app->song.mute[t] = !g_app->song.mute[t]; else g_app->song.solo[t] = !g_app->song.solo[t];
    app_unlock();
    invalidate_window(win);
    return true;
  }
  return true;
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
      st->press_clip = -1;
      sync_scroll(win);
      return true;
    case evResize: sync_scroll(win); sync_clips(win); invalidate_window(win); return false;
    case evHScroll: sync_clips(win); invalidate_window(win); return true;
    case evPaint: paint_sheet(win); return true;
    case evHitTest: return true; // clips paint, the sheet keeps the pointer
    case evGetTooltipText: {
      if (!lparam) return false;
      ipoint16_t p = { (int16_t)LOWORD(wparam) - hpos(win), (int16_t)HIWORD(wparam) };
      if (p.y < RULER_H) return false;
      int track = (p.y - RULER_H) / row_h(win);
      if (track >= GR_TRACKS) return false;
      const char *action;
      if (rect_contains_point(mute_rect(win, track), p)) action = g_app->song.mute[track] ? "Unmute" : "Mute";
      else if (rect_contains_point(solo_rect(win, track), p)) action = g_app->song.solo[track] ? "Unsolo" : "Solo";
      else return false;
      snprintf(lparam, 256, "%s track %d", action, track + 1);
      return true;
    }

    case evLeftButtonDown: {
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam), cx = mx - hpos(win);
      if (my < RULER_H) { if (cx >= HDR_W) app_seek_position(floordiv(mx - HDR_W, SNAP_W) * GR_SNAP_TICKS); return true; }
      if (header_click(win, cx, my)) return true;
      int track = (my - RULER_H) / row_h(win), position = position_at(mx - HDR_W);
      if (track < 0 || track >= GR_TRACKS) return true;
      st->press_clip = song_clip_at(&g_app->song, track, position);
      st->press = (ipoint16_t){ (int16_t)mx, (int16_t)my };
      app_select_clip(st->press_clip);
      if (st->press_clip >= 0) {
        const clip_t *c = &g_app->song.clips[st->press_clip];
        ipoint16_t grab = { mx - HDR_W - c->position * BAR_W / GR_TICKS_BAR, my - track_y(win, c->track) };
        g_app->drag = (drag_t){ .block = c->block, .from_clip = st->press_clip, .grab = grab,
                                .track = c->track, .position = c->position, .valid = true };
        set_capture(win);
      }
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
      st->press_clip = -1;
      st->own_drag = false;
      set_capture(NULL);
      drag_clear(win);
      return true;
    case evRightButtonDown: {
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
      if (my < RULER_H || mx - hpos(win) < HDR_W) return false;
      int idx = song_clip_at(&g_app->song, (my - RULER_H) / row_h(win), position_at(mx - HDR_W));
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
      if (st && st->press_clip >= 0) set_capture(NULL);
      free(st);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}
