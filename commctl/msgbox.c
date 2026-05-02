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
#include "../user/text.h"
#include "../user/theme.h"

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

#define MB_WIN_W   240
#define MB_TEXT_Y    8
#define MB_TEXT_H   34   // room for two lines of text without touching buttons
#define MB_PAD       8
#define MB_BTN_GAP  10
#define MB_BTN_W    50
#define MB_BTN_H   BUTTON_HEIGHT
#define MB_WIN_H   (TITLEBAR_HEIGHT + MB_TEXT_Y + MB_TEXT_H + MB_BTN_GAP + MB_BTN_H + MB_PAD)

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
    case evCreate: {
      ms = (mb_state_t *)lparam;
      win->userdata = ms;

      // Buttons — positioned flush with the right edge, same style as the
      // imageeditor's about-dialog and file-picker.
      int btn_y = MB_TEXT_Y + MB_TEXT_H + MB_BTN_GAP;
      uint32_t btype = ms->type & 0x0F;

      if (btype == MB_YESNOCANCEL) {
        create_window("Yes", BUTTON_DEFAULT,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD) * 3, btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, 0, NULL);
        create_window("No", 0,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD) * 2, btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, 0, NULL);
        create_window("Cancel", 0,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD), btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, 0, NULL);
      } else if (btype == MB_YESNO) {
        create_window("Yes", BUTTON_DEFAULT,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD) * 2, btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, 0, NULL);
        create_window("No", 0,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD), btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, 0, NULL);
      } else if (btype == MB_OKCANCEL) {
        create_window("OK", BUTTON_DEFAULT,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD) * 2, btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, 0, NULL);
        create_window("Cancel", 0,
            MAKERECT(MB_WIN_W - (MB_BTN_W + MB_PAD), btn_y,
                     MB_BTN_W, MB_BTN_H),
            win, win_button, 0, NULL);
      } else {
        /* MB_OK */
        create_window("OK", BUTTON_DEFAULT,
            MAKERECT((MB_WIN_W - MB_BTN_W) / 2, btn_y, MB_BTN_W, MB_BTN_H),
            win, win_button, 0, NULL);
      }
      return true;
    }

    case evPaint:
      draw_text(FONT_SMALL, ms->text ? ms->text : "",
                MB_PAD, MB_TEXT_Y, get_sys_color(brTextNormal));
      return false;

    case evCommand: {
      if (HIWORD(wparam) != btnClicked) return false;
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
              MB_WIN_W, MB_WIN_H,
              parent, mb_proc, &ms);
  return ms.result;
}
