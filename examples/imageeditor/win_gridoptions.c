// Grid Options dialog
// Lets the user configure the horizontal and vertical grid cell size.
// Call show_grid_options_dialog() with the current spacing; returns true if
// accepted.
//
// Uses show_ddx_dialog() — no custom window proc is needed.

#include "imageeditor.h"

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
  DDX_TEXT(GO_ID_GRIDX, go_state_t, x),
  DDX_TEXT(GO_ID_GRIDY, go_state_t, y),
};

// ──────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────

bool show_grid_options_dialog(window_t *parent, int *out_x, int *out_y) {
  go_state_t st = { .x = *out_x, .y = *out_y };
  form_def_t form = imageeditor_grid_options_form;
  form.bindings = k_go_bindings;
  form.binding_count = ARRAY_LEN(k_go_bindings);
  form.ok_id = GO_ID_OK;
  form.cancel_id = GO_ID_CANCEL;
  if (!show_ddx_dialog(&form, "Grid Options", parent, &st)) return false;
  if (st.x >= GO_MIN_SPACING && st.x <= GO_MAX_SPACING) *out_x = st.x;
  if (st.y >= GO_MIN_SPACING && st.y <= GO_MAX_SPACING) *out_y = st.y;
  return true;
}
