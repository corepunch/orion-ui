// "New Image" / "Canvas Size" dialog
// Reusable modal dialog that asks the user for a canvas width and height.
// Call show_size_dialog() with an initial width/height; returns true if accepted.
//
// Uses show_ddx_dialog() — no custom window proc is needed.

#include "imageeditor.h"

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
  DDX_TEXT(NI_ID_WIDTH, ni_state_t, w),
  DDX_TEXT(NI_ID_HEIGHT, ni_state_t, h),
};

// ──────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────

// Show a modal dialog that lets the user choose a canvas width and height.
// out_w / out_h are pre-filled with the default values on entry; on return they
// hold the user-entered values (only modified when the function returns true).
bool show_size_dialog(window_t *parent, const char *title, int *out_w, int *out_h) {
  ni_state_t st = { .w = *out_w, .h = *out_h };
  form_def_t form = imageeditor_new_image_form;
  form.bindings = k_ni_bindings;
  form.binding_count = ARRAY_LEN(k_ni_bindings);
  form.ok_id = NI_ID_OK;
  form.cancel_id = NI_ID_CANCEL;
  if (!show_ddx_dialog(&form, title, parent, &st)) return false;
  if (st.w > 0 && st.w <= MAX_IMAGE_DIMENSION) *out_w = st.w;
  if (st.h > 0 && st.h <= MAX_IMAGE_DIMENSION) *out_h = st.h;
  return true;
}
