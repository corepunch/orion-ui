// SpriteButton and SpriteSlider: controls drawn from bitmap sprites (a skin) instead of the theme.

#include "commctl.h"
#include <orion/user/gdi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void sprite_reject(const char *what, window_t *win) {
  fprintf(stderr, "[sprite] %s win=%u\n", what, win ? win->id : 0);
  fflush(stderr);
}

// Sprite rect scaled by sx, sy; edges are rounded, not sizes, so adjacent sprites never gap.
static irect16_t sprite_scale(irect16_t r, float sx, float sy) {
  int x0 = (int)lroundf(r.x * sx), y0 = (int)lroundf(r.y * sy);
  return R(x0, y0, (int)lroundf((r.x + r.w) * sx) - x0, (int)lroundf((r.y + r.h) * sy) - y0);
}

// ── SpriteButton ────────────────────────────────────────────────────────────

typedef struct { sprite_button_t def; bool pressed, inside; } spb_t;

static bool spb_hit(window_t *win, uint32_t wparam) {
  int x = (int16_t)LOWORD(wparam), y = (int16_t)HIWORD(wparam);
  return x >= 0 && y >= 0 && x < win->frame.w && y < win->frame.h;
}

static void spb_release(window_t *win, spb_t *b) {
  b->pressed = b->inside = false;
  set_capture(NULL);
  invalidate_window(win);
}

result_t win_spritebutton(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  spb_t *b = win->userdata;
  switch (msg) {
    case evCreate:
      if (!(b = allocate_window_data(win, sizeof(spb_t)))) { sprite_reject("button allocation failed", win); return false; }
      return true;
    case spbSetSprites:
      if (!b || !lparam) { sprite_reject("spbSetSprites needs sprites", win); return false; }
      b->def = *(const sprite_button_t *)lparam;
      invalidate_window(win);
      return true;
    case btnSetCheck:
      win->value = wparam ? 1 : 0;
      invalidate_window(win);
      return true;
    case btnGetCheck:
      return win->value != 0;
    case evPaint: {
      if (!b || !b->def.bm) return true;
      bool down = b->pressed && b->inside, on = b->def.toggle && win->value;
      irect16_t src = on && b->def.on.w ? (down && b->def.on_down.w ? b->def.on_down : b->def.on) : (down ? b->def.down : b->def.up);
      if (!src.w) src = b->def.up;
      stretch_blt(R(0, 0, win->frame.w, win->frame.h), b->def.bm, src);
      return true;
    }
    case evQueryDrag:
      return DRAG_NOW;
    case evLeftButtonDown:
      if (!b || window_has_state(win, WINDOW_STATE_DISABLED)) return false;
      b->pressed = b->inside = true;
      set_capture(win);
      invalidate_window(win);
      return true;
    case evMouseMove:
      if (!b || !b->pressed) return false;
      if (b->inside != spb_hit(win, wparam)) { b->inside = !b->inside; invalidate_window(win); }
      return true;
    case evLeftButtonUp: {
      if (!b || !b->pressed) return false;
      bool fire = b->inside;
      spb_release(win, b);
      if (fire) {
        if (b->def.auto_check) win->value = !win->value;
        if (win->parent) send_message(win->parent, evCommand, MAKEDWORD(win->id, btnClicked), win);
      }
      return true;
    }
    case evPointerCancel:
      if (b && b->pressed) spb_release(win, b);
      return true;
    case evDestroy:
      if (b) { free(b); win->userdata = NULL; }
      return true;
    default:
      return false;
  }
}

// ── SpriteSlider ────────────────────────────────────────────────────────────

typedef struct { sprite_slider_t def; int min_val, max_val, pos; bool dragging, thumb_visible; } sps_t;

static float sps_norm(const sps_t *s) {
  int range = s->max_val - s->min_val;
  return range > 0 ? (float)(s->pos - s->min_val) / (float)range : 0.0f;
}

static void sps_scales(const window_t *win, const sps_t *s, float *sx, float *sy) {
  *sx = s->def.native.w > 0 ? (float)win->frame.w / s->def.native.w : 1.0f;
  *sy = s->def.native.h > 0 ? (float)win->frame.h / s->def.native.h : 1.0f;
}

// Pointer (window-local, wparam) to a slider value, centring the thumb under the pointer.
static int sps_value_at(const window_t *win, const sps_t *s, uint32_t wparam) {
  float sx, sy;
  sps_scales(win, s, &sx, &sy);
  const sprite_slider_t *d = &s->def;
  float t;
  if (d->vertical) t = d->travel.h > 0 ? 1.0f - ((int16_t)HIWORD(wparam) / sy - d->thumb.h / 2.0f - d->travel.y) / d->travel.h : 0;
  else             t = d->travel.w > 0 ? ((int16_t)LOWORD(wparam) / sx - d->thumb.w / 2.0f - d->travel.x) / d->travel.w : 0;
  t = fmaxf(0.0f, fminf(1.0f, t));
  return s->min_val + (int)lroundf(t * (float)(s->max_val - s->min_val));
}

