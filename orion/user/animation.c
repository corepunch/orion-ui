#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <platform/platform.h>
#include "user.h"
#include "messages.h"
#include "rect.h"
#include "animation.h"

// ── Frame clock ────────────────────────────────────────────────────────────
// Inside a frame every caller sees the frame's sample; outside one (input handlers that start an
// animation) the live clock, so an animation started after an idle stretch starts now.
static uint32_t g_anim_frame_time;
static bool g_anim_in_frame;
static uint32_t (*g_anim_clock)(void);

static uint32_t anim_now(void) { return g_anim_clock ? g_anim_clock() : (uint32_t)axGetMilliseconds(); }

void ui_animation_set_clock(uint32_t (*clock)(void)) { g_anim_clock = clock; }

void ui_animation_begin_frame(void) {
  g_anim_frame_time = anim_now();
  g_anim_in_frame = true;
}

void ui_animation_end_frame(void) { g_anim_in_frame = false; }

uint32_t ui_animation_time(void) {
  return g_anim_in_frame ? g_anim_frame_time : anim_now();
}

void request_animate(window_t *win) {
  if (!win || !is_window(win)) {
    fprintf(stderr, "[anim] request_animate rejected win=%u\n", win ? win->id : 0);
    fflush(stderr);
    return;
  }
  post_message(win, evAnimate, 0, NULL);
}

// ── Curves ─────────────────────────────────────────────────────────────────
static float anim_bezier(float t, float x1, float y1, float x2, float y2) {
  // Solve x(u) = t for u by Newton steps, then evaluate y(u), as CSS cubic-bezier() does.
  float u = t;
  for (int i = 0; i < 8; i++) {
    float x = ((1 - 3 * x2 + 3 * x1) * u + (3 * x2 - 6 * x1)) * u * u + 3 * x1 * u - t;
    float dx = (3 * (1 - 3 * x2 + 3 * x1) * u + 2 * (3 * x2 - 6 * x1)) * u + 3 * x1;
    if (fabsf(x) < 1e-5f || fabsf(dx) < 1e-6f) break;
    u = fminf(1, fmaxf(0, u - x / dx));
  }
  return ((1 - 3 * y2 + 3 * y1) * u + (3 * y2 - 6 * y1)) * u * u + 3 * y1 * u;
}

float ui_curve_ease(ui_curve_kind_t kind, float t) {
  t = fminf(1, fmaxf(0, t));
  switch (kind) {
    case UI_CURVE_EASE_OUT:    return anim_bezier(t, 0, 0, .58f, 1);
    case UI_CURVE_EASE_IN_OUT: return anim_bezier(t, .42f, 0, .58f, 1);
    default:                   return t;
  }
}

// Displacement from the target of a damped spring released at x0 with velocity v0 (per second).
static float anim_spring(float x0, float v0, float response, float damping, float t) {
  float w = 2 * (float)M_PI / response;
  if (damping < 1) {
    float wd = w * sqrtf(1 - damping * damping);
    return expf(-damping * w * t) * (x0 * cosf(wd * t) + (v0 + damping * w * x0) / wd * sinf(wd * t));
  }
  if (damping == 1) return expf(-w * t) * (x0 + (v0 + w * x0) * t);
  float root = w * sqrtf(damping * damping - 1);
  float r1 = -damping * w + root, r2 = -damping * w - root;
  float c1 = (v0 - r2 * x0) / (r1 - r2);
  return c1 * expf(r1 * t) + (x0 - c1) * expf(r2 * t);
}

static float anim_elapsed(const ui_anim_t *a) {
  return (float)(int32_t)(ui_animation_time() - a->start);
}

static float anim_value_at(const ui_anim_t *a, float ms) {
  if (a->curve.kind == UI_CURVE_SPRING)
    return a->to + anim_spring(a->from - a->to, a->velocity, a->curve.time / 1000, a->curve.damping, ms / 1000);
  float t = a->curve.time > 0 ? ms / a->curve.time : 1;
  return a->from + (a->to - a->from) * ui_curve_ease(a->curve.kind, t);
}

static bool anim_curve_valid(ui_curve_t c) {
  return isfinite(c.time) && c.time >= 0 && c.kind <= UI_CURVE_SPRING &&
         (c.kind != UI_CURVE_SPRING || (c.time > 0 && isfinite(c.damping) && c.damping > 0));
}

