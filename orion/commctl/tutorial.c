// Tutorials: a speech bubble walks the user through a task, pointing at one control at a time.
//
// One tutorial runs at a time (like a tooltip). It owns three popups above its owner window: a scrim
// that dims the window except around the step's target (Tutorial::scrim), a ring around the target
// (Tutorial::spot) and a SpeechBubble with the step's text, counter, Skip and Next. All of them let
// the pointer through outside the bubble's body, so the user can click the very control
// the step explains. A repeating timer re-anchors them while the owner moves, scrolls or relayouts.
#include "commctl.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include <orion/user/toolbar.h>

#define TUTORIAL_TRACK_MS  100
#define TUTORIAL_EDGE      8      // nearest approach of the bubble to the owner's edges
#define TUTORIAL_SEEN_FILE "tutorials.txt"
#define TUTORIAL_SEEN_MAX  4096

enum { TU_ID_BUBBLE = 1, TU_ID_TITLE, TU_ID_TEXT, TU_ID_STEP, TU_ID_SKIP, TU_ID_NEXT };

static const form_ctrl_def_t kTutorialActions[] = {
  { .class_name = "Label",  .id = TU_ID_STEP, .name = "step", .text = "", .color = brInfoText, .color_set = true,
    .h_align = LAYOUT_ALIGN_START, .v_align = LAYOUT_ALIGN_CENTER },
  { .class_name = "Space",  .name = "flex", .h_align = LAYOUT_ALIGN_STRETCH },
  { .class_name = "Button", .id = TU_ID_SKIP, .name = "skip", .text = "Skip", .h_align = LAYOUT_ALIGN_START },
  { .class_name = "Button", .id = TU_ID_NEXT, .name = "next", .text = "Next", .flags = BUTTON_DEFAULT,
    .h_align = LAYOUT_ALIGN_START },
};

static const form_ctrl_def_t kTutorialBubble[] = {
  { .class_name = "Label", .id = TU_ID_TITLE, .name = "title", .text = "", .font = LABEL_HEADING, .font_set = true,
    .color = brInfoText, .color_set = true, .flags = LABEL_COLLAPSE_EMPTY, .v_align = LAYOUT_ALIGN_START },
  { .class_name = "Label", .id = TU_ID_TEXT, .name = "text", .text = "", .color = brInfoText, .color_set = true,
    .v_align = LAYOUT_ALIGN_START },
  { .class_name = "StackView", .name = "actions", .flags = WINDOW_STACK_HORIZONTAL, .layout_spacing = 6,
    .v_align = LAYOUT_ALIGN_START, .children = kTutorialActions, .child_count = ARRAY_LEN(kTutorialActions) },
};

static const form_ctrl_def_t kTutorialChildren[] = {
  { .class_name = "SpeechBubble", .id = TU_ID_BUBBLE, .name = "bubble", .layout_spacing = 8,
    .flags = WINDOW_NOACTIVATE, .children = kTutorialBubble, .child_count = ARRAY_LEN(kTutorialBubble) },
};

#define TUTORIAL_POPUP_FLAGS (WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | \
                              WINDOW_NOFILL | WINDOW_TRANSPARENT | WINDOW_NOACTIVATE | WINDOW_HIDDEN)

static const form_def_t kTutorialForm = {
  .name = "Tutorial", .flags = TUTORIAL_POPUP_FLAGS | WINDOW_AUTO_LAYOUT, .width = 300,
  .children = kTutorialChildren, .child_count = ARRAY_LEN(kTutorialChildren),
};

typedef struct {
  const tutorial_def_t *def;
  window_t *owner, *bubble, *spot, *scrim;
  int step;
  uint32_t timer;
  irect16_t target;              // screen rectangle the step points at
  irect16_t hole;                // the ring's screen rectangle, left undimmed by the scrim
  tooltip_tail_side_t side;
  bool placed;
} tutorial_state_t;

static tutorial_state_t g_tu = { .step = -1 };

static bool tutorial_alive(void) {
  return g_tu.def && is_window(g_tu.owner) && is_window(g_tu.bubble) && is_window(g_tu.spot) &&
         is_window(g_tu.scrim);
}

// ── Remembering finished tutorials ──────────────────────────────────────────
// One name per line in the settings directory, which every Orion application shares: orionc names
// declared tutorials <prefix>.<name>.

static size_t seen_load(char *buf, size_t cap) {
  size_t n = 0;
  if (!axSettingsLoad(TUTORIAL_SEEN_FILE, buf, cap - 1, &n)) n = 0;
  buf[n] = '\0';
  return n;
}

