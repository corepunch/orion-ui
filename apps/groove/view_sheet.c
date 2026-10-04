// VIEW: the sheet — track headers on the left, bar ruler on top, one lane per
// track. Blocks snap to whole bars. It is the drop target for tiles dragged
// from the bin (shDragOver / shDrop). Each clip is a child window; dragging
// one lifts that window with window_set_drag_visual.
//
// Coordinates: mouse messages arrive in content space (client + scroll), so
// `mx - hpos` is the client x. Clip windows receive their own client space.
// Painting is in client space.

#include "groove.h"

#define HDR_W      78
#define RULER_H    22
#define BAR_W      88
#define MIN_ROW    26
#define MAX_ROW    64
#define SHEET_SLOP 4

typedef struct { int press_clip; ipoint16_t press; bool own_drag; uint32_t track_icons; } sheet_t;

static result_t win_clip(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

static int  hpos(window_t *win)    { return get_scroll_pos(win, SB_HORZ); }
static int  row_h(window_t *win)   { return CLAMP((get_client_rect(win).h - RULER_H) / GR_TRACKS, MIN_ROW, MAX_ROW); }
static int  floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int  bar_x(window_t *win, int bar)  { return HDR_W + bar * BAR_W - hpos(win); }
static int  track_y(window_t *win, int t)  { return RULER_H + t * row_h(win); }
static irect16_t grid_rect(window_t *win)  { irect16_t cr = get_client_rect(win); return R(HDR_W, RULER_H, cr.w - HDR_W, row_h(win) * GR_TRACKS); }
static irect16_t mute_rect(window_t *win, int t) { return rect_center(R(HDR_W - 52, track_y(win, t), 24, row_h(win)), 24, 24); }
static irect16_t solo_rect(window_t *win, int t) { return rect_offset(mute_rect(win, t), 26, 0); }

static uint32_t load_track_icons(window_t *win) {
  if (!g_ui_runtime.running) return 0;
  char path[1024];
  snprintf(path, sizeof(path), "%s/../share/groove/icons/track-controls.png", ui_get_exe_dir());
  int w = 0, h = 0;
  uint8_t *pixels = load_image(path, &w, &h);
  if (!pixels || w != 128 || h != 128) {
    fprintf(stderr, "[gr] track icons unavailable win=%u path=%s size=%dx%d\n", win->id, path, w, h);
    fflush(stderr);
    image_free(pixels);
    return 0;
  }
  uint32_t texture = R_CreateTextureSRGBA8(w, h, pixels, R_FILTER_LINEAR, R_WRAP_CLAMP);
  image_free(pixels);
  if (!texture) { fprintf(stderr, "[gr] track icon texture failed win=%u\n", win->id); fflush(stderr); }
  return texture;
}

static void draw_track_toggle(window_t *win, irect16_t r, int icon, bool active) {
  const sheet_t *st = win->userdata;
  theme_draw(THEME_PART_TOOLBAR_BUTTON, r, active ? CTRL_SELECTED : CTRL_NORMAL);
  if (!st->track_icons) return;
  float x = icon * 0.5f, y = active ? 0.5f : 0.0f;
  draw_sprite_region(st->track_icons, r, UV_RECT(x, y, x + 0.5f, y + 0.5f), 0xffffffffu, 0);
}

static void sync_scroll(window_t *win) {
  scroll_info_t si = { .fMask = SIF_RANGE | SIF_PAGE | SIF_POS, .nMin = 0, .nMax = GR_BARS * BAR_W,
                       .nPage = get_client_rect(win).w - HDR_W, .nPos = hpos(win) };
  set_scroll_info(win, SB_HORZ, &si, false);
}

static irect16_t clip_rect(window_t *win, int track, int bar, int bars) {
  return R(bar_x(win, bar) + 1, track_y(win, track) + 2, bars * BAR_W - 2, row_h(win) - 4);
}

static int child_index(window_t *win) {
  int i = 0;
  if (!win || !win->parent) return -1;
  for (window_t *c = win->parent->children; c; c = c->next, i++)
    if (c == win) return i;
  return -1;
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
    if (!create_window("", GR_CARD_FLAGS, MAKERECT(0, 0, 1, 1), win, win_clip, 0, NULL)) {
      fprintf(stderr, "[sh] clip allocation failed index=%d\n", child_count(win));
      fflush(stderr);
      break;
    }
  }
  int i = 0;
  for (window_t *c = win->children; c && i < n; c = c->next, i++) {
    const clip_t *cl = &g_app->song.clips[i];
    const block_t *b = block_get(cl->block);
    card_place(c, R(bar_x(win, cl->bar), track_y(win, cl->track), b->bars * BAR_W, row_h(win)));
  }
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
  window_t *card = child_at(win, g_app->drag.from_clip);
  if (card) window_clear_drag_visual(card);
  g_app->drag = (drag_t){ .track = -1, .from_clip = -1 };
  invalidate_window(win);
}

