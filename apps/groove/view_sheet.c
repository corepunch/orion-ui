// VIEW: the sheet — track headers on the left, bar ruler on top, one lane per
// track. Blocks snap to whole bars. It is both a drop target for blocks dragged
// from the bin (shDragOver / shDrop) and the source of drags that move clips.
//
// Coordinates: mouse messages arrive in content space (client + scroll), so
// `mx - hpos` is the client x; painting is in client space.

#include "groove.h"

#define HDR_W   78
#define RULER_H 22
#define BAR_W   88
#define MIN_ROW 26
#define MAX_ROW 64
#define SHEET_SLOP 4

typedef struct { int press_clip; ipoint16_t press; bool own_drag; } sheet_t;

static int  hpos(window_t *win)   { return get_scroll_pos(win, SB_HORZ); }
static int  row_h(window_t *win)  { return CLAMP((get_client_rect(win).h - RULER_H) / GR_TRACKS, MIN_ROW, MAX_ROW); }
static int  floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int  bar_x(window_t *win, int bar)  { return HDR_W + bar * BAR_W - hpos(win); }
static int  track_y(window_t *win, int t)  { return RULER_H + t * row_h(win); }
static irect16_t grid_rect(window_t *win)  { irect16_t cr = get_client_rect(win); return R(HDR_W, RULER_H, cr.w - HDR_W, row_h(win) * GR_TRACKS); }
static irect16_t mute_rect(window_t *win, int t) { return R(HDR_W - 46, track_y(win, t) + (row_h(win) - 16) / 2, 20, 16); }
static irect16_t solo_rect(window_t *win, int t) { return R(HDR_W - 24, track_y(win, t) + (row_h(win) - 16) / 2, 20, 16); }

static void sync_scroll(window_t *win) {
  scroll_info_t si = { .fMask = SIF_RANGE | SIF_PAGE | SIF_POS, .nMin = 0, .nMax = GR_BARS * BAR_W,
                       .nPage = get_client_rect(win).w - HDR_W, .nPos = hpos(win) };
  set_scroll_info(win, SB_HORZ, &si, false);
}

static irect16_t clip_rect(window_t *win, int track, int bar, int bars) {
  return R(bar_x(win, bar) + 1, track_y(win, track) + 2, bars * BAR_W - 2, row_h(win) - 4);
}

// Resolves a client-space point into the drag's snapped target.
static void drag_target(window_t *win, int cx, int cy) {
  drag_t *d = &g_app->drag;
  int bars = block_get(d->block)->bars;
  d->track = -1;
  d->valid = false;
  if (!rect_contains_point(grid_rect(win), (ipoint16_t){ (int16_t)cx, (int16_t)cy })) return;
  d->track = (cy - RULER_H) / row_h(win);
  d->bar = CLAMP(floordiv(cx - HDR_W + hpos(win), BAR_W) - d->grab_bars, 0, GR_BARS - bars);
  d->valid = song_can_place(&g_app->song, d->track, d->bar, bars, d->from_clip);
}

static void drag_clear(window_t *win) {
  g_app->drag = (drag_t){ .track = -1, .from_clip = -1 };
  invalidate_window(win);
}

void draw_clip(window_t *win, const block_t *b, irect16_t r, uint32_t color, bool ring) {
  int radius = get_theme()->card_corner_radius;
  if (ring) fill_rounded_rect(get_sys_color(brAccent), rect_inset(r, -2), radius + 2);
  fill_rounded_rect(color, r, radius);
  uint32_t ink = color_with_alpha(get_sys_color(brWindowDarkBg), (color >> 24) < 0xff ? 0x70 : 0x55);
  int np = b->audio.npeaks;
  for (int x = 0; x < r.w && np > 1; x++) {
    float f = (x + 0.5f) * np / r.w - 0.5f;
    int k = f < 0 ? 0 : (int)f, k1 = k + 1 < np ? k + 1 : np - 1;
    float t = f < 0 ? 0 : f - k, p = b->audio.peaks[k] + (b->audio.peaks[k1] - b->audio.peaks[k]) * t;
    int h = MAX(1, (int)(p * (r.h - 14) / 255));
    fill_rect(ink, R(r.x + x, r.y + r.h - 4 - h, 1, h));
  }
  draw_text_ellipsized(FONT_SMALL, b->name, r.x + 6, r.y + 3, r.w - 10, color_with_alpha(get_sys_color(brTextNormal), (color >> 24)));
}

static void paint_headers(window_t *win) {
  irect16_t cr = get_client_rect(win);
  fill_rect(get_sys_color(brPanelDarker), R(0, RULER_H, HDR_W, cr.h - RULER_H));
  for (int t = 0; t < GR_TRACKS; t++) {
    int y = track_y(win, t), rh = row_h(win);
    char num[8];
    snprintf(num, sizeof(num), "%d", t + 1);
    fill_rect(get_sys_color(brDarkEdge), R(0, y + rh - 1, HDR_W, 1));
    draw_text(FONT_SYSTEM, num, 10, y + (rh - text_char_height(FONT_SYSTEM)) / 2, get_sys_color(brTextSecondary));
    irect16_t m = mute_rect(win, t), s = solo_rect(win, t);
    draw_badge(FONT_SMALLEST, "M", m.x + 5, m.y, m.h, g_app->song.mute[t] ? get_sys_color(brTextError) : get_sys_color(brTextDisabled));
    draw_badge(FONT_SMALLEST, "S", s.x + 6, s.y, s.h, g_app->song.solo[t] ? get_sys_color(brTextWarning) : get_sys_color(brTextDisabled));
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
  }
  set_clip_rect(win, R(0, 0, win->frame.w, win->frame.h));
}

