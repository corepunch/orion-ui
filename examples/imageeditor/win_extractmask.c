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
// Dialog geometry
// ──────────────────────────────────────────────────────────────────

#define EM_W       220
#define EM_H        68

#define EM_LBL_X        4
#define EM_COMBO_X     70
#define EM_COMBO_W    140
#define EM_ROW_Y       10
#define EM_CTRL_H      13

#define EM_BTN_Y    (EM_ROW_Y + EM_CTRL_H + 12)
#define EM_BTN_H    BUTTON_HEIGHT
#define EM_BTN_W    44
#define EM_BTN_GAP   4
#define EM_OK_X     (EM_W - 2 * (EM_BTN_W + EM_BTN_GAP))
#define EM_CA_X     (EM_W - (EM_BTN_W + EM_BTN_GAP))

// ──────────────────────────────────────────────────────────────────
// Child IDs
// ──────────────────────────────────────────────────────────────────

#define EM_ID_FILL    1
#define EM_ID_OK      2
#define EM_ID_CANCEL  3

// ──────────────────────────────────────────────────────────────────
// State
// ──────────────────────────────────────────────────────────────────

typedef struct {
  int  fill_mode;
  bool accepted;
} em_state_t;

// ──────────────────────────────────────────────────────────────────
// Form layout
// ──────────────────────────────────────────────────────────────────

static const form_ctrl_def_t kAddMaskChildren[] = {
  { FORM_CTRL_LABEL,    -1,           {EM_LBL_X, EM_ROW_Y, EM_COMBO_X - EM_LBL_X - 2, EM_CTRL_H}, 0, "Fill with:", "lbl_fill" },
  { FORM_CTRL_COMBOBOX,  EM_ID_FILL,   {EM_COMBO_X, EM_ROW_Y, EM_COMBO_W, EM_CTRL_H}, 0, "", "combo_fill" },
  { FORM_CTRL_BUTTON,    EM_ID_OK,     {EM_OK_X, EM_BTN_Y, EM_BTN_W, EM_BTN_H}, BUTTON_DEFAULT, "OK", "ok" },
  { FORM_CTRL_BUTTON,    EM_ID_CANCEL, {EM_CA_X, EM_BTN_Y, EM_BTN_W, EM_BTN_H}, 0, "Cancel", "cancel" },
};

static const ctrl_binding_t kAddMaskBindings[] = {
  { EM_ID_FILL, BIND_INT_COMBO, offsetof(em_state_t, fill_mode), MASK_EXTRACT_GRAYSCALE },
};

static const form_def_t kAddMaskForm = {
  .name          = "",
  .width         = EM_W,
  .height        = EM_H,
  .flags         = 0,
  .children      = kAddMaskChildren,
  .child_count   = ARRAY_LEN(kAddMaskChildren),
  .bindings      = kAddMaskBindings,
  .binding_count = ARRAY_LEN(kAddMaskBindings),
  .ok_id         = EM_ID_OK,
  .cancel_id     = EM_ID_CANCEL,
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
  show_dialog_from_form(&kAddMaskForm, "Add Mask", parent,
                        add_mask_proc, &st);
  if (!st.accepted) return false;
  if (out_fill_mode) *out_fill_mode = st.fill_mode;
  return true;
}