static void sps_set_pos(window_t *win, sps_t *s, int pos) {
  pos = pos < s->min_val ? s->min_val : pos > s->max_val ? s->max_val : pos;
  if (pos == s->pos) return;
  s->pos = pos;
  invalidate_window(win);
  if (win->parent) send_message(win->parent, evCommand, MAKEDWORD(win->id, sliderValueChanged), win);
}

result_t win_spriteslider(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  sps_t *s = win->userdata;
  switch (msg) {
    case evCreate:
      if (!(s = allocate_window_data(win, sizeof(sps_t)))) { sprite_reject("slider allocation failed", win); return false; }
      s->max_val = 100;
      s->thumb_visible = true;
      return true;
    case spsSetSprites:
      if (!s || !lparam) { sprite_reject("spsSetSprites needs sprites", win); return false; }
      s->def = *(const sprite_slider_t *)lparam;
      if (s->def.frames < 1) s->def.frames = 1;
      if (s->def.columns < 1) s->def.columns = 1;
      invalidate_window(win);
      return true;
    case spsSetThumbVisible:
      if (s) { s->thumb_visible = wparam != 0; invalidate_window(win); }
      return true;
    case spsIsDragging:
      return s && s->dragging;
    case slSetRange: {
      if (!s || !lparam) { sprite_reject("slSetRange needs a range", win); return false; }
      const slider_range_t *r = lparam;
      s->min_val = MIN(r->min_val, r->max_val);
      s->max_val = MAX(r->min_val, r->max_val);
      s->pos = s->pos < s->min_val ? s->min_val : s->pos > s->max_val ? s->max_val : s->pos;
      invalidate_window(win);
      return true;
    }
    case slGetRange:
      if (s && lparam) { slider_range_t *r = lparam; r->min_val = s->min_val; r->max_val = s->max_val; }
      return s != NULL;
    case slSetPos: {
      if (!s) return false;
      int pos = (int)(intptr_t)lparam;
      s->pos = pos < s->min_val ? s->min_val : pos > s->max_val ? s->max_val : pos;   // programmatic: no notification
      invalidate_window(win);
      return true;
    }
    case slGetPos:
      if (!s) return false;
      if (lparam) *(int *)lparam = s->pos;
      return s->pos;
    case evPaint: {
      if (!s || !s->def.bm) return true;
      const sprite_slider_t *d = &s->def;
      float sx, sy, t = sps_norm(s);
      sps_scales(win, s, &sx, &sy);
      float ft = d->by_magnitude ? fabsf(t * 2 - 1) : t;
      int frame = (int)lroundf(ft * (float)(d->frames - 1));
      irect16_t src = R(d->track.x + (frame % d->columns) * d->frame_step.x, d->track.y + (frame / d->columns) * d->frame_step.y, d->track.w, d->track.h);
      stretch_blt(R(0, 0, win->frame.w, win->frame.h), d->bm, src);
      if (!s->thumb_visible) return true;
      const irect16_t th = s->dragging ? d->thumb_down : d->thumb;
      irect16_t at = R(d->travel.x, d->travel.y, th.w, th.h);
      if (d->vertical) at.y += (int)lroundf((1.0f - t) * d->travel.h);
      else             at.x += (int)lroundf(t * d->travel.w);
      stretch_blt(sprite_scale(at, sx, sy), d->bm, th);
      return true;
    }
    case evQueryDrag:
      return DRAG_NOW;
    case evLeftButtonDown:
      if (!s || window_has_state(win, WINDOW_STATE_DISABLED)) return false;
      s->dragging = true;
      set_capture(win);
      sps_set_pos(win, s, sps_value_at(win, s, wparam));
      invalidate_window(win);
      return true;
    case evMouseMove:
      if (!s || !s->dragging) return false;
      sps_set_pos(win, s, sps_value_at(win, s, wparam));
      return true;
    case evLeftButtonUp:
      if (!s || !s->dragging) return false;
      s->dragging = false;
      set_capture(NULL);
      invalidate_window(win);
      if (win->parent) send_message(win->parent, evCommand, MAKEDWORD(win->id, spsnReleased), win);
      return true;
    case evPointerCancel:
      if (s && s->dragging) { s->dragging = false; set_capture(NULL); invalidate_window(win); }
      return true;
    case evDestroy:
      if (s) { free(s); win->userdata = NULL; }
      return true;
    default:
      return false;
  }
}
