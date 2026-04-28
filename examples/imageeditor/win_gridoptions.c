// Grid Options dialog
// Lets the user configure the horizontal and vertical grid cell size.
// Call show_grid_options_dialog() with the current spacing; returns true if
// accepted.
//
// Uses show_dialog_from_form() so all children are defined declaratively.

#include "imageeditor.h"

// ──────────────────────────────────────────────────────────────────
// Dialog geometry (relative to dialog content area)
// ──────────────────────────────────────────────────────────────────

#define GO_W         180
#define GO_H          84

#define GO_LBL_X       4
#define GO_EDIT_X     74   // leave room for "Horizontal:" label
#define GO_EDIT_W     56
#define GO_EDIT_H     13
#define GO_ROW1_Y      8
#define GO_ROW2_Y     (GO_ROW1_Y + GO_EDIT_H + 4)

#define GO_BTN_Y      (GO_ROW2_Y + GO_EDIT_H + 6)
#define GO_BTN_H      BUTTON_HEIGHT
#define GO_BTN_W      40
#define GO_BTN_GAP     4
#define GO_OK_X       (GO_W - 2 * (GO_BTN_W + GO_BTN_GAP))
#define GO_CA_X       (GO_W - (GO_BTN_W + GO_BTN_GAP))

// Child control IDs
#define GO_ID_GRIDX    1
#define GO_ID_GRIDY    2
#define GO_ID_OK       3
#define GO_ID_CANCEL   4

// Accepted range for grid spacing (1..1024 canvas pixels)
#define GO_MIN_SPACING   1
#define GO_MAX_SPACING 1024

// ──────────────────────────────────────────────────────────────────
// Declarative form definition
// ──────────────────────────────────────────────────────────────────

static const form_ctrl_def_t kGoChildren[] = {
  { FORM_CTRL_TEXTEDIT, GO_ID_GRIDX, {GO_EDIT_X, GO_ROW1_Y, GO_EDIT_W, GO_EDIT_H}, 0,              "",       "gridx"  },
  { FORM_CTRL_TEXTEDIT, GO_ID_GRIDY, {GO_EDIT_X, GO_ROW2_Y, GO_EDIT_W, GO_EDIT_H}, 0,              "",       "gridy"  },
  { FORM_CTRL_BUTTON,   GO_ID_OK,    {GO_OK_X,   GO_BTN_Y,  GO_BTN_W,  GO_BTN_H},  BUTTON_DEFAULT, "OK",     "ok"     },
  { FORM_CTRL_BUTTON,   GO_ID_CANCEL,{GO_CA_X,   GO_BTN_Y,  GO_BTN_W,  GO_BTN_H},  0,              "Cancel", "cancel" },
};

static const form_def_t kGoForm = {
  .name        = "Grid Options",
  .width       = GO_W,
  .height      = GO_H,
  .flags       = 0,
  .children    = kGoChildren,
  .child_count = 4,
};

// ──────────────────────────────────────────────────────────────────
// State
// ──────────────────────────────────────────────────────────────────

typedef struct {
  int  *out_x;
  int  *out_y;
  int   x, y;       // embedded copies for DDX
  bool  accepted;
} go_state_t;

static const ctrl_binding_t k_go_bindings[] = {
  { GO_ID_GRIDX, BIND_INT_EDIT, offsetof(go_state_t, x), 0 },
  { GO_ID_GRIDY, BIND_INT_EDIT, offsetof(go_state_t, y), 0 },
};

static void go_accept(window_t *win, go_state_t *st) {
  dialog_pull(win, st, k_go_bindings, ARRAY_LEN(k_go_bindings));
  if (st->x >= GO_MIN_SPACING && st->x <= GO_MAX_SPACING) *st->out_x = st->x;
  if (st->y >= GO_MIN_SPACING && st->y <= GO_MAX_SPACING) *st->out_y = st->y;
  st->accepted = true;
  end_dialog(win, 1);
}

// ──────────────────────────────────────────────────────────────────
// Dialog window procedure
// ──────────────────────────────────────────────────────────────────

static result_t go_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
  go_state_t *st = (go_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      st = (go_state_t *)lparam;
      win->userdata = st;
      st->x = *st->out_x;
      st->y = *st->out_y;
      dialog_push(win, st, k_go_bindings, ARRAY_LEN(k_go_bindings));
      return true;
    }

    case evPaint: {
      draw_text_small("Horizontal:", GO_LBL_X, GO_ROW1_Y + 3, get_sys_color(brTextDisabled));
      draw_text_small("Vertical:",   GO_LBL_X, GO_ROW2_Y + 3, get_sys_color(brTextDisabled));
      return false;
    }

    case evCommand: {
      uint16_t notif = HIWORD(wparam);

      if (notif == edUpdate) {
        go_accept(win, st);
        return true;
      }

      if (notif != btnClicked) return false;
      window_t *src = (window_t *)lparam;
      if (!src) return false;

      if (src->id == GO_ID_OK) {
        go_accept(win, st);
        return true;
      }

      if (src->id == GO_ID_CANCEL) {
        end_dialog(win, 0);
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

bool show_grid_options_dialog(window_t *parent, int *out_x, int *out_y) {
  go_state_t st = {0};
  st.out_x = out_x;
  st.out_y = out_y;
  show_dialog_from_form(&kGoForm, "Grid Options", parent, go_proc, &st);
  return st.accepted;
}
