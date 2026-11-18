#include "comic_stage.h"
#include <orion/commctl/commctl_internal.h>
#include <math.h>

#define COMIC_PAN_PROJECTION  .15f    // seconds of momentum added to the release position
#define COMIC_PAN_RESISTANCE  .55f    // UIKit's rubber band past either end

typedef struct {
  comic_stage_layout_t layout;
  comic_stage_slide_t slide;
  bool recto[2];           // the spread in that slot is a lone right-hand page
  ipoint16_t down;
  bool tracking, panning;
  bool allowed;            // the root prepared the incoming spread for slide.direction
  int grab;                // finger x at zero offset, while panning
  ui_velocity_t velocity;  // finger x
  ui_anim_t snap;          // progress after a release or for a whole turn
} comic_stage_t;

static int comic_stage_width(window_t *win) { return MAX(1, get_client_rect(win).w); }

// Where a spread rests: the whole stage, or the right-hand page of it for a lone recto.
static irect16_t comic_stage_spread_rect(window_t *win, const comic_stage_t *st, int slot) {
  irect16_t r = get_client_rect(win);
  if (!st->recto[slot]) return r;
  window_t *spread = st->layout.spread[slot];
  return rect_split_right(r, (r.w - layout_spacing_for(spread)) / 2);
}

static void comic_stage_position(window_t *win, comic_stage_t *st) {
  int width = get_client_rect(win).w;
  comic_stage_slide_t slide = st->slide;
  int offset = slide.direction * (int)lroundf(width * slide.progress);
  int front = slide.front, back = 1 - front;
  move_window(st->layout.spread[front], comic_stage_spread_rect(win, st, front).x - offset, 0);
  move_window(st->layout.spread[back], comic_stage_spread_rect(win, st, back).x + slide.direction * width - offset, 0);
  invalidate_window(win);
}

static void comic_stage_arrange(window_t *win, comic_stage_t *st) {
  if (!st->layout.spread[0]) return;
  for (int i = 0; i < 2; i++) layout_arrange_child(st->layout.spread[i], comic_stage_spread_rect(win, st, i));
  irect16_t r = get_client_rect(win);
  comic_stage_position(win, st);
  irect16_t area = rect_inset(r, ui_px(ui_selector(win, COMIC_STAGE_CLASS), "--reader-overlay-inset"));
  window_t *controls[] = {st->layout.shelf, st->layout.back, st->layout.palette, st->layout.forward};
  for (int i = 0; i < ARRAY_LEN(controls); i++) {
    window_t *control = controls[i];
    layout_measure_t m = layout_measure_child(control, area.w, area.h);
    // The shelf button and the palette ride along the top, back and forward along the bottom.
    bool top = control == st->layout.shelf || control == st->layout.palette;
    irect16_t at = top ? rect_split_top(area, m.desired_h) : rect_split_bottom(area, m.desired_h);
    at = i < 2 ? rect_split_left(at, m.desired_w) :
         i == 3 ? rect_split_right(at, m.desired_w) : rect_center(at, m.desired_w, at.h);
    layout_arrange_child(control, at);
  }
}

static window_t *comic_stage_control(const comic_stage_t *st, ipoint16_t p) {
  window_t *controls[] = {st->layout.shelf, st->layout.back, st->layout.palette, st->layout.forward};
  for (int i = 0; i < ARRAY_LEN(controls); i++)
    if (window_is_visible(controls[i]) && rect_contains_point(controls[i]->frame, p)) return controls[i];
  return NULL;
}

static bool comic_stage_notify(window_t *win, uint16_t code) {
  return send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, code), win) != 0;
}

// Points the turn at `direction`, asking the root for the spread that lies that way.
static void comic_stage_aim(window_t *win, comic_stage_t *st, int direction) {
  if (direction == st->slide.direction) return;
  if (st->allowed) comic_stage_notify(win, COMIC_TURN_CANCEL);
  st->allowed = false;
  st->slide.direction = direction;
  if (direction) st->allowed = comic_stage_notify(win, direction > 0 ? COMIC_TURN_QUERY_NEXT : COMIC_TURN_QUERY_BACK);
}

static void comic_stage_finish(window_t *win, comic_stage_t *st, bool turned) {
  st->snap.running = false;
  if (turned && st->allowed) {
    st->allowed = false;
    comic_stage_notify(win, COMIC_TURN_DONE);   // the root brings the incoming spread to the front
    return;
  }
  if (st->allowed) comic_stage_notify(win, COMIC_TURN_CANCEL);
  st->allowed = false;
  st->slide.direction = 0;
  st->slide.progress = 0;
  comic_stage_position(win, st);
}

// Past either end the spread follows the finger ever more reluctantly.
static float comic_stage_rubber(float distance) {
  return 1 - 1 / (distance * COMIC_PAN_RESISTANCE + 1);
}

static float comic_stage_unrubber(float shown) {
  return shown < 1 ? (1 / (1 - shown) - 1) / COMIC_PAN_RESISTANCE : 0;
}

static void comic_stage_drag(window_t *win, comic_stage_t *st, int x) {
  int offset = x - st->grab;
  comic_stage_aim(win, st, offset < 0 ? 1 : offset > 0 ? -1 : st->slide.direction);
  float distance = fabsf((float)offset) / comic_stage_width(win);
  st->slide.progress = st->allowed ? fminf(distance, 1) : comic_stage_rubber(distance);
  comic_stage_position(win, st);
}