void ui_anim_start(ui_anim_t *a, float from, float to, float velocity, ui_curve_t curve) {
  if (!a || !anim_curve_valid(curve) || !isfinite(from) || !isfinite(to) || !isfinite(velocity)) {
    fprintf(stderr, "[anim] start rejected from=%g to=%g velocity=%g kind=%d time=%g damping=%g\n",
            from, to, velocity, (int)curve.kind, curve.time, curve.damping);
    fflush(stderr);
    if (a) ui_anim_set(a, isfinite(to) ? to : 0);
    return;
  }
  *a = (ui_anim_t){from, to, velocity, ui_animation_time(), curve, true};
}

void ui_anim_retarget(ui_anim_t *a, float to, ui_curve_t curve) {
  if (!a) return;
  ui_anim_start(a, ui_anim_value(a), to, ui_anim_velocity(a), curve);
}

void ui_anim_set(ui_anim_t *a, float value) {
  if (a) *a = (ui_anim_t){value, value, 0, ui_animation_time(), UI_EASE_OUT(0), false};
}

float ui_anim_value(const ui_anim_t *a) {
  if (!a) return 0;
  return a->running ? anim_value_at(a, anim_elapsed(a)) : a->to;
}

float ui_anim_velocity(const ui_anim_t *a) {
  if (!a || !a->running) return 0;
  float ms = anim_elapsed(a);
  return (anim_value_at(a, ms + .5f) - anim_value_at(a, ms - .5f)) * 1000;
}

float ui_anim_step(ui_anim_t *a) {
  if (!a || !a->running) return a ? a->to : 0;
  float ms = anim_elapsed(a);
  bool done;
  if (a->curve.kind == UI_CURVE_SPRING) {
    // Settled once both the distance and the speed are invisible at this scale.
    float range = fmaxf(fabsf(a->to - a->from), fmaxf(fabsf(a->velocity) * .05f, 1e-3f));
    float eps = range * 1e-3f;
    float x = anim_value_at(a, ms) - a->to, v = ui_anim_velocity(a);
    done = (fabsf(x) < eps && fabsf(v) < eps * 20) || ms > a->curve.time * 8;
  } else {
    done = ms >= a->curve.time;
  }
  if (done) a->running = false;
  return ui_anim_value(a);
}

// ── Velocity ───────────────────────────────────────────────────────────────
#define VELOCITY_WINDOW_MS 100u
#define VELOCITY_REST_MS   50u

void ui_velocity_reset(ui_velocity_t *v, float value) {
  if (!v) return;
  *v = (ui_velocity_t){0};
  ui_velocity_add(v, value);
}

void ui_velocity_add(ui_velocity_t *v, float value) {
  if (!v) return;
  v->head = (v->head + 1) % ARRAY_LEN(v->value);
  v->value[v->head] = value;
  v->time[v->head] = anim_now();
  if (v->count < ARRAY_LEN(v->value)) v->count++;
}

float ui_velocity_get(const ui_velocity_t *v) {
  if (!v || v->count < 2) return 0;
  uint32_t now = anim_now(), newest = v->time[v->head];
  if (now - newest > VELOCITY_REST_MS) return 0;
  int oldest = v->head;
  for (int i = 1; i < v->count; i++) {
    int k = (v->head - i + ARRAY_LEN(v->value)) % ARRAY_LEN(v->value);
    if (newest - v->time[k] > VELOCITY_WINDOW_MS) break;
    oldest = k;
  }
  uint32_t dt = newest - v->time[oldest];
  return dt ? (v->value[v->head] - v->value[oldest]) * 1000.0f / (float)dt : 0;
}

// ── AnimateWindow ──────────────────────────────────────────────────────────
typedef struct {
  window_t *win;
  uint32_t id, flags;
  irect16_t from;
  ui_anim_t progress;    // 0 at `from`, 1 at the frame
} window_animation_t;

static window_animation_t *g_window_anims;
static int g_window_anim_count, g_window_anim_cap;

static window_animation_t *window_animation_find(const window_t *win) {
  for (int i = 0; i < g_window_anim_count; i++)
    if (g_window_anims[i].win == win) return &g_window_anims[i];
  return NULL;
}

bool window_is_animating(const window_t *win) {
  return win && window_animation_find(win) != NULL;
}

static float anim_lerp(float a, float b, float t) { return a + (b - a) * t; }