bool tutorial_seen(const char *name) {
  if (!name || !name[0]) return false;
  char buf[TUTORIAL_SEEN_MAX];
  seen_load(buf, sizeof(buf));
  size_t len = strlen(name);
  for (const char *line = buf; *line; ) {
    const char *end = strchr(line, '\n');
    size_t n = end ? (size_t)(end - line) : strlen(line);
    if (n == len && !memcmp(line, name, len)) return true;
    if (!end) break;
    line = end + 1;
  }
  return false;
}

bool tutorial_set_seen(const char *name, bool seen) {
  if (!name || !name[0] || strchr(name, '\n')) {
    fprintf(stderr, "[tutorial] set seen rejected name=%s\n", name ? name : "<null>");
    fflush(stderr);
    return false;
  }
  if (tutorial_seen(name) == seen) return true;
  char buf[TUTORIAL_SEEN_MAX], out[TUTORIAL_SEEN_MAX];
  size_t n = 0, len = strlen(name);
  seen_load(buf, sizeof(buf));
  for (char *line = buf; *line; ) {   // keep every other line
    char *end = strchr(line, '\n');
    size_t ln = end ? (size_t)(end - line) : strlen(line);
    if (ln && !(ln == len && !memcmp(line, name, len)) && n + ln + 1 < sizeof(out)) {
      memcpy(out + n, line, ln); n += ln; out[n++] = '\n';
    }
    if (!end) break;
    line = end + 1;
  }
  if (seen && n + len + 1 < sizeof(out)) { memcpy(out + n, name, len); n += len; out[n++] = '\n'; }
  if (!n) {   // axSettingsSave stores no empty file: the last name goes with the file
    char path[1024];
    const char *dir = axSettingsDirectory();
    int len_path = snprintf(path, sizeof(path), "%s/%s", dir ? dir : "", TUTORIAL_SEEN_FILE);
    if (len_path > 0 && (size_t)len_path < sizeof(path) && (!remove(path) || errno == ENOENT)) return true;
    fprintf(stderr, "[tutorial] could not remove file=%s name=%s\n", path, name);
    fflush(stderr);
    return false;
  }
  if (!axSettingsSave(TUTORIAL_SEEN_FILE, out, n)) {
    fprintf(stderr, "[tutorial] could not save file=%s name=%s seen=%d\n", TUTORIAL_SEEN_FILE, name, seen);
    fflush(stderr);
    return false;
  }
  return true;
}

// ── Anchoring ───────────────────────────────────────────────────────────────

static bool toolbar_has_item(window_t *win, uint32_t ident) {
  toolbar_state_t *tb = toolbar_get_state(win);
  for (int i = 0; tb && tb->items && i < tb->item_count; i++) if ((uint32_t)tb->items[i].ident == ident) return true;
  return false;
}

static irect16_t window_screen_frame(const window_t *win) {
  return R(window_screen_x(win), window_screen_y(win), win->frame.w, win->frame.h);
}

// The screen rectangle the step points at: a control (narrowed by its part) or a toolbar button.
// Without a visible target the whole owner is the target.
static irect16_t step_target(const tutorial_step_t *step) {
  window_t *owner = g_tu.owner;
  window_t *target = step->target ? get_window_item(owner, step->target) : owner;
  if (target && window_is_visible(target)) {
    tutorial_anchor_t anchor = { .part = step->part, .rect = get_client_rect(target) };
    if (step->part && send_message(target, evGetTutorialAnchor, (uint32_t)g_tu.step, &anchor)) {
      if (anchor.window && is_window(anchor.window) && window_is_visible(anchor.window))
        return window_screen_frame(anchor.window);
      if (!anchor.window && anchor.rect.w > 0 && anchor.rect.h > 0)
        return rect_offset(anchor.rect, window_screen_x(target), window_screen_y(target) + titlebar_height(target));
    }
    return window_screen_frame(target);
  }
  irect16_t item = {0};
  if (step->target && toolbar_has_item(owner, step->target) && send_message(owner, tbGetItemRect, step->target, &item))
    return rect_offset(item, window_screen_x(owner), window_screen_y(owner) + toolbar_content_offset(owner));
  return window_screen_frame(get_root_window(owner));
}

static void tutorial_set_ring(irect16_t ring) {
  move_window(g_tu.spot, ring.x, ring.y);
  resize_window(g_tu.spot, ring.w, ring.h);
  invalidate_window(g_tu.spot);
  g_tu.hole = ring;
  invalidate_window(g_tu.scrim);
}

