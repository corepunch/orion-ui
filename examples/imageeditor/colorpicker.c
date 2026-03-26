// Color Picker Dialog
// Opens as a modal dialog via right-click on the color palette.
// Provides RGB sliders, HSV sliders, and a user-defined color palette.

#include "imageeditor.h"

// ──────────────────────────────────────────────────────────────────
// Dialog geometry (all coords relative to the dialog content area)
// ──────────────────────────────────────────────────────────────────

#define CP_WIN_W     240
#define CP_WIN_H     122

// Color preview swatches (left column, x = 0..49)
#define CP_PREV_X      2
#define CP_NEW_LBL_Y   2
#define CP_NEW_Y      10   // new-colour swatch top
#define CP_NEW_H      18
#define CP_OLD_LBL_Y  31
#define CP_OLD_Y      39   // old-colour swatch top
#define CP_OLD_H      18
#define CP_PREV_W     44   // swatch width

// Slider column layout (x = 50..239)
#define CP_LBL_X      50   // 1-char label x
#define CP_TRK_X      58   // gradient track start x
#define CP_TRK_W     144   // gradient track pixel width
#define CP_TRK_H       7   // gradient track pixel height
#define CP_VAL_X     (CP_TRK_X + CP_TRK_W + 3)  // numeric value text x

// Slider row Y positions (top of each track)
#define CP_Y_R         4
#define CP_Y_G        15
#define CP_Y_B        26
#define CP_Y_H        41
#define CP_Y_S        52
#define CP_Y_V        63

// User-palette section
#define CP_PAL_LBL_Y  78
#define CP_PAL_Y      88
#define CP_PAL_SW     22   // swatch width
#define CP_PAL_SH     12   // swatch height

// Button row
#define CP_BTN_Y     104
#define CP_BTN_H      13
// Button X positions
#define CP_BTN_OK_X    2
#define CP_BTN_OK_W   40
#define CP_BTN_CA_X   44
#define CP_BTN_CA_W   48
#define CP_BTN_AD_X   94
#define CP_BTN_AD_W   62

// Button child IDs (matched against btn->id in kWindowMessageCommand)
#define CP_ID_OK      1
#define CP_ID_CANCEL  2
#define CP_ID_ADD     3

// ──────────────────────────────────────────────────────────────────
// HSV conversion helpers
// ──────────────────────────────────────────────────────────────────

static void rgb_to_hsv(rgba_t c, float *h, float *s, float *v) {
  float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
  float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  *v = mx;
  float delta = mx - mn;
  if (mx < 1e-6f || delta < 1e-6f) { *s = 0.0f; *h = 0.0f; return; }
  *s = delta / mx;
  float hh;
  if      (mx == r) hh =        (g - b) / delta;
  else if (mx == g) hh = 2.0f + (b - r) / delta;
  else              hh = 4.0f + (r - g) / delta;
  hh /= 6.0f;
  if (hh < 0.0f) hh += 1.0f;
  *h = hh;
}

static rgba_t hsv_to_rgb(float h, float s, float v, uint8_t a) {
  float r, g, b;
  if (s < 1e-6f) {
    r = g = b = v;
  } else {
    float hh = h * 6.0f;
    int   i  = (int)hh % 6;
    float f  = hh - (int)hh;
    float p  = v * (1.0f - s);
    float q  = v * (1.0f - s * f);
    float t  = v * (1.0f - s * (1.0f - f));
    switch (i) {
      case 0: r=v; g=t; b=p; break;
      case 1: r=q; g=v; b=p; break;
      case 2: r=p; g=v; b=t; break;
      case 3: r=p; g=q; b=v; break;
      case 4: r=t; g=p; b=v; break;
      default: r=v; g=p; b=q; break;
    }
  }
  return (rgba_t){(uint8_t)(r*255.0f),(uint8_t)(g*255.0f),(uint8_t)(b*255.0f),a};
}

// ──────────────────────────────────────────────────────────────────
// Dialog state
// ──────────────────────────────────────────────────────────────────

