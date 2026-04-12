// "New Image" / "Canvas Size" dialog
// Reusable modal dialog that asks the user for a canvas width and height.
// Call show_size_dialog() with an initial width/height; returns true if accepted.

#include "imageeditor.h"

// ──────────────────────────────────────────────────────────────────
// Dialog geometry (relative to dialog content area)
// ──────────────────────────────────────────────────────────────────

#define NI_W          180
#define NI_H           84

// Label + edit-box rows
#define NI_LBL_X        4
#define NI_EDIT_X      62   // leave room for "Height: " (8 chars × ~6px + margin)
#define NI_EDIT_W      56
#define NI_EDIT_H      13
#define NI_ROW1_Y       8
#define NI_ROW2_Y      (NI_ROW1_Y + NI_EDIT_H + 4)

// Buttons
#define NI_BTN_Y       (NI_ROW2_Y + NI_EDIT_H + 6)
#define NI_BTN_H       13
#define NI_BTN_W       40
#define NI_BTN_GAP      4

// Child IDs
#define NI_ID_WIDTH    1
#define NI_ID_HEIGHT   2
#define NI_ID_OK       3
#define NI_ID_CANCEL   4

// ──────────────────────────────────────────────────────────────────
// State
// ──────────────────────────────────────────────────────────────────

typedef struct {
  int  *out_w;
  int  *out_h;
  bool  accepted;
} ni_state_t;

// ──────────────────────────────────────────────────────────────────
// Dialog window procedure
// ──────────────────────────────────────────────────────────────────

static result_t ni_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
  ni_state_t *st = (ni_state_t *)win->userdata;

  switch (msg) {
    case kWindowMessageCreate: {
      st = (ni_state_t *)lparam;
      win->userdata = st;

      char buf[16];

      // Width field
      snprintf(buf, sizeof(buf), "%d", *st->out_w);
      window_t *ew = create_window(buf, 0,
          MAKERECT(NI_EDIT_X, NI_ROW1_Y, NI_EDIT_W, NI_EDIT_H),
          win, win_textedit, NULL);
      ew->id = NI_ID_WIDTH;

      // Height field
      snprintf(buf, sizeof(buf), "%d", *st->out_h);
      window_t *eh = create_window(buf, 0,
          MAKERECT(NI_EDIT_X, NI_ROW2_Y, NI_EDIT_W, NI_EDIT_H),
          win, win_textedit, NULL);
      eh->id = NI_ID_HEIGHT;

      // OK button (default, triggered by Enter)
      int ok_x = NI_W - 2 * (NI_BTN_W + NI_BTN_GAP);
      window_t *ok = create_window("OK", BUTTON_DEFAULT,
          MAKERECT(ok_x, NI_BTN_Y, NI_BTN_W, NI_BTN_H),
          win, win_button, NULL);
      ok->id = NI_ID_OK;

      // Cancel button
      int ca_x = NI_W - (NI_BTN_W + NI_BTN_GAP);
      window_t *ca = create_window("Cancel", 0,
          MAKERECT(ca_x, NI_BTN_Y, NI_BTN_W, NI_BTN_H),
          win, win_button, NULL);
      ca->id = NI_ID_CANCEL;

      return true;
    }

    case kWindowMessagePaint: {
      // Draw field labels manually (same pattern as win_textdialog.c)
      draw_text_small("Width:",  NI_LBL_X, NI_ROW1_Y + 3, COLOR_TEXT_DISABLED);
      draw_text_small("Height:", NI_LBL_X, NI_ROW2_Y + 3, COLOR_TEXT_DISABLED);
      return false;  // let child controls paint themselves
    }

    case kWindowMessageCommand: {
      if (HIWORD(wparam) != kButtonNotificationClicked) return false;
      window_t *src = (window_t *)lparam;
      if (!src) return false;

      if (src->id == NI_ID_OK) {
        window_t *ew = get_window_item(win, NI_ID_WIDTH);
        window_t *eh = get_window_item(win, NI_ID_HEIGHT);
        if (ew) { int v = atoi(ew->title); if (v > 0 && v <= 16384) *st->out_w = v; }
        if (eh) { int v = atoi(eh->title); if (v > 0 && v <= 16384) *st->out_h = v; }
        st->accepted = true;
        end_dialog(win, 1);
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

  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  int dx = (sw - NI_W) / 2;
  int dy = (sh - NI_H) / 2;

  show_dialog(title, MAKERECT(dx, dy, NI_W, NI_H), parent, ni_proc, &st);
  return st.accepted;
}
