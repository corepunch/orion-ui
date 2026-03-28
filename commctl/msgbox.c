// commctl/msgbox.c — WinAPI-style message_box() modal dialog.
//
// Implements a minimal modal dialog with a text label and one of four
// standard button combinations (OK, OK/Cancel, Yes/No, Yes/No/Cancel),
// matching the WinAPI MessageBox() calling convention.

#include <string.h>

#include "msgbox.h"
#include "commctl.h"
#include "../user/user.h"
#include "../user/messages.h"

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

#define MB_WIN_W   240
#define MB_TEXT_H   30   // room for two lines of text
#define MB_PAD       8
#define MB_BTN_W    50
#define MB_BTN_H   BUTTON_HEIGHT
#define MB_WIN_H   (MB_PAD + MB_TEXT_H + MB_PAD + MB_BTN_H + MB_PAD)

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

typedef struct {
  const char *text;
  uint32_t    type;
  int         result;
} mb_state_t;

// ---------------------------------------------------------------------------
// Dialog procedure
// ---------------------------------------------------------------------------

static result_t mb_proc(window_t *win, uint32_t msg,
                         uint32_t wparam, void *lparam) {
  mb_state_t *ms = (mb_state_t *)win->userdata;

  switch (msg) {
    case kWindowMessageCreate: {
      ms = (mb_state_t *)lparam;
      win->userdata = ms;

      // Text label
      create_window(ms->text ? ms->text : "", WINDOW_NOTITLE,
          MAKERECT(MB_PAD, MB_PAD, MB_WIN_W - MB_PAD * 2, MB_TEXT_H),
          win, win_label, NULL);

      // Buttons — positioned flush with the right edge, same style as the
      // imageeditor's about-dialog and file-picker.
      int btn_y = MB_PAD + MB_TEXT_H + MB_PAD;
      uint32_t btype = ms->type & 0x0F;

      if (btype == MB_YESNOCANCEL) {
        create_window("Yes", 0,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD) * 3, btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, NULL);
        create_window("No", 0,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD) * 2, btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, NULL);
        create_window("Cancel", 0,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD), btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, NULL);
      } else if (btype == MB_YESNO) {
        create_window("Yes", 0,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD) * 2, btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, NULL);
        create_window("No", 0,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD), btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, NULL);
      } else if (btype == MB_OKCANCEL) {
        create_window("OK", 0,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD) * 2, btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, NULL);
        create_window("Cancel", 0,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD), btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, NULL);
      } else {
        /* MB_OK */
        create_window("OK", 0,
            MAKERECT((MB_WIN_W - MB_BTN_W) / 2, btn_y, MB_BTN_W, MB_BTN_H),
            win, win_button, NULL);
      }
      return true;
    }

    case kWindowMessageCommand: {
      if (HIWORD(wparam) != kButtonNotificationClicked) return false;
      window_t *btn = (window_t *)lparam;
      if (!btn) return true;

      int code = IDCANCEL;
      if      (strcmp(btn->title, "OK")     == 0) code = IDOK;
      else if (strcmp(btn->title, "Yes")    == 0) code = IDYES;
      else if (strcmp(btn->title, "No")     == 0) code = IDNO;
      else if (strcmp(btn->title, "Cancel") == 0) code = IDCANCEL;

      ms->result = code;
      end_dialog(win, (uint32_t)code);
      return true;
    }

    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int message_box(window_t *parent, const char *text,
                const char *caption, uint32_t type) {
  mb_state_t ms = {0};
  ms.text   = text;
  ms.type   = type;
  ms.result = IDCANCEL;

  const char *title = caption ? caption : "Message";
  show_dialog(title,
              MAKERECT(80, 60, MB_WIN_W, MB_WIN_H),
              parent, mb_proc, &ms);
  return ms.result;
}