typedef struct {
  rgba_t orig;       // original colour ("Old" preview)
  rgba_t cur;        // colour being edited ("New" preview)
  float  h, s, v;   // HSV kept in sync with cur.rgb
  int    dragging;   // slider index being dragged (0-5), or -1
  int    hover_pal;  // palette swatch under cursor, or -1
  bool   accepted;
} cp_state_t;

static void sync_hsv(cp_state_t *st) { rgb_to_hsv(st->cur, &st->h, &st->s, &st->v); }
static void sync_rgb(cp_state_t *st) { st->cur = hsv_to_rgb(st->h, st->s, st->v, st->cur.a); }

// ──────────────────────────────────────────────────────────────────
// Slider helpers
// ──────────────────────────────────────────────────────────────────

static const int   kSliderY[6]     = {CP_Y_R, CP_Y_G, CP_Y_B, CP_Y_H, CP_Y_S, CP_Y_V};
static const char *kSliderLbl[6]   = {"R","G","B","H","S","V"};
static const int   kSliderMax[6]   = {255, 255, 255, 360, 100, 100};

static int slider_int_val(const cp_state_t *st, int idx) {
  switch (idx) {
    case 0: return st->cur.r;
    case 1: return st->cur.g;
    case 2: return st->cur.b;
    case 3: return (int)(st->h * 360.0f + 0.5f);
    case 4: return (int)(st->s * 100.0f + 0.5f);
    case 5: return (int)(st->v * 100.0f + 0.5f);
    default: return 0;
  }
}

// Colour for a gradient segment at normalised position t ∈ [0,1]
static uint32_t slider_grad_col(const cp_state_t *st, int idx, float t) {
  rgba_t c;
  switch (idx) {
    case 0: c = (rgba_t){(uint8_t)(t*255),0,0,0xFF};            break;
    case 1: c = (rgba_t){0,(uint8_t)(t*255),0,0xFF};            break;
    case 2: c = (rgba_t){0,0,(uint8_t)(t*255),0xFF};            break;
    case 3: c = hsv_to_rgb(t, 1.0f, 1.0f, 0xFF);               break;
    case 4: c = hsv_to_rgb(st->h, t, 1.0f, 0xFF);              break;
    case 5: c = hsv_to_rgb(st->h, st->s > 1e-6f ? st->s : 1.0f, t, 0xFF); break;
    default: c = (rgba_t){0,0,0,0xFF};                          break;
  }
  return rgba_to_col(c);
}

static void draw_slider(int idx, const cp_state_t *st) {
  int ty = kSliderY[idx];

  // 1-char label
  draw_text_small(kSliderLbl[idx], CP_LBL_X, ty, COLOR_TEXT_NORMAL);

  // Gradient track
  const int SEGS = (idx == 3) ? 36 : 16;
  for (int seg = 0; seg < SEGS; seg++) {
    float t0 = (float) seg      / SEGS;
    float t1 = (float)(seg + 1) / SEGS;
    int   x0 = CP_TRK_X + (int)(t0 * CP_TRK_W);
    int   x1 = CP_TRK_X + (int)(t1 * CP_TRK_W);
    fill_rect((int)slider_grad_col(st, idx, (t0+t1)*0.5f),
              x0, ty, x1 - x0, CP_TRK_H);
  }

  // Thumb — bright vertical bar at the current value position
  int val = slider_int_val(st, idx);
  int tx  = CP_TRK_X + val * CP_TRK_W / kSliderMax[idx];
  fill_rect((int)COLOR_FLARE, tx - 1, ty - 1, 3, CP_TRK_H + 2);

  // Numeric value
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", val);
  draw_text_small(buf, CP_VAL_X, ty, COLOR_TEXT_NORMAL);
}

// ──────────────────────────────────────────────────────────────────
// Hit-testing helpers
// ──────────────────────────────────────────────────────────────────

static int hit_slider(int lx, int ly) {
  for (int i = 0; i < 6; i++) {
    int ty = kSliderY[i];
    if (lx >= CP_TRK_X && lx < CP_TRK_X + CP_TRK_W &&
        ly >= ty - 2   && ly < ty + CP_TRK_H + 2)
      return i;
  }
  return -1;
}

