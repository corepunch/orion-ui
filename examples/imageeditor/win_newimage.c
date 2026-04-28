// "New Image" / "Canvas Size" dialog
// Reusable modal dialog that asks the user for a canvas width and height.
// Call show_size_dialog() with an initial width/height; returns true if accepted.
//
// Uses create_window_from_form() via show_dialog_from_form() so that all child
// controls are described declaratively in kSizeDialogForm.

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
// Declarative form definition
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
  .name        = "",
  .width       = NI_W,
  .height      = NI_H,
  .flags       = 0,
  .children    = kSizeDialogChildren,
  .child_count = 6,
};

// ──────────────────────────────────────────────────────────────────
// State
// ──────────────────────────────────────────────────────────────────

typedef struct {
  int  *out_w;
  int  *out_h;
  int   w, h;      // embedded copies for DDX (push/pull)
  bool  accepted;
} ni_state_t;

// DDX binding table: width and height edit boxes ↔ ni_state_t.w / .h
static const ctrl_binding_t k_ni_bindings[] = {
  { NI_ID_WIDTH,  BIND_INT_EDIT, offsetof(ni_state_t, w), 0 },
  { NI_ID_HEIGHT, BIND_INT_EDIT, offsetof(ni_state_t, h), 0 },
};

// Validate and accept the dialog, copying bounded values back to caller pointers.
static void ni_accept(window_t *win, ni_state_t *st) {
  dialog_pull(win, st, k_ni_bindings, ARRAY_LEN(k_ni_bindings));
  if (st->w > 0 && st->w <= MAX_IMAGE_DIMENSION) *st->out_w = st->w;
  if (st->h > 0 && st->h <= MAX_IMAGE_DIMENSION) *st->out_h = st->h;
  st->accepted = true;
  end_dialog(win, 1);
}

// ──────────────────────────────────────────────────────────────────
// Dialog window procedure
// ──────────────────────────────────────────────────────────────────

static result_t ni_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
  ni_state_t *st = (ni_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      // Children were already created by create_window_from_form() before this
      // message fired.  Store state and push the caller-supplied dimensions
      // into the edit boxes via DDX.
      st = (ni_state_t *)lparam;
      win->userdata = st;
      st->w = *st->out_w;
      st->h = *st->out_h;
      dialog_push(win, st, k_ni_bindings, ARRAY_LEN(k_ni_bindings));
      return true;
    }

    case evCommand: {
      uint16_t notif = HIWORD(wparam);

      // When the user presses Enter inside either edit box, win_textedit fires
      // edUpdate. Treat that as clicking OK so Enter accepts
      // the dialog while focus is in a text field.
      if (notif == edUpdate) {
        ni_accept(win, st);
        return true;
      }

      if (notif != btnClicked) return false;
      window_t *src = (window_t *)lparam;
      if (!src) return false;

      if (src->id == NI_ID_OK) {
        ni_accept(win, st);
        return true;
      }

      if (src->id == NI_ID_CANCEL) {
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

// Show a modal dialog that lets the user choose a canvas width and height.
// out_w / out_h are pre-filled with the default values on entry; on return they
// hold the user-entered values (only modified when the function returns true).
bool show_size_dialog(window_t *parent, const char *title, int *out_w, int *out_h) {
  ni_state_t st = {0};
  st.out_w = out_w;
  st.out_h = out_h;

  show_dialog_from_form(&kSizeDialogForm, title, parent, ni_proc, &st);
  return st.accepted;
}
