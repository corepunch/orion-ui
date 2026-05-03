// "Add Mask" dialog
//
// Lets the user choose how the new mask should be initialized.
// Mirrors Photoshop-style options:
//   - Grayscale copy of the image
//   - White
//   - Background color
//   - Foreground color

#include "imageeditor.h"

// ──────────────────────────────────────────────────────────────────
// State
// ──────────────────────────────────────────────────────────────────

typedef struct {
  int  fill_mode;
  bool accepted;
} em_state_t;

static const ctrl_binding_t kAddMaskBindings[] = {
  DDX_COMBO(EM_ID_FILL, em_state_t, fill_mode, MASK_EXTRACT_GRAYSCALE),
};

static result_t add_mask_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  em_state_t *s = (em_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      win->userdata = lparam;
      s = (em_state_t *)lparam;
      window_t *cb = get_window_item(win, EM_ID_FILL);
      if (cb) {
        send_message(cb, cbAddString, 0, (void *)"Grayscale copy of the image");
        send_message(cb, cbAddString, 0, (void *)"White");
        send_message(cb, cbAddString, 0, (void *)"Background Color");
        send_message(cb, cbAddString, 0, (void *)"Foreground Color");
      }
      dialog_push(win, s, kAddMaskBindings, ARRAY_LEN(kAddMaskBindings));
      return true;
    }

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (!src) return false;
        if (src->id == EM_ID_OK) {
          dialog_pull(win, s, kAddMaskBindings, ARRAY_LEN(kAddMaskBindings));
          s->accepted = true;
          end_dialog(win, 1);
          return true;
        }
        if (src->id == EM_ID_CANCEL) {
          end_dialog(win, 0);
          return true;
        }
      }
      return false;

    default:
      return false;
  }
}

bool show_add_mask_dialog(window_t *parent, int *out_fill_mode) {
  em_state_t st = { .fill_mode = MASK_EXTRACT_GRAYSCALE, .accepted = false };
  show_dialog_from_form(&imageeditor_add_mask_form, "Add Mask", parent,
                        add_mask_proc, &st);
  if (!st.accepted) return false;
  if (out_fill_mode) *out_fill_mode = st.fill_mode;
  return true;
}