static int hit_palette(int lx, int ly) {
  if (ly < CP_PAL_Y || ly >= CP_PAL_Y + CP_PAL_SH) return -1;
  int i = (lx - CP_PREV_X) / CP_PAL_SW;
  if (i < 0 || i >= NUM_USER_COLORS) return -1;
  return i;
}

static void drag_slider(cp_state_t *st, int idx, int lx) {
  int raw = lx - CP_TRK_X;
  if (raw < 0)        raw = 0;
  if (raw > CP_TRK_W) raw = CP_TRK_W;
  int val = raw * kSliderMax[idx] / CP_TRK_W;
  switch (idx) {
    case 0: st->cur.r = (uint8_t)val; sync_hsv(st); break;
    case 1: st->cur.g = (uint8_t)val; sync_hsv(st); break;
    case 2: st->cur.b = (uint8_t)val; sync_hsv(st); break;
    case 3: st->h = (float)val / 360.0f; sync_rgb(st); break;
    case 4: st->s = (float)val / 100.0f; sync_rgb(st); break;
    case 5: st->v = (float)val / 100.0f; sync_rgb(st); break;
  }
}

// ──────────────────────────────────────────────────────────────────
// Full dialog paint  (returns false so child buttons auto-paint)
// ──────────────────────────────────────────────────────────────────

static void paint_cp(const cp_state_t *st) {
  // "New" colour preview
  draw_text_small("New", CP_PREV_X + 2, CP_NEW_LBL_Y, COLOR_TEXT_DISABLED);
  fill_rect((int)COLOR_DARK_EDGE,      CP_PREV_X - 1, CP_NEW_Y - 1,
                                       CP_PREV_W + 2,  CP_NEW_H + 2);
  fill_rect((int)rgba_to_col(st->cur), CP_PREV_X,     CP_NEW_Y,
                                       CP_PREV_W,      CP_NEW_H);

  // "Old" colour preview
  draw_text_small("Old", CP_PREV_X + 2, CP_OLD_LBL_Y, COLOR_TEXT_DISABLED);
  fill_rect((int)COLOR_DARK_EDGE,       CP_PREV_X - 1, CP_OLD_Y - 1,
                                        CP_PREV_W + 2,  CP_OLD_H + 2);
  fill_rect((int)rgba_to_col(st->orig), CP_PREV_X,     CP_OLD_Y,
                                        CP_PREV_W,      CP_OLD_H);

  // Separator between RGB and HSV groups
  fill_rect((int)COLOR_DARK_EDGE,
            CP_LBL_X, (CP_Y_B + CP_Y_H) / 2 + 2,
            CP_TRK_W + (CP_TRK_X - CP_LBL_X), 1);

  // Six sliders
  for (int i = 0; i < 6; i++)
    draw_slider(i, st);

  // User palette
  draw_text_small("Palette:", CP_PREV_X, CP_PAL_LBL_Y, COLOR_TEXT_DISABLED);
  for (int i = 0; i < NUM_USER_COLORS; i++) {
    int px = CP_PREV_X + i * CP_PAL_SW;
    bool has = (g_app && i < g_app->num_user_colors);
    fill_rect((int)COLOR_DARK_EDGE, px - 1, CP_PAL_Y - 1, CP_PAL_SW + 1, CP_PAL_SH + 2);
    fill_rect(has ? (int)rgba_to_col(g_app->user_palette[i]) : (int)COLOR_PANEL_DARK_BG,
              px, CP_PAL_Y, CP_PAL_SW - 1, CP_PAL_SH);
    if (has && i == st->hover_pal)
      fill_rect((int)COLOR_FOCUSED, px, CP_PAL_Y, CP_PAL_SW - 1, 1);
  }
}

// ──────────────────────────────────────────────────────────────────
// Dialog window procedure
// ──────────────────────────────────────────────────────────────────