// Bottom pixel row of a rounded card at column x, so the waveform sits on the
// silhouette instead of a padded box. radius 0 is a square bottom.
static int card_bottom(int x, int w, int h, int radius) {
  if (h <= 0) return 0;
  if (radius <= 0) return h - 1;
  if (radius > w / 2) radius = w / 2;
  if (radius > h / 2) radius = h / 2;
  float px = x + 0.5f, dx = 0;
  if (px < radius) dx = radius - px;
  else if (px > w - radius) dx = px - (w - radius);
  if (dx <= 0) return h - 1;
  float dy = sqrtf((float)radius * radius - dx * dx);
  int row = (int)((float)h - radius + dy - 0.5f);
  return row < 0 ? 0 : row > h - 1 ? h - 1 : row;
}

void draw_clip(window_t *win, const block_t *b, irect16_t r, uint32_t color, ctrl_state_t state) {
  draw_gradient_card(r, state, color);
  r = rect_inset(r, get_theme()->card_ring_width);
  int radius = MAX(0, get_theme()->card_corner_radius - get_theme()->card_ring_width);
  uint32_t ink = color_with_alpha(get_sys_color(brTextOnColor), (color >> 24) * 0x99 / 255);
  int np = b->audio.npeaks;
  for (int x = 0; x < r.w && np > 1; x++) {
    float f = (x + 0.5f) * np / r.w - 0.5f;
    int k = f < 0 ? 0 : (int)f, k1 = k + 1 < np ? k + 1 : np - 1;
    float t = f < 0 ? 0 : f - k, p = b->audio.peaks[k] + (b->audio.peaks[k1] - b->audio.peaks[k]) * t;
    int h = MAX(1, (int)(p * MAX(1, r.h - 14) / 255));
    int bottom = r.y + card_bottom(x, r.w, r.h, radius);
    int y = bottom + 1 - h;
    if (y < r.y) { h -= r.y - y; y = r.y; }
    if (h > 0) fill_rect(ink, R(r.x + x, y, 1, h));
  }
  draw_text_ellipsized(FONT_SMALL, b->name, r.x + 4, r.y + 3, r.w - 8, color_with_alpha(get_sys_color(brTextOnColor), color >> 24));
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
    draw_track_toggle(win, m, 0, g_app->song.mute[t]);
    draw_track_toggle(win, s, 1, g_app->song.solo[t]);
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
  int bar = bar_frames_for_bpm(s->bpm);
  sync_clips(win);
  fill_rect(get_sys_color(brWorkspaceBg), cr);
  set_clip_rect(win, grid);
  for (int t = 0; t < GR_TRACKS; t++)
    fill_rect(get_sys_color(t % 2 ? brColumnViewBg : brWindowDarkBg), R(grid.x, track_y(win, t), grid.w, row_h(win)));
  for (int b = 0; b <= GR_BARS; b++)
    fill_rect(color_with_alpha(get_sys_color(brLightEdge), b % 4 ? 0x14 : 0x38), R(bar_x(win, b), grid.y, 1, grid.h));
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
                        clip_rect(win, d->track, d->bar, b->bars), get_theme()->card_corner_radius, 2);
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
    GR_TRACE("track win=%p selected=%d track=%d mute=%d solo=%d", (void *)win, g_app->selected_clip, t, g_app->song.mute[t], g_app->song.solo[t]);
    invalidate_window(win);
    return true;
  }
  return true;
}

// The sheet owns the pointer. Clip windows only paint, so a click falls
// through to the canvas and a two-finger pan can cancel the drag.
static result_t win_clip(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (!g_app && msg != evDestroy) return false;
  switch (msg) {
    case evCreate: return true;
    case evPaint: {
      int idx = child_index(win);
      if (idx < 0 || idx >= g_app->song.nclips) return true;
      const clip_t *c = &g_app->song.clips[idx];
      const block_t *b = block_get(c->block);
      irect16_t cr = get_client_rect(win);
      draw_clip(win, b, R(1, 2, cr.w - 2, cr.h - 4), category_color(b->cat), idx == g_app->selected_clip ? CTRL_SELECTED : CTRL_NORMAL);
      return true;
    }
    case evDestroy: return true;
    default: return false;
  }
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
      st->track_icons = load_track_icons(win);
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
      if (my < RULER_H) { if (cx >= HDR_W) app_seek_bar((mx - HDR_W) / BAR_W); return true; }
      if (header_click(win, cx, my)) return true;
      int track = (my - RULER_H) / row_h(win), bar = floordiv(mx - HDR_W, BAR_W);
      if (track < 0 || track >= GR_TRACKS) return true;
      st->press_clip = song_clip_at(&g_app->song, track, bar);
      st->press = (ipoint16_t){ (int16_t)mx, (int16_t)my };
      app_select_clip(st->press_clip);
      if (st->press_clip >= 0) {
        const clip_t *c = &g_app->song.clips[st->press_clip];
        g_app->drag = (drag_t){ .block = c->block, .from_clip = st->press_clip, .grab_bars = bar - c->bar,
                                .track = c->track, .bar = c->bar, .valid = true };
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
          if (card) window_set_drag_visual(card, mx - st->press.x, my - st->press.y);
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
      if (st && st->track_icons) R_DeleteTexture(st->track_icons);
      free(st);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}