// Below the target when the bubble fits under it, else above it, else over its top edge; centred
// on it horizontally inside the owner, the tail at the target's centre.
static void tutorial_place(bool force) {
  if (!tutorial_alive()) return;
  const tutorial_step_t *step = &g_tu.def->steps[g_tu.step];
  irect16_t t = step_target(step);
  if (!force && g_tu.placed && !memcmp(&t, &g_tu.target, sizeof(t))) return;
  g_tu.target = t;
  g_tu.placed = true;
  int outset = ui_px("Tutorial::spot", "--outset");
  irect16_t ring = rect_inset(t, -outset);
  irect16_t area = window_screen_frame(get_root_window(g_tu.owner));
  move_window(g_tu.scrim, area.x, area.y);
  resize_window(g_tu.scrim, area.w, area.h);
  tutorial_set_ring(ring);

  window_t *bubble = get_window_item(g_tu.bubble, TU_ID_BUBBLE);
  int width = MIN(g_tu.bubble->frame.w, area.w - 2 * TUTORIAL_EDGE);
  layout_measure_t m = { .avail_w = width };
  layout_measure_window(g_tu.bubble, &m);
  int height = m.desired_h;
  int below = ring.y + ring.h, above = ring.y - height;
  tooltip_tail_side_t side = TOOLTIP_TAIL_TOP;
  int y = below;
  if (below + height > area.y + area.h - TUTORIAL_EDGE) {
    if (above >= area.y + TUTORIAL_EDGE) { side = TOOLTIP_TAIL_BOTTOM; y = above; }
    else y = MAX(area.y + TUTORIAL_EDGE, MIN(ring.y + TUTORIAL_EDGE, area.y + area.h - TUTORIAL_EDGE - height));
  }
  if (side != g_tu.side && bubble) {
    g_tu.side = side;
    send_message(bubble, bbSetTailSide, side, NULL);
    m = (layout_measure_t){ .avail_w = width };
    layout_measure_window(g_tu.bubble, &m);
    height = m.desired_h;
    if (side == TOOLTIP_TAIL_BOTTOM) y = ring.y - height;
  }
  int cx = t.x + t.w / 2;
  int x = MAX(area.x + TUTORIAL_EDGE, MIN(cx - width / 2, area.x + area.w - TUTORIAL_EDGE - width));
  move_window(g_tu.bubble, x, y);
  resize_window(g_tu.bubble, width, height);
  window_layout_sync(g_tu.bubble);
  if (bubble) send_message(bubble, bbSetTailOffset, (uint32_t)MAX(1, cx - x - bubble->frame.x), NULL);
  invalidate_window(g_tu.bubble);
}

// ── Steps ───────────────────────────────────────────────────────────────────

static void tutorial_show_step(void) {
  const tutorial_def_t *def = g_tu.def;
  const tutorial_step_t *step = &def->steps[g_tu.step];
  bool last = g_tu.step == def->step_count - 1;
  char counter[64];
  snprintf(counter, sizeof(counter), ui_tr("Step %d of %d"), g_tu.step + 1, def->step_count);
  set_window_item_text(g_tu.bubble, TU_ID_TITLE, "%s", def->title ? ui_tr(def->title) : "");
  set_window_item_text(g_tu.bubble, TU_ID_TEXT, "%s", step->text ? ui_tr(step->text) : "");
  set_window_item_text(g_tu.bubble, TU_ID_STEP, "%s", def->step_count > 1 ? counter : "");
  set_window_item_text(g_tu.bubble, TU_ID_NEXT, "%s", ui_tr(last ? "Done" : "Next"));
  window_t *skip = get_window_item(g_tu.bubble, TU_ID_SKIP);
  if (skip) show_window(skip, !last);
  tutorial_place(true);
  show_window(g_tu.scrim, true);
  show_window(g_tu.spot, true);
  show_window(g_tu.bubble, true);
}

static void tutorial_close(void) {
  window_t *bubble = g_tu.bubble, *spot = g_tu.spot, *scrim = g_tu.scrim;
  if (g_tu.timer) axCancelTimer(g_tu.timer);
  g_tu = (tutorial_state_t){ .step = -1 };
  if (is_window(spot)) destroy_window(spot);
  if (is_window(scrim)) destroy_window(scrim);
  if (is_window(bubble)) destroy_window(bubble);
}

// The scrim only dims: clicks reach the window under it, so a step can ask for any of them.
static result_t tutorial_scrim_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam;
  switch (msg) {
    case evCreate: return true;
    case evPaint:
      fill_rect_cutout(ui_color("Tutorial::scrim", "background"), R(0, 0, win->frame.w, win->frame.h),
                       rect_offset(g_tu.hole, -win->frame.x, -win->frame.y), ui_radius("Tutorial::spot"));
      return true;
    case evHitTest:
      if (lparam) *(window_t **)lparam = NULL;
      return true;
    case evDestroy:
      if (g_tu.scrim == win) g_tu.scrim = NULL;
      return true;
  }
  return false;
}