static result_t cp_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
  cp_state_t *st = (cp_state_t *)win->userdata;

  switch (msg) {
    case kWindowMessageCreate: {
      st = (cp_state_t *)lparam;
      win->userdata = st;
      sync_hsv(st);

      window_t *ok = create_window("OK", 0,
          MAKERECT(CP_BTN_OK_X, CP_BTN_Y, CP_BTN_OK_W, CP_BTN_H),
          win, win_button, NULL);
      ok->id = CP_ID_OK;

      window_t *ca = create_window("Cancel", 0,
          MAKERECT(CP_BTN_CA_X, CP_BTN_Y, CP_BTN_CA_W, CP_BTN_H),
          win, win_button, NULL);
      ca->id = CP_ID_CANCEL;

      window_t *ad = create_window("+ Palette", 0,
          MAKERECT(CP_BTN_AD_X, CP_BTN_Y, CP_BTN_AD_W, CP_BTN_H),
          win, win_button, NULL);
      ad->id = CP_ID_ADD;
      return true;
    }

    case kWindowMessagePaint:
      paint_cp(st);
      return false;  // returning false lets child buttons paint themselves

    case kWindowMessageCommand: {
      if (HIWORD(wparam) != kButtonNotificationClicked) return false;
      window_t *btn = (window_t *)lparam;
      if (!btn) return false;
      if (btn->id == CP_ID_OK) {
        st->accepted = true;
        end_dialog(win, 1);
        return true;
      }
      if (btn->id == CP_ID_CANCEL) {
        end_dialog(win, 0);
        return true;
      }
      if (btn->id == CP_ID_ADD && g_app) {
        if (g_app->num_user_colors < NUM_USER_COLORS) {
          g_app->user_palette[g_app->num_user_colors++] = st->cur;
          invalidate_window(win);
        }
        return true;
      }
      return false;
    }

    case kWindowMessageLeftButtonDown: {
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);

      // Slider track hit?
      int si = hit_slider(lx, ly);
      if (si >= 0) {
        st->dragging = si;
        set_capture(win);
        drag_slider(st, si, lx);
        invalidate_window(win);
        return true;
      }

      // User-palette swatch hit?
      int pi = hit_palette(lx, ly);
      if (pi >= 0 && g_app && pi < g_app->num_user_colors) {
        st->cur = g_app->user_palette[pi];
        sync_hsv(st);
        invalidate_window(win);
        return true;
      }
      return false;
    }

    case kWindowMessageMouseMove: {
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);

      if (st->dragging >= 0) {
        drag_slider(st, st->dragging, lx);
        invalidate_window(win);
        return true;
      }

      int pi = hit_palette(lx, ly);
      if (pi != st->hover_pal) {
        st->hover_pal = pi;
        invalidate_window(win);
      }
      return true;
    }

    case kWindowMessageLeftButtonUp:
      if (st->dragging >= 0) {
        drag_slider(st, st->dragging, (int16_t)LOWORD(wparam));
        st->dragging = -1;
        set_capture(NULL);
        invalidate_window(win);
        return true;
      }
      return false;

    case kWindowMessageMouseLeave:
      if (st->hover_pal >= 0) {
        st->hover_pal = -1;
        invalidate_window(win);
      }
      return false;

    default:
      return false;
  }
}

// ──────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────

bool show_color_picker(window_t *parent, rgba_t initial, rgba_t *out) {
  cp_state_t st = {0};
  st.orig      = initial;
  st.cur       = initial;
  st.dragging  = -1;
  st.hover_pal = -1;
  sync_hsv(&st);

  // Centre the dialog on-screen
  int sx = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sy = ui_get_system_metrics(kSystemMetricScreenHeight);
  int dx = (sx - CP_WIN_W) / 2;
  int dy = (sy - CP_WIN_H) / 2;

  uint32_t result = show_dialog("Edit Color",
      MAKERECT(dx, dy, CP_WIN_W, CP_WIN_H),
      parent, cp_proc, &st);

  if (result && st.accepted) {
    *out = st.cur;
    return true;
  }
  return false;
}
