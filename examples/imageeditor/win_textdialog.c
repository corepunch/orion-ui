// Text Tool Dialog
// Shown when the user clicks the canvas with the Text tool.
// Provides: text input, font size slider, color selector, anti-alias toggle.

#include "imageeditor.h"

// ──────────────────────────────────────────────────────────────────
// Dialog geometry (relative to dialog content area)
// ──────────────────────────────────────────────────────────────────

#define TD_W          220
#define TD_H           96

// Row 1: text label + edit box
#define TD_LBL_X        2
#define TD_EDIT_X       2
#define TD_EDIT_Y      12
#define TD_EDIT_W     (TD_W - 4)
#define TD_EDIT_H      13

// Row 2: font size slider
#define TD_SIZE_LBL_Y  30
#define TD_SLIDER_X     2
#define TD_SLIDER_Y    40
#define TD_SLIDER_W   (TD_W - 4)
#define TD_SLIDER_H     7
#define TD_SIZE_MIN     8
#define TD_SIZE_MAX    72

// Row 3: color + anti-alias
#define TD_COLOR_Y     53
#define TD_SWATCH_W    14
#define TD_SWATCH_H    11
#define TD_AA_X        (TD_SWATCH_W + 62 + 6)  // after [Change...] button
#define TD_AA_Y        TD_COLOR_Y

// Row 4: OK / Cancel
#define TD_BTN_Y       77
#define TD_BTN_H       13
#define TD_BTN_OK_X     2
#define TD_BTN_OK_W    38
#define TD_BTN_CA_X    44
#define TD_BTN_CA_W    50

// Child window IDs
#define TD_ID_EDIT      1
#define TD_ID_OK        2
#define TD_ID_CANCEL    3
#define TD_ID_COLOR     4
#define TD_ID_AA        5

// ──────────────────────────────────────────────────────────────────
// Dialog state
// ──────────────────────────────────────────────────────────────────

typedef struct {
  text_options_t *opts;    // caller-provided options (in/out)
  bool            accepted;
  bool            dragging_size; // true while dragging the size slider
} td_state_t;

// Convert a slider x-pixel to a font size
static int slider_x_to_size(int lx) {
  int raw = lx - TD_SLIDER_X;
  if (raw < 0)           raw = 0;
  if (raw > TD_SLIDER_W) raw = TD_SLIDER_W;
  int val = TD_SIZE_MIN + raw * (TD_SIZE_MAX - TD_SIZE_MIN) / TD_SLIDER_W;
  return val;
}

// Convert a font size to a slider thumb x-pixel
static int size_to_slider_x(int sz) {
  if (sz < TD_SIZE_MIN) sz = TD_SIZE_MIN;
  if (sz > TD_SIZE_MAX) sz = TD_SIZE_MAX;
  return TD_SLIDER_X + (sz - TD_SIZE_MIN) * TD_SLIDER_W / (TD_SIZE_MAX - TD_SIZE_MIN);
}

// ──────────────────────────────────────────────────────────────────
// Paint
// ──────────────────────────────────────────────────────────────────

static void paint_td(window_t *win, const td_state_t *st) {
  text_options_t *opts = st->opts;

  // "Text:" label
  draw_text_small("Text:", TD_LBL_X, 3, get_sys_color(kColorTextDisabled));

  // Size label
  char size_buf[32];
  snprintf(size_buf, sizeof(size_buf), "Size: %dpx", opts->font_size);
  draw_text_small(size_buf, TD_LBL_X, TD_SIZE_LBL_Y, get_sys_color(kColorTextDisabled));

  // Size slider track (16 segments)
  const int SEGS = 16;
  for (int seg = 0; seg < SEGS; seg++) {
    int x0 = TD_SLIDER_X + seg       * TD_SLIDER_W / SEGS;
    int x1 = TD_SLIDER_X + (seg + 1) * TD_SLIDER_W / SEGS;
    fill_rect(get_sys_color(kColorButtonBg), x0, TD_SLIDER_Y, x1 - x0, TD_SLIDER_H);
  }
  fill_rect(get_sys_color(kColorDarkEdge), TD_SLIDER_X, TD_SLIDER_Y, TD_SLIDER_W, 1);

  // Slider thumb
  int tx = size_to_slider_x(opts->font_size);
  fill_rect(get_sys_color(kColorFlare), tx - 1, TD_SLIDER_Y - 1, 3, TD_SLIDER_H + 2);

  // Color label + swatch
  draw_text_small("Color:", TD_LBL_X, TD_COLOR_Y + 2, get_sys_color(kColorTextDisabled));
  int sw_x = TD_LBL_X + 6 * 6 + 2; // after "Color:" text
  fill_rect(get_sys_color(kColorDarkEdge), sw_x - 1, TD_COLOR_Y - 1,
            TD_SWATCH_W + 2, TD_SWATCH_H + 2);
  fill_rect(opts->color, sw_x, TD_COLOR_Y,
            TD_SWATCH_W, TD_SWATCH_H);

  (void)win;
}

