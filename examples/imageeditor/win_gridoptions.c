// Grid Options dialog
// Lets the user configure the horizontal and vertical grid cell size.
// Call show_grid_options_dialog() with the current spacing; returns true if
// accepted.
//
// Uses show_ddx_dialog() — no custom window proc is needed.

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
#define GO_ID_LBL_H    5
#define GO_ID_LBL_V    6

// Accepted range for grid spacing (1..1024 canvas pixels)
#define GO_MIN_SPACING   1
#define GO_MAX_SPACING 1024

// ──────────────────────────────────────────────────────────────────
// State
// ──────────────────────────────────────────────────────────────────

typedef struct { int x, y; } go_state_t;

// ──────────────────────────────────────────────────────────────────
// DDX binding table
// ──────────────────────────────────────────────────────────────────

static const ctrl_binding_t k_go_bindings[] = {
  { GO_ID_GRIDX, BIND_INT_EDIT, offsetof(go_state_t, x), 0 },
  { GO_ID_GRIDY, BIND_INT_EDIT, offsetof(go_state_t, y), 0 },
};

// ──────────────────────────────────────────────────────────────────
// Declarative form definition (includes DDX bindings + button IDs)
// ──────────────────────────────────────────────────────────────────

static const form_ctrl_def_t kGoChildren[] = {
  { FORM_CTRL_LABEL,    GO_ID_LBL_H, {GO_LBL_X,  GO_ROW1_Y + 1, GO_EDIT_X - GO_LBL_X - 4, GO_EDIT_H}, 0, "Horizontal:", "lbl_h" },
  { FORM_CTRL_LABEL,    GO_ID_LBL_V, {GO_LBL_X,  GO_ROW2_Y + 1, GO_EDIT_X - GO_LBL_X - 4, GO_EDIT_H}, 0, "Vertical:",   "lbl_v" },
  { FORM_CTRL_TEXTEDIT, GO_ID_GRIDX, {GO_EDIT_X, GO_ROW1_Y, GO_EDIT_W, GO_EDIT_H}, 0,              "",       "gridx"  },
  { FORM_CTRL_TEXTEDIT, GO_ID_GRIDY, {GO_EDIT_X, GO_ROW2_Y, GO_EDIT_W, GO_EDIT_H}, 0,              "",       "gridy"  },
  { FORM_CTRL_BUTTON,   GO_ID_OK,    {GO_OK_X,   GO_BTN_Y,  GO_BTN_W,  GO_BTN_H},  BUTTON_DEFAULT, "OK",     "ok"     },
  { FORM_CTRL_BUTTON,   GO_ID_CANCEL,{GO_CA_X,   GO_BTN_Y,  GO_BTN_W,  GO_BTN_H},  0,              "Cancel", "cancel" },
};

static const form_def_t kGoForm = {
  .name          = "Grid Options",
  .width         = GO_W,
  .height        = GO_H,
  .flags         = 0,
  .children      = kGoChildren,
  .child_count   = ARRAY_LEN(kGoChildren),
  .bindings      = k_go_bindings,
  .binding_count = ARRAY_LEN(k_go_bindings),
  .ok_id         = GO_ID_OK,
  .cancel_id     = GO_ID_CANCEL,
};

// ──────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────

bool show_grid_options_dialog(window_t *parent, int *out_x, int *out_y) {
  go_state_t st = { .x = *out_x, .y = *out_y };
  if (!show_ddx_dialog(&kGoForm, "Grid Options", parent, &st)) return false;
  if (st.x >= GO_MIN_SPACING && st.x <= GO_MAX_SPACING) *out_x = st.x;
  if (st.y >= GO_MIN_SPACING && st.y <= GO_MAX_SPACING) *out_y = st.y;
  return true;
}
