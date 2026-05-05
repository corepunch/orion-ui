// "New Layer" dialog
//
// Modal dialog that lets the user choose how to fill the new layer:
//   0  Transparent
//   1  White
//   2  Background Color
//   3  Foreground Color
//
// The combobox index is stored in nl_state_t.fill_index and mapped to an
// RGBA color by show_new_layer_dialog() before returning to the caller.

#include "imageeditor.h"

// ──────────────────────────────────────────────────────────────────
// State
// ──────────────────────────────────────────────────────────────────

typedef struct {
  int  fill_index;  // 0=transparent, 1=white, 2=background, 3=foreground
  bool accepted;
} nl_state_t;

static const ctrl_binding_t kNewLayerBindings[] = {
  DDX_COMBO(NL_ID_FILL, nl_state_t, fill_index, 0 /* default: transparent */),
};

// ──────────────────────────────────────────────────────────────────
// Window procedure
// ──────────────────────────────────────────────────────────────────

static result_t new_layer_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  nl_state_t *s = (nl_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      win->userdata = lparam;
      s = (nl_state_t *)lparam;

      // Populate fill options (order matches fill_index values).
      window_t *cb = get_window_item(win, NL_ID_FILL);
      if (cb) {
        send_message(cb, cbAddString, 0, (void *)"Transparent");
        send_message(cb, cbAddString, 0, (void *)"White");
        send_message(cb, cbAddString, 0, (void *)"Background Color");
        send_message(cb, cbAddString, 0, (void *)"Foreground Color");
      }
      // Push initial selection (default = transparent, index 0).
      dialog_push(win, s, kNewLayerBindings, ARRAY_LEN(kNewLayerBindings));
      return true;
    }

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (!src) return false;
        if (src->id == NL_ID_OK) {
          dialog_pull(win, s, kNewLayerBindings, ARRAY_LEN(kNewLayerBindings));
          s->accepted = true;
          end_dialog(win, 1);
          return true;
        }
        if (src->id == NL_ID_CANCEL) {
          end_dialog(win, 0);
          return true;
        }
      }
      return false;

    default:
      return false;
  }
}

// ──────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────

bool show_new_layer_dialog(window_t *parent, uint32_t *out_color) {
  nl_state_t st = { .fill_index = 0, .accepted = false };

  show_dialog_from_form(&imageeditor_new_layer_form, "New Layer", parent,
                        new_layer_proc, &st);
  if (!st.accepted) return false;

  // Map fill index → RGBA color.
  switch (st.fill_index) {
    case 0: *out_color = MAKE_COLOR(0x00, 0x00, 0x00, 0x00); break;  // transparent
    default:
    case 1: *out_color = MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF); break;  // white
    case 2: *out_color = g_app ? g_app->bg_color : MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF); break;
    case 3: *out_color = g_app ? g_app->fg_color : MAKE_COLOR(0x00, 0x00, 0x00, 0xFF); break;
  }
  return true;
}