static void comic_stage_snap(window_t *win, comic_stage_t *st, float target, float velocity) {
  ui_anim_start(&st->snap, st->slide.progress, target, velocity, UI_CURVE_PAGING);
  request_animate(win);
}

static void comic_stage_release(window_t *win, comic_stage_t *st) {
  // Velocity in progress per second towards the incoming spread, as UIKit projects a paging flick.
  float velocity = st->slide.direction ? -st->slide.direction * ui_velocity_get(&st->velocity) / comic_stage_width(win) : 0;
  float projected = st->slide.progress + velocity * COMIC_PAN_PROJECTION;
  bool turn = st->allowed && projected > .5f;
  comic_stage_snap(win, st, turn ? 1 : 0, st->allowed ? velocity : 0);
}

static void comic_stage_grab(window_t *win, comic_stage_t *st, ipoint16_t p) {
  // Catching a moving spread holds it where it is.
  st->snap.running = false;
  float distance = st->allowed ? st->slide.progress : comic_stage_unrubber(st->slide.progress);
  st->grab = p.x + st->slide.direction * (int)lroundf(distance * comic_stage_width(win));
  st->panning = true;
}

static void comic_stage_settle(window_t *win, comic_stage_t *st) {
  if (st->tracking) set_capture(NULL);
  st->tracking = st->panning = false;
  if (st->snap.running || st->slide.direction) comic_stage_finish(win, st, st->snap.running && st->snap.to == 1);
}

bool comic_stage_turning(window_t *win) {
  comic_stage_t *st = win ? win->userdata : NULL;
  return st && (st->panning || st->snap.running);
}

result_t comic_stage_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  comic_stage_t *st = win->userdata;
  ipoint16_t p = {(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
  switch (msg) {
    case evCreate: return allocate_window_data(win, sizeof(*st)) != NULL;
    case evDestroy: return true;
    case evPaint:
      fill_rect(ui_color(ui_selector(win, COMIC_STAGE_CLASS), "background"), get_client_rect(win));
      return false;
    case evResize:
      if (st) comic_stage_arrange(win, st);
      return true;
    case COMIC_STAGE_CONFIGURE:
      st->layout = *(const comic_stage_layout_t *)lparam;
      comic_stage_arrange(win, st);
      return true;
    case COMIC_STAGE_SLIDE:
      st->slide = *(const comic_stage_slide_t *)lparam;
      comic_stage_position(win, st);
      return true;
    case COMIC_STAGE_TURN: {
      int direction = (int)wparam == 1 ? 1 : (int)wparam == -1 ? -1 : 0;
      if (!direction || comic_stage_turning(win)) return false;
      comic_stage_aim(win, st, direction);
      if (!st->allowed) { st->slide.direction = 0; return false; }
      comic_stage_snap(win, st, 1, 0);
      return true;
    }
    case COMIC_STAGE_RECTO:
      if (wparam > 1) return false;
      if (st->recto[wparam] != (lparam != NULL)) {
        st->recto[wparam] = lparam != NULL;
        comic_stage_arrange(win, st);
      }
      return true;
    case COMIC_STAGE_SETTLE:
      comic_stage_settle(win, st);
      return true;
    case evAnimate:
      if (!st->snap.running) return false;
      st->slide.progress = fminf(1, fmaxf(0, ui_anim_step(&st->snap)));
      comic_stage_position(win, st);
      if (st->snap.running) request_animate(win);
      else comic_stage_finish(win, st, st->snap.to == 1);
      return true;
    case evChildHitTest:
      return (result_t)comic_stage_control(st, p);
    case evHitTest:
      // Dispatch to the artwork from the stage, so an unclaimed press can start a pan.
      return !comic_stage_control(st, p);
    case evLeftButtonDown:
      if (ui_get_pointer().flags & AX_POINTER_STYLUS) return false;
      // fall through
    case COMIC_STAGE_GRAB:
      st->tracking = true;
      st->panning = false;
      st->down = p;
      ui_velocity_reset(&st->velocity, p.x);
      if (st->snap.running) comic_stage_grab(win, st, p);
      set_capture(win);
      return true;
    case evMouseMove:
      if (!st->tracking) return false;
      ui_velocity_add(&st->velocity, p.x);
      if (!st->panning) {
        int dx = p.x - st->down.x, dy = p.y - st->down.y;
        if (abs(dy) > COMIC_PAN_SLOP && abs(dy) > abs(dx)) {
          st->tracking = false;
          set_capture(NULL);
          return true;
        }
        if (abs(dx) <= COMIC_PAN_SLOP) return true;
        // The pan starts where the finger left the slop, so the spread never jumps.
        st->panning = true;
        st->grab = st->down.x + (dx < 0 ? -COMIC_PAN_SLOP : COMIC_PAN_SLOP);
      }
      comic_stage_drag(win, st, p.x);
      return true;
    case evLeftButtonUp:
      if (!st->tracking) return false;
      st->tracking = false;
      set_capture(NULL);
      if (st->panning) {
        ui_velocity_add(&st->velocity, p.x);
        comic_stage_drag(win, st, p.x);
        st->panning = false;
        comic_stage_release(win, st);
      }
      return true;
    case evPointerCancel:
      if (st->tracking) {
        st->tracking = false;
        set_capture(NULL);
        if (st->panning) { st->panning = false; comic_stage_snap(win, st, 0, 0); }
      }
      return true;
    default: return false;
  }
}