// ──────────────────────────────────────────────────────────────────
// Dialog window procedure
// ──────────────────────────────────────────────────────────────────

static result_t td_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
  td_state_t *st = (td_state_t *)win->userdata;

  switch (msg) {
    case kWindowMessageCreate: {
      st = (td_state_t *)lparam;
      win->userdata = st;

      // Edit box for text input
      window_t *ed = create_window(st->opts->text, 0,
          MAKERECT(TD_EDIT_X, TD_EDIT_Y, TD_EDIT_W, TD_EDIT_H),
          win, win_textedit, NULL);
      ed->id = TD_ID_EDIT;

      // "Change..." color button
      int change_x = TD_LBL_X + 6 * 6 + 2 + TD_SWATCH_W + 4;
      window_t *cb = create_window("Change...", 0,
          MAKERECT(change_x, TD_COLOR_Y - 1, 54, TD_BTN_H),
          win, win_button, NULL);
      cb->id = TD_ID_COLOR;

      // Anti-alias checkbox
      window_t *aa = create_window("Anti-alias", 0,
          MAKERECT(TD_AA_X, TD_AA_Y, 74, 12),
          win, win_checkbox, NULL);
      aa->id = TD_ID_AA;
      aa->value = st->opts->antialias;

      // OK button
      window_t *ok = create_window("OK", 0,
          MAKERECT(TD_BTN_OK_X, TD_BTN_Y, TD_BTN_OK_W, TD_BTN_H),
          win, win_button, NULL);
      ok->id = TD_ID_OK;

      // Cancel button
      window_t *ca = create_window("Cancel", 0,
          MAKERECT(TD_BTN_CA_X, TD_BTN_Y, TD_BTN_CA_W, TD_BTN_H),
          win, win_button, NULL);
      ca->id = TD_ID_CANCEL;

      return true;
    }

    case kWindowMessagePaint:
      paint_td(win, st);
      return false;  // let child controls paint themselves

    case kWindowMessageCommand: {
      if (HIWORD(wparam) != kButtonNotificationClicked) return false;
      window_t *src = (window_t *)lparam;
      if (!src) return false;

      if (src->id == TD_ID_OK) {
        // Copy text from the edit box
        window_t *ed = get_window_item(win, TD_ID_EDIT);
        if (ed) {
          strncpy(st->opts->text, ed->title, sizeof(st->opts->text) - 1);
          st->opts->text[sizeof(st->opts->text) - 1] = '\0';
        }
        // Read anti-alias state from checkbox
        window_t *aa = get_window_item(win, TD_ID_AA);
        if (aa) st->opts->antialias = aa->value;
        st->accepted = true;
        end_dialog(win, 1);
        return true;
      }

      if (src->id == TD_ID_CANCEL) {
        end_dialog(win, 0);
        return true;
      }

      if (src->id == TD_ID_COLOR) {
        // Open color picker to change text color
        uint32_t new_col = st->opts->color;
        if (show_color_picker(win, st->opts->color, &new_col)) {
          st->opts->color = new_col;
          invalidate_window(win);
        }
        return true;
      }

      if (src->id == TD_ID_AA) {
        // Checkbox toggles its own value — just re-read it in OK handler
        return false;
      }
      return false;
    }

    case kWindowMessageLeftButtonDown: {
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      // Check if click is on the size slider
      if (lx >= TD_SLIDER_X && lx <= TD_SLIDER_X + TD_SLIDER_W &&
          ly >= TD_SLIDER_Y - 2 && ly < TD_SLIDER_Y + TD_SLIDER_H + 2) {
        st->dragging_size = true;
        set_capture(win);
        st->opts->font_size = slider_x_to_size(lx);
        invalidate_window(win);
        return true;
      }
      return false;
    }

    case kWindowMessageMouseMove: {
      if (st->dragging_size) {
        int lx = (int16_t)LOWORD(wparam);
        st->opts->font_size = slider_x_to_size(lx);
        invalidate_window(win);
        return true;
      }
      return false;
    }

    case kWindowMessageLeftButtonUp: {
      if (st->dragging_size) {
        int lx = (int16_t)LOWORD(wparam);
        st->opts->font_size = slider_x_to_size(lx);
        st->dragging_size = false;
        set_capture(NULL);
        invalidate_window(win);
        return true;
      }
      return false;
    }

    default:
      return false;
  }
}

// ──────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────

bool show_text_dialog(window_t *parent, text_options_t *opts) {
  td_state_t st = {0};
  st.opts     = opts;
  st.accepted = false;

  int sx = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sy = ui_get_system_metrics(kSystemMetricScreenHeight);
  int dx = (sx - TD_W) / 2;
  int dy = (sy - TD_H) / 2;

  show_dialog("Insert Text",
              MAKERECT(dx, dy, TD_W, TD_H),
              parent, td_proc, &st);

  return st.accepted && opts->text[0] != '\0';
}
