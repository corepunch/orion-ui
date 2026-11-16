#ifndef __UI_ANIMATION_H__
#define __UI_ANIMATION_H__

#include <stdbool.h>
#include <stdint.h>
#include <orion/user/user.h>

// Animation, the WinAPI way: nothing runs on its own clock or thread. A window that is animating
// asks for one evAnimate (request_animate), reads the shared frame clock when it arrives, moves its
// state, invalidates, and asks again while it is still moving. Requests coalesce in the posted
// queue, so a window gets at most one evAnimate per frame however often it asks, and the vsynced
// frame loop paces them. See docs/animation.md.

// ── Frame clock (≈ IUIAnimationTimer::GetTime) ─────────────────────────────
// Milliseconds, sampled once at the start of each frame: every animation stepped in one frame
// sees the same time, so things that move together stay together.
uint32_t ui_animation_time(void);
void     ui_animation_begin_frame(void);   // repost_messages() brackets each frame with these
void     ui_animation_end_frame(void);
// Replaces the time source (NULL restores the platform's monotonic clock), as tests do to step
// animations deterministically. Pointer velocity reads the same source.
void     ui_animation_set_clock(uint32_t (*clock)(void));
// Posts evAnimate to win (≈ OBJ_RequestAnimate). Repeated requests before it is dispatched merge.
void     request_animate(window_t *win);

// ── Timing curves (≈ IUIAnimationTransitionLibrary) ────────────────────────
typedef enum {
  UI_CURVE_LINEAR,
  UI_CURVE_EASE_OUT,       // cubic-bezier(0, 0, .58, 1)
  UI_CURVE_EASE_IN_OUT,    // cubic-bezier(.42, 0, .58, 1), UIView's default
  UI_CURVE_SPRING,         // SwiftUI .spring(response:, dampingFraction:); carries velocity
} ui_curve_kind_t;

typedef struct {
  ui_curve_kind_t kind;
  float time;              // ms: duration of an eased curve, response (period) of a spring
  float damping;           // spring damping ratio: 1 settles without overshoot, < 1 bounces
} ui_curve_t;

#define UI_EASE_OUT(ms)              ((ui_curve_t){UI_CURVE_EASE_OUT, (ms), 0})
#define UI_EASE_IN_OUT(ms)           ((ui_curve_t){UI_CURVE_EASE_IN_OUT, (ms), 0})
#define UI_SPRING(response_ms, damp) ((ui_curve_t){UI_CURVE_SPRING, (response_ms), (damp)})

// System curves, so controls that move alike feel alike (≈ UIKit's stock springs).
#define UI_CURVE_PRESS     UI_EASE_OUT(90)        // a control sinking under a finger
#define UI_CURVE_RELEASE   UI_SPRING(320, .52f)   // and bouncing back
#define UI_CURVE_PAGING    UI_SPRING(380, 1)      // UIScrollView paging snap
#define UI_CURVE_ZOOM      UI_SPRING(480, .9f)    // a view growing out of its thumbnail

float ui_curve_ease(ui_curve_kind_t kind, float t);   // eased curves: 0..1 -> 0..1

// ── An animated value (≈ IUIAnimationVariable driven by one transition) ───
// Plain data, owned by whoever animates: embed it in window state.
typedef struct {
  float from, to;
  float velocity;          // units per second at `start`; springs continue it
  uint32_t start;          // ui_animation_time() at the start
  ui_curve_t curve;
  bool running;
} ui_anim_t;

// Starts from `from` towards `to`. Velocity matters to springs only (a fling handed to a snap).
void  ui_anim_start(ui_anim_t *a, float from, float to, float velocity, ui_curve_t curve);
// Starts from the current value and velocity, so a retargeted motion never jumps.
void  ui_anim_retarget(ui_anim_t *a, float to, ui_curve_t curve);
void  ui_anim_set(ui_anim_t *a, float value);     // holds a value, stopped
float ui_anim_value(const ui_anim_t *a);          // at ui_animation_time()
float ui_anim_velocity(const ui_anim_t *a);       // units per second
// Advances to the frame clock; clears `running` once the curve ends or the spring settles and
// returns the value (exactly `to` at the end). Call from evAnimate, then request again while running.
float ui_anim_step(ui_anim_t *a);

// ── Pointer velocity (≈ UIPanGestureRecognizer.velocity) ──────────────────
// Feed positions as they arrive; the velocity is measured over the last ~100 ms.
typedef struct {
  float value[8];
  uint32_t time[8];
  int count, head;
} ui_velocity_t;

void  ui_velocity_reset(ui_velocity_t *v, float value);
void  ui_velocity_add(ui_velocity_t *v, float value);
float ui_velocity_get(const ui_velocity_t *v);    // units per second; 0 when the pointer rested

// ── Compositor visual (≈ IDCompositionVisual SetTransform / SetClip / SetOpacity) ──
// A child window with a visual paints its subtree into its own surface, at its own size, and the
// window system draws `source` of that surface into `rect` with `opacity`. Both rects are in the
// window's own coordinates; the identity visual is source = rect = {0, 0, w, h}, opacity 1.
// Layout, hit-testing and the window's frame are unchanged: a visual moves pixels, not the window.
typedef struct {
  frect_t rect;
  frect_t source;
  float opacity;
} window_visual_t;

void window_set_visual(window_t *win, const window_visual_t *visual);   // NULL restores identity
bool window_get_visual(const window_t *win, window_visual_t *visual);   // false when identity

// ── Bitmap cache (≈ WPF UIElement.CacheMode = BitmapCache) ─────────────────
// A cached child paints its subtree into its surface once; later frames composite those pixels,
// through its visual when it has one, without calling its window procedures. A scaled or fading
// cached window therefore never lays out or rasterizes its text again. The cache is repainted after
// window_invalidate_cache() (call it when the content changes), a theme change, or a new size.
void window_set_cache(window_t *win, bool cached);
void window_invalidate_cache(window_t *win);
bool window_is_cached(const window_t *win);
bool window_cache_current(const window_t *win);   // framework: the cached pixels can be reused
void window_cache_validate(window_t *win);         // framework: the surface was just repainted

// ── AnimateWindow ──────────────────────────────────────────────────────────
// ≈ AnimateWindow(hwnd, time, AW_*) with DrawAnimatedRects(IDANI_CAPTION)'s zoom from a rectangle,
// but asynchronous. AW_ZOOM grows the window out of `from` (its own coordinates) into its frame, its
// content aspect-filled and cropped to the growing rectangle; AW_BLEND fades it in as it leaves `from`
// (without AW_ZOOM, over the whole animation).
// With AW_HIDE it runs backwards into `from` and hides the window at the end. The window is shown at
// the start and takes no input until it ends. When it ends (or is replaced by another animate_window
// call) the parent receives evCommand MAKEDWORD(win->id, awnFinished) with lparam = win.
enum {
  AW_ZOOM  = 1 << 0,
  AW_BLEND = 1 << 1,
  AW_HIDE  = 1 << 2,
};
bool animate_window(window_t *win, irect16_t from, uint32_t flags, ui_curve_t curve);
bool window_is_animating(const window_t *win);
// Framework hooks: evAnimate for a window animation, and cleanup when the window is destroyed.
bool window_animation_step(window_t *win);
void window_animation_release(window_t *win);

// ≈ MapWindowPoints: a rectangle in `from`'s client coordinates, in `to`'s client coordinates.
irect16_t map_window_rect(const window_t *from, const window_t *to, irect16_t r);

#endif
