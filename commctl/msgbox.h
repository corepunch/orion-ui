#ifndef __UI_MSGBOX_H__
#define __UI_MSGBOX_H__

// commctl/msgbox.h — WinAPI-style message_box() modal dialog.
//
// Usage:
//   int result = message_box(parent_win, "Proceed?", "Confirm", MB_YESNOCANCEL);
//   if (result == IDYES) { ... }

#include "../user/user.h"

// Button-set flags (low nibble of `type`).
// On Windows these names are already defined by <winuser.h> with the same
// semantics; guard against redefinition to suppress compiler warnings.
#ifndef MB_OK
#  define MB_OK           0x00
#endif
#ifndef MB_OKCANCEL
#  define MB_OKCANCEL     0x01
#endif
#ifndef MB_YESNO
#  define MB_YESNO        0x02
#endif
#ifndef MB_YESNOCANCEL
#  define MB_YESNOCANCEL  0x03
#endif

// Return values – mirror WinAPI IDOK / IDCANCEL / IDYES / IDNO exactly so
// callers can use either the Orion or the native definitions interchangeably.
#ifndef IDOK
#  define IDOK     1
#endif
#ifndef IDCANCEL
#  define IDCANCEL 2
#endif
#ifndef IDYES
#  define IDYES    6
#endif
#ifndef IDNO
#  define IDNO     7
#endif

// Show a modal message-box dialog.
// parent – owner window (or NULL)
// text   – message body
// caption – title bar text
// type   – one of MB_OK, MB_OKCANCEL, MB_YESNO, MB_YESNOCANCEL
// Returns one of IDOK, IDCANCEL, IDYES, IDNO.
int message_box(window_t *parent, const char *text,
                const char *caption, uint32_t type);

#endif
