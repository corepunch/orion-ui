// Text Tool Dialog
// Shown when the user clicks the canvas with the Text tool.
// Provides: text input, font size slider, color selector, anti-alias toggle.
// Uses show_dialog_from_form() so all standard controls are declarative.

#include "imageeditor.h"

// ──────────────────────────────────────────────────────────────────
// Dialog geometry (relative to dialog content area)
// ──────────────────────────────────────────────────────────────────

#define TD_W          220
#define TD_H           96

// Row 1: text label + edit box
#define TD_LBL_X        2
#define TD_TEXT_LBL_Y   3
#define TD_TEXT_LBL_W   36
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
#define TD_COLOR_LBL_W 36
#define TD_SWATCH_W    14
#define TD_SWATCH_H    11
#define TD_SWATCH_X    (TD_LBL_X + TD_COLOR_LBL_W + 2)
#define TD_COLOR_BTN_X (TD_SWATCH_X + TD_SWATCH_W + 4)
#define TD_COLOR_BTN_W 54
#define TD_AA_X        (TD_COLOR_BTN_X + TD_COLOR_BTN_W + 6)
#define TD_AA_Y        TD_COLOR_Y

// Row 4: OK / Cancel
#define TD_BTN_H       BUTTON_HEIGHT
#define TD_BTN_Y       (TD_H - TD_BTN_H - 6)
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

// Declarative form controls (labels + standard controls).
static const form_ctrl_def_t kTextDialogChildren[] = {
  { FORM_CTRL_LABEL,    -1,           {TD_LBL_X,      TD_TEXT_LBL_Y, TD_TEXT_LBL_W, 13}, 0,              "Text:",      "lbl_text"  },
  { FORM_CTRL_TEXTEDIT, TD_ID_EDIT,   {TD_EDIT_X,     TD_EDIT_Y,     TD_EDIT_W,     TD_EDIT_H}, 0,       "",           "edit_text" },
  { FORM_CTRL_LABEL,    -1,           {TD_LBL_X,      TD_COLOR_Y + 2, TD_COLOR_LBL_W, 13}, 0,            "Color:",     "lbl_color" },
  { FORM_CTRL_BUTTON,   TD_ID_COLOR,  {TD_COLOR_BTN_X, TD_COLOR_Y - 1, TD_COLOR_BTN_W, TD_BTN_H}, 0,     "Change...",  "btn_color" },
  { FORM_CTRL_CHECKBOX, TD_ID_AA,     {TD_AA_X,       TD_AA_Y,       74,            12}, 0,               "Anti-alias", "chk_aa"    },
  { FORM_CTRL_BUTTON,   TD_ID_OK,     {TD_BTN_OK_X,   TD_BTN_Y,      TD_BTN_OK_W,   TD_BTN_H}, 0,        "OK",         "btn_ok"    },
  { FORM_CTRL_BUTTON,   TD_ID_CANCEL, {TD_BTN_CA_X,   TD_BTN_Y,      TD_BTN_CA_W,   TD_BTN_H}, 0,        "Cancel",     "btn_cancel" },
};

static const form_def_t kTextDialogForm = {
  .name = "Text Dialog",
  .width = TD_W,
  .height = TD_H,
  .flags = 0,
  .children = kTextDialogChildren,
  .child_count = sizeof(kTextDialogChildren) / sizeof(kTextDialogChildren[0]),
};

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

  // Size label
  char size_buf[32];
  snprintf(size_buf, sizeof(size_buf), "Size: %dpx", opts->font_size);
  draw_text_small(size_buf, TD_LBL_X, TD_SIZE_LBL_Y, get_sys_color(brTextDisabled));

  // Size slider track (16 segments)
  const int SEGS = 16;
  for (int seg = 0; seg < SEGS; seg++) {
    int x0 = TD_SLIDER_X + seg       * TD_SLIDER_W / SEGS;
    int x1 = TD_SLIDER_X + (seg + 1) * TD_SLIDER_W / SEGS;
    fill_rect(get_sys_color(brButtonBg), R(x0, TD_SLIDER_Y, x1 - x0, TD_SLIDER_H));
  }
  fill_rect(get_sys_color(brDarkEdge), R(TD_SLIDER_X, TD_SLIDER_Y, TD_SLIDER_W, 1));

  // Slider thumb
  fill_rect(get_sys_color(brFlare),
            R(size_to_slider_x(opts->font_size) - 1,
              TD_SLIDER_Y - 1, 3, TD_SLIDER_H + 2));

  // Color swatch
  fill_rect(get_sys_color(brDarkEdge), R(TD_SWATCH_X - 1, TD_COLOR_Y - 1,
            TD_SWATCH_W + 2, TD_SWATCH_H + 2));
  fill_rect(opts->color, R(TD_SWATCH_X, TD_COLOR_Y,
            TD_SWATCH_W, TD_SWATCH_H));

  (void)win;
}

// ──────────────────────────────────────────────────────────────────
// Dialog window procedure
// ──────────────────────────────────────────────────────────────────

static result_t td_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
  td_state_t *st = (td_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      st = (td_state_t *)lparam;
      win->userdata = st;

      set_window_item_text(win, TD_ID_EDIT, "%s", st->opts->text);
      window_t *aa = get_window_item(win, TD_ID_AA);
      if (aa) aa->value = st->opts->antialias;
      window_t *ed = get_window_item(win, TD_ID_EDIT);
      if (ed) set_focus(ed);

      return true;
    }

    case evPaint:
      paint_td(win, st);
      return false;  // let child controls paint themselves

    case evCommand: {
      if (HIWORD(wparam) != btnClicked) return false;
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

    case evLeftButtonDown: {
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

    case evMouseMove: {
      if (st->dragging_size) {
        int lx = (int16_t)LOWORD(wparam);
        st->opts->font_size = slider_x_to_size(lx);
        invalidate_window(win);
        return true;
      }
      return false;
    }

    case evLeftButtonUp: {
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

  show_dialog_from_form(&kTextDialogForm, "Insert Text", parent, td_proc, &st);

  return st.accepted && opts->text[0] != '\0';
}