static void paint_sheet(window_t *win) {
  irect16_t cr = get_client_rect(win), grid = grid_rect(win);
  const song_t *s = &g_app->song;
  const drag_t *d = &g_app->drag;
  int rh = row_h(win), bar = bar_frames_for_bpm(s->bpm);
  fill_rect(get_sys_color(brWorkspaceBg), cr);
  set_clip_rect(win, grid);
  for (int t = 0; t < GR_TRACKS; t++)
    fill_rect(get_sys_color(t % 2 ? brColumnViewBg : brWindowDarkBg), R(grid.x, track_y(win, t), grid.w, rh));
  for (int b = 0; b <= GR_BARS; b++)
    fill_rect(color_with_alpha(get_sys_color(brLightEdge), b % 4 ? 0x14 : 0x38), R(bar_x(win, b), grid.y, 1, grid.h));
  for (int i = 0; i < s->nclips; i++) {
    const clip_t *c = &s->clips[i];
    const block_t *b = block_get(c->block);
    bool lifted = d->active && d->from_clip == i;
    uint32_t color = category_color(b->cat);
    draw_clip(win, b, clip_rect(win, c->track, c->bar, b->bars), lifted ? color_with_alpha(color, 0x60) : color, i == g_app->selected_clip);
  }
  if (d->active && d->track >= 0) {
    const block_t *b = block_get(d->block);
    draw_clip(win, b, clip_rect(win, d->track, d->bar, b->bars),
              color_with_alpha(d->valid ? category_color(b->cat) : get_sys_color(brTextError), 0xb0), false);
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
    GR_TRACE("track=%d %s", t, m ? "mute" : "solo");
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
      if (!(st = allocate_window_data(win, sizeof(*st)))) { fprintf(stderr, "[sh] allocation failed win=%u\n", (unsigned)win->id); fflush(stderr); return false; }
      st->press_clip = -1;
      sync_scroll(win);
      return true;
    case evResize: sync_scroll(win); invalidate_window(win); return false;
    case evHScroll: invalidate_window(win); return true;
    case evPaint: paint_sheet(win); return true;

    case evLeftButtonDown: {
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam), cx = mx - hpos(win);
      GR_TRACE("sheet down win=%u mx=%d my=%d cx=%d hpos=%d", (unsigned)win->id, mx, my, cx, hpos(win));
      if (my < RULER_H) { if (cx >= HDR_W) app_seek_bar((mx - HDR_W) / BAR_W); return true; }
      if (header_click(win, cx, my)) return true;
      int track = (my - RULER_H) / row_h(win), bar = floordiv(mx - HDR_W, BAR_W);
      if (track >= GR_TRACKS) return true;
      st->press_clip = song_clip_at(&g_app->song, track, bar);
      st->press = (ipoint16_t){ (int16_t)mx, (int16_t)my };
      app_select_clip(st->press_clip);
      if (st->press_clip >= 0) {
        const clip_t *c = &g_app->song.clips[st->press_clip];
        g_app->drag = (drag_t){ .block = c->block, .from_clip = st->press_clip, .grab_bars = bar - c->bar, .track = c->track, .bar = c->bar, .valid = true };
        app_preview(c->block);
        set_capture(win);
      }
      return true;
    }
    case evMouseMove:
      if (st->press_clip < 0) return false;
      {
        int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
        if (!st->own_drag && abs(mx - st->press.x) + abs(my - st->press.y) > SHEET_SLOP) { st->own_drag = true; g_app->drag.active = true; GR_TRACE("clip drag start clip=%d", st->press_clip); }
        if (st->own_drag) { drag_target(win, mx - hpos(win), my); invalidate_window(win); }
      }
      return true;
    case evLeftButtonUp:
      if (st->press_clip < 0) return false;
      if (st->own_drag && g_app->drag.track >= 0 && g_app->drag.valid) app_drop(&g_app->drag);
      st->press_clip = -1;
      st->own_drag = false;
      set_capture(NULL);
      drag_clear(win);
      return true;
    case evRightButtonDown: {
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
      if (my < RULER_H || mx - hpos(win) < HDR_W) return false;
      int idx = song_clip_at(&g_app->song, (my - RULER_H) / row_h(win), floordiv(mx - HDR_W, BAR_W));
      if (idx < 0) return false;
      app_select_clip(idx);
      app_command(ID_DELETE);
      return true;
    }

    case shDragOver:
    case shDrop: {
      int cx = (int)LOWORD(wparam) - window_screen_x(win), cy = (int)HIWORD(wparam) - window_screen_y(win);
      drag_t prev = g_app->drag;
      drag_target(win, cx, cy);
      const drag_t *d = &g_app->drag;
      if (msg == shDrop || d->track != prev.track || d->bar != prev.bar || d->valid != prev.valid)
        GR_TRACE("sheet %s win=%u cx=%d cy=%d track=%d bar=%d valid=%d", msg == shDrop ? "drop" : "over", (unsigned)win->id, cx, cy, d->track, d->bar, d->valid);
      if (msg == shDrop && g_app->drag.track >= 0 && g_app->drag.valid) app_drop(&g_app->drag);
      if (msg == shDrop) drag_clear(win); else invalidate_window(win);
      return true;
    }
    case shDragEnd: drag_clear(win); return true;
    case evPointerCancel:
      if (st->press_clip >= 0) { st->press_clip = -1; st->own_drag = false; set_capture(NULL); drag_clear(win); }
      return true;
    case evDestroy:
      if (st && st->press_clip >= 0) set_capture(NULL);
      free(st);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}
