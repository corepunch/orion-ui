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
// Dialog geometry
// ──────────────────────────────────────────────────────────────────

#define NL_W       200
#define NL_H        68

#define NL_LBL_X        4
#define NL_COMBO_X     54
#define NL_LBL_COMBO_GAP 2   // gap between label right edge and combobox left edge
#define NL_COMBO_W  (NL_W - NL_COMBO_X - 8)
#define NL_ROW_Y     8
#define NL_CTRL_H   13

#define NL_BTN_Y    (NL_ROW_Y + NL_CTRL_H + 12)
#define NL_BTN_H    BUTTON_HEIGHT
#define NL_BTN_W    44
#define NL_BTN_GAP   4
#define NL_OK_X     (NL_W - 2 * (NL_BTN_W + NL_BTN_GAP))
#define NL_CA_X     (NL_W - (NL_BTN_W + NL_BTN_GAP))

// ──────────────────────────────────────────────────────────────────
// Child IDs
// ──────────────────────────────────────────────────────────────────

#define NL_ID_FILL    1
#define NL_ID_OK      2
#define NL_ID_CANCEL  3

// ──────────────────────────────────────────────────────────────────
// State
// ──────────────────────────────────────────────────────────────────

typedef struct {
  int  fill_index;  // 0=transparent, 1=white, 2=background, 3=foreground
  bool accepted;
} nl_state_t;

// ──────────────────────────────────────────────────────────────────
// Form layout
// ──────────────────────────────────────────────────────────────────

static const form_ctrl_def_t kNewLayerChildren[] = {
  { FORM_CTRL_LABEL,    -1,         {NL_LBL_X,   NL_ROW_Y, NL_COMBO_X - NL_LBL_X - NL_LBL_COMBO_GAP, NL_CTRL_H}, 0, "Fill with:", "lbl_fill"   },
  { FORM_CTRL_COMBOBOX, NL_ID_FILL, {NL_COMBO_X, NL_ROW_Y, NL_COMBO_W, NL_CTRL_H}, 0,              "",             "combo_fill" },
  { FORM_CTRL_BUTTON,   NL_ID_OK,   {NL_OK_X, NL_BTN_Y, NL_BTN_W, NL_BTN_H}, BUTTON_DEFAULT, "OK",     "ok"     },
  { FORM_CTRL_BUTTON,   NL_ID_CANCEL, {NL_CA_X, NL_BTN_Y, NL_BTN_W, NL_BTN_H}, 0,              "Cancel", "cancel" },
};

static const ctrl_binding_t kNewLayerBindings[] = {
  { NL_ID_FILL, BIND_INT_COMBO, offsetof(nl_state_t, fill_index), 1 /* default: white */ },
};

static const form_def_t kNewLayerForm = {
  .name          = "",
  .width         = NL_W,
  .height        = NL_H,
  .flags         = 0,
  .children      = kNewLayerChildren,
  .child_count   = ARRAY_LEN(kNewLayerChildren),
  .bindings      = kNewLayerBindings,
  .binding_count = ARRAY_LEN(kNewLayerBindings),
  .ok_id         = NL_ID_OK,
  .cancel_id     = NL_ID_CANCEL,
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
      // Push initial selection (default = white, index 1).
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
  nl_state_t st = { .fill_index = 1, .accepted = false };

  show_dialog_from_form(&kNewLayerForm, "New Layer", parent,
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
