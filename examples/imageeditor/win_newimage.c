// "New Image" / "Canvas Size" dialog
// Reusable modal dialog that asks the user for a canvas width and height.
// Call show_size_dialog() with an initial width/height; returns true if accepted.
//
// Uses show_ddx_dialog() — no custom window proc is needed.

#include "imageeditor.h"

// ──────────────────────────────────────────────────────────────────
// Dialog geometry (relative to dialog content area)
// ──────────────────────────────────────────────────────────────────

#define NI_W          180
#define NI_H           84

// Label + edit-box rows
#define NI_LBL_X        4
#define NI_EDIT_X      62   // leave room for "Height: " (8 chars x ~6px + margin)
#define NI_EDIT_W      56
#define NI_EDIT_H      13
#define NI_ROW1_Y       8
#define NI_ROW2_Y      (NI_ROW1_Y + NI_EDIT_H + 8)

// Buttons
#define NI_BTN_Y       (NI_ROW2_Y + NI_EDIT_H + 12)
#define NI_BTN_H       BUTTON_HEIGHT
#define NI_BTN_W       40
#define NI_BTN_GAP      4
#define NI_OK_X        (NI_W - 2 * (NI_BTN_W + NI_BTN_GAP))
#define NI_CA_X        (NI_W - (NI_BTN_W + NI_BTN_GAP))

// Child IDs
#define NI_ID_WIDTH    1
#define NI_ID_HEIGHT   2
#define NI_ID_OK       3
#define NI_ID_CANCEL   4

// Maximum dimension accepted by the dialog (must match canvas_resize limit).
#define MAX_IMAGE_DIMENSION 16384

// ──────────────────────────────────────────────────────────────────
// State
// ──────────────────────────────────────────────────────────────────

typedef struct { int w, h; } ni_state_t;

// ──────────────────────────────────────────────────────────────────
// DDX binding table
// ──────────────────────────────────────────────────────────────────

static const ctrl_binding_t k_ni_bindings[] = {
  { NI_ID_WIDTH,  BIND_INT_EDIT, offsetof(ni_state_t, w), 0 },
  { NI_ID_HEIGHT, BIND_INT_EDIT, offsetof(ni_state_t, h), 0 },
};

// ──────────────────────────────────────────────────────────────────
// Declarative form definition (includes DDX bindings + button IDs)
// ──────────────────────────────────────────────────────────────────

static const form_ctrl_def_t kSizeDialogChildren[] = {
  { FORM_CTRL_LABEL,    -1,           {NI_LBL_X,  NI_ROW1_Y, NI_EDIT_X - NI_LBL_X - 2, NI_EDIT_H}, 0,              "Width:",  "lbl_width"  },
  { FORM_CTRL_LABEL,    -1,           {NI_LBL_X,  NI_ROW2_Y, NI_EDIT_X - NI_LBL_X - 2, NI_EDIT_H}, 0,              "Height:", "lbl_height" },
  { FORM_CTRL_TEXTEDIT, NI_ID_WIDTH,  {NI_EDIT_X, NI_ROW1_Y, NI_EDIT_W, NI_EDIT_H}, 0,              "",       "width"  },
  { FORM_CTRL_TEXTEDIT, NI_ID_HEIGHT, {NI_EDIT_X, NI_ROW2_Y, NI_EDIT_W, NI_EDIT_H}, 0,              "",       "height" },
  { FORM_CTRL_BUTTON,   NI_ID_OK,     {NI_OK_X,   NI_BTN_Y,  NI_BTN_W,  NI_BTN_H},  BUTTON_DEFAULT, "OK",     "ok"     },
  { FORM_CTRL_BUTTON,   NI_ID_CANCEL, {NI_CA_X,   NI_BTN_Y,  NI_BTN_W,  NI_BTN_H},  0,              "Cancel", "cancel" },
};

static const form_def_t kSizeDialogForm = {
  .name          = "",
  .width         = NI_W,
  .height        = NI_H,
  .flags         = 0,
  .children      = kSizeDialogChildren,
  .child_count   = ARRAY_LEN(kSizeDialogChildren),
  .bindings      = k_ni_bindings,
  .binding_count = ARRAY_LEN(k_ni_bindings),
  .ok_id         = NI_ID_OK,
  .cancel_id     = NI_ID_CANCEL,
};

// ──────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────

// Show a modal dialog that lets the user choose a canvas width and height.
// out_w / out_h are pre-filled with the default values on entry; on return they
// hold the user-entered values (only modified when the function returns true).
bool show_size_dialog(window_t *parent, const char *title, int *out_w, int *out_h) {
  ni_state_t st = { .w = *out_w, .h = *out_h };
  if (!show_ddx_dialog(&kSizeDialogForm, title, parent, &st)) return false;
  if (st.w > 0 && st.w <= MAX_IMAGE_DIMENSION) *out_w = st.w;
  if (st.h > 0 && st.h <= MAX_IMAGE_DIMENSION) *out_h = st.h;
  return true;
}