static result_t tutorial_spot_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam;
  switch (msg) {
    case evCreate: return true;
    case evPaint:
      theme_draw_in(win, THEME_PART_TUTORIAL_SPOT, R(0, 0, win->frame.w, win->frame.h), CTRL_NORMAL);
      return true;
    case evHitTest:  // the ring never takes the pointer from what it outlines
      if (lparam) *(window_t **)lparam = NULL;
      return true;
    case evDestroy:
      if (g_tu.spot == win) g_tu.spot = NULL;
      return true;
  }
  return false;
}

static result_t tutorial_bubble_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: return true;
    case evPaint:  return false;
    case evHitTest:  // only the bubble's body takes the pointer; its shadow and margins let it through
      if (lparam) *(window_t **)lparam = NULL;
      return false;
    case evTimer:
      if (!tutorial_alive()) { tutorial_close(); return true; }
      tutorial_place(false);
      return true;
    case evCommand:
      if (HIWORD(wparam) != btnClicked) return false;
      if (LOWORD(wparam) == TU_ID_NEXT) { tutorial_next(); return true; }
      if (LOWORD(wparam) == TU_ID_SKIP) { tutorial_stop(); return true; }
      return false;
    case evDestroy:
      if (g_tu.bubble == win) g_tu.bubble = NULL;
      return true;
  }
  (void)lparam;
  return false;
}

// ── Public API ──────────────────────────────────────────────────────────────

bool tutorial_start(window_t *owner, const tutorial_def_t *def) {
  if (!owner || !is_window(owner) || !def || !def->steps || def->step_count <= 0) {
    fprintf(stderr, "[tutorial] start rejected owner=%u name=%s steps=%d\n", owner ? owner->id : 0,
            def && def->name ? def->name : "<null>", def ? def->step_count : 0);
    fflush(stderr);
    return false;
  }
  tutorial_close();
  window_t *root = get_root_window(owner);
  // Created bottom to top: the scrim, the ring, the bubble.
  window_t *scrim = create_window("", TUTORIAL_POPUP_FLAGS, MAKERECT(0, 0, 1, 1), NULL, tutorial_scrim_proc, root->hinstance, NULL);
  window_t *spot = create_window("", TUTORIAL_POPUP_FLAGS, MAKERECT(0, 0, 1, 1), NULL, tutorial_spot_proc, root->hinstance, NULL);
  window_t *bubble = create_window_from_form(&kTutorialForm, 0, 0, NULL, tutorial_bubble_proc, root->hinstance, NULL);
  int width = ui_px("Tutorial", "--bubble-width");
  if (bubble && width > 0) resize_window(bubble, width, bubble->frame.h);
  uint32_t timer = bubble ? axSetTimer(bubble, TUTORIAL_TRACK_MS, NULL, true) : 0;
  if (!scrim || !spot || !bubble || !timer) {
    fprintf(stderr, "[tutorial] start failed owner=%u name=%s spot=%d bubble=%d timer=%u\n", owner->id,
            def->name ? def->name : "", spot != NULL, bubble != NULL, timer);
    fflush(stderr);
    if (timer) axCancelTimer(timer);
    if (scrim) destroy_window(scrim);
    if (spot) destroy_window(spot);
    if (bubble) destroy_window(bubble);
    return false;
  }
  window_set_owner(scrim, root);
  window_set_owner(spot, root);
  window_set_owner(bubble, root);
  g_tu = (tutorial_state_t){ .def = def, .owner = owner, .bubble = bubble, .spot = spot, .scrim = scrim, .step = 0, .timer = timer,
                         .side = TOOLTIP_TAIL_BOTTOM };
  tutorial_show_step();
  return true;
}

bool tutorial_start_once(window_t *owner, const tutorial_def_t *def) {
  if (!def || tutorial_seen(def->name) || tutorial_is_running(def)) return false;
  return tutorial_start(owner, def);
}

void tutorial_stop(void) {
  if (g_tu.def) tutorial_set_seen(g_tu.def->name, true);
  tutorial_close();
}

bool tutorial_next(void) {
  if (!tutorial_alive()) { tutorial_close(); return false; }
  if (g_tu.step + 1 >= g_tu.def->step_count) { tutorial_stop(); return false; }
  g_tu.step++;
  tutorial_show_step();
  return true;
}

void tutorial_command(uint32_t command) {
  if (!command || !tutorial_alive() || g_tu.def->steps[g_tu.step].until != command) return;
  tutorial_next();
}

bool tutorial_is_running(const tutorial_def_t *def) {
  return tutorial_alive() && (!def || def == g_tu.def);
}

int tutorial_current_step(void) { return tutorial_alive() ? g_tu.step : -1; }

window_t *tutorial_bubble_window(void) { return tutorial_alive() ? g_tu.bubble : NULL; }
