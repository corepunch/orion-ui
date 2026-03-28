#ifndef __UI_MSGBOX_H__
#define __UI_MSGBOX_H__

// commctl/msgbox.h — WinAPI-style message_box() modal dialog.
//
// Usage:
//   int result = message_box(parent_win, "Proceed?", "Confirm", MB_YESNOCANCEL);
//   if (result == IDYES) { ... }

#include "../user/user.h"

// Button-set flags (low nibble of `type`)
#define MB_OK           0x00
#define MB_OKCANCEL     0x01
#define MB_YESNO        0x02
#define MB_YESNOCANCEL  0x03

// Return values (match WinAPI IDOK / IDCANCEL / IDYES / IDNO)
#define IDOK     1
#define IDCANCEL 2
#define IDYES    6
#define IDNO     7

// Show a modal message-box dialog.
// parent – owner window (or NULL)
// text   – message body
// caption – title bar text
// type   – one of MB_OK, MB_OKCANCEL, MB_YESNO, MB_YESNOCANCEL
// Returns one of IDOK, IDCANCEL, IDYES, IDNO.
int message_box(window_t *parent, const char *text,
                const char *caption, uint32_t type);

#endif