// The window's content fills the growing rectangle the way a photo fills a frame: scaled until it
// covers it, centred, the overflow cropped.
static window_visual_t window_animation_visual(const window_t *win, const window_animation_t *wa, float p) {
  float w = MAX(1, win->frame.w), h = MAX(1, win->frame.h);
  window_visual_t v = {.opacity = 1};
  v.rect = (frect_t){anim_lerp(wa->from.x, 0, p), anim_lerp(wa->from.y, 0, p),
                     anim_lerp(wa->from.w, w, p), anim_lerp(wa->from.h, h, p)};
  if (!(wa->flags & ORION_AW_ZOOM)) v.rect = (frect_t){0, 0, w, h};
  float scale = fmaxf(fmaxf(v.rect.w / w, v.rect.h / h), 1e-3f);
  float sw = fminf(w, v.rect.w / scale), sh = fminf(h, v.rect.h / scale);
  v.source = (frect_t){(w - sw) / 2, (h - sh) / 2, sw, sh};
  // Zooming, it is opaque within the first part of the way out of `from`, so the thumbnail below
  // hands over to it; a plain blend fades over the whole animation.
  if (wa->flags & ORION_AW_BLEND) v.opacity = fminf(1, fmaxf(0, (wa->flags & ORION_AW_ZOOM) ? p / .35f : p));
  return v;
}

static void window_animation_finish(window_animation_t *wa) {
  window_t *win = wa->win;
  uint32_t flags = wa->flags;
  *wa = g_window_anims[--g_window_anim_count];
  window_set_visual(win, NULL);
  if (flags & ORION_AW_HIDE) show_window(win, false);
  invalidate_window(win);
  if (win->parent) send_message(win->parent, evCommand, MAKEDWORD(win->id, awnFinished), win);
}

bool animate_window(window_t *win, irect16_t from, uint32_t flags, ui_curve_t curve) {
  if (!win || !is_window(win) || !win->parent || from.w <= 0 || from.h <= 0 ||
      (flags & ~(ORION_AW_ZOOM | ORION_AW_BLEND | ORION_AW_HIDE)) || !(flags & (ORION_AW_ZOOM | ORION_AW_BLEND)) || !anim_curve_valid(curve)) {
    fprintf(stderr, "[anim] animate_window rejected win=%u parent=%u from=%d,%d,%d,%d flags=0x%x kind=%d time=%g\n",
            win ? win->id : 0, win && win->parent ? win->parent->id : 0, from.x, from.y, from.w, from.h,
            flags, (int)curve.kind, curve.time);
    fflush(stderr);
    return false;
  }
  window_animation_t *wa = window_animation_find(win);
  if (wa) {
    // A new animation replaces the running one; its owner hears that the old one ended.
    window_animation_finish(wa);
    if (!is_window(win)) return false;
  }
  if (g_window_anim_count == g_window_anim_cap) {
    int cap = g_window_anim_cap ? g_window_anim_cap * 2 : 4;
    window_animation_t *grown = realloc(g_window_anims, (size_t)cap * sizeof(*grown));
    if (!grown) {
      fprintf(stderr, "[anim] animate_window allocation failed win=%u cap=%d\n", win->id, cap);
      fflush(stderr);
      return false;
    }
    g_window_anims = grown;
    g_window_anim_cap = cap;
  }
  wa = &g_window_anims[g_window_anim_count++];
  *wa = (window_animation_t){win, win->id, flags, from};
  bool hide = flags & ORION_AW_HIDE;
  ui_anim_start(&wa->progress, hide ? 1 : 0, hide ? 0 : 1, 0, curve);
  window_visual_t visual = window_animation_visual(win, wa, hide ? 1 : 0);
  window_set_visual(win, &visual);
  if (!window_has_state(win, WINDOW_STATE_VISIBLE)) show_window(win, true);
  request_animate(win);
  invalidate_window(win);
  return true;
}

bool window_animation_step(window_t *win) {
  window_animation_t *wa = window_animation_find(win);
  if (!wa) return false;
  float p = ui_anim_step(&wa->progress);
  if (!wa->progress.running) {
    window_animation_finish(wa);
    return true;
  }
  window_visual_t visual = window_animation_visual(win, wa, p);
  window_set_visual(win, &visual);
  invalidate_window(win);
  request_animate(win);
  return true;
}

void window_animation_release(window_t *win) {
  window_animation_t *wa = window_animation_find(win);
  if (wa) *wa = g_window_anims[--g_window_anim_count];
}

// ── Coordinates ────────────────────────────────────────────────────────────
static ipoint16_t anim_client_origin(const window_t *win) {
  return (ipoint16_t){window_screen_x(win), window_screen_y(win) + titlebar_height(win)};
}

irect16_t map_window_rect(const window_t *from, const window_t *to, irect16_t r) {
  ipoint16_t a = from ? anim_client_origin(from) : (ipoint16_t){0, 0};
  ipoint16_t b = to ? anim_client_origin(to) : (ipoint16_t){0, 0};
  return rect_offset(r, a.x - b.x, a.y - b.y);
}
