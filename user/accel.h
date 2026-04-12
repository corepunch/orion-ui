#ifndef __UI_ACCEL_H__
#define __UI_ACCEL_H__

// Accelerator table API – modelled on the WinAPI LoadAccelerators /
// TranslateAccelerator pattern.
//
// Usage:
//   1. Declare a static array of accel_t entries, e.g.:
//        static const accel_t kAccel[] = {
//          { FCONTROL|FVIRTKEY, 'z', ID_EDIT_UNDO },
//          { FCONTROL|FVIRTKEY, 'y', ID_EDIT_REDO },
//          { FCONTROL|FVIRTKEY, 's', ID_FILE_SAVE },
//        };
//   2. Load the table once at startup:
//        accel_table_t *hAccel = load_accelerators(kAccel, 3);
//   3. In your event loop, call translate_accelerator before dispatch_message:
//        while (get_message(&e)) {
//          if (!translate_accelerator(win, &e, hAccel))
//            dispatch_message(&e);
//        }
//   4. Handle kWindowMessageCommand as usual (accelerators and menu items share
//      the same command-ID space; accelerator commands arrive with
//      HIWORD(wparam) == kAcceleratorNotification).
//   5. Free the table on shutdown:
//        free_accelerators(hAccel);

#include <stdbool.h>
#include <stdint.h>
#include "../kernel/kernel.h"

// Modifier flags for accel_t.fVirt (mirror WinAPI names).
// On Windows these are already defined by <winuser.h>; guard against
// redefinition to suppress compiler warnings.
#ifndef FVIRTKEY
#  define FVIRTKEY  0x01  // key field is a WI_KEY value (always set for Orion)
#endif
#ifndef FSHIFT
#  define FSHIFT    0x04  // Shift modifier must be held
#endif
#ifndef FCONTROL
#  define FCONTROL  0x08  // Ctrl modifier must be held
#endif
#ifndef FALT
#  define FALT      0x10  // Alt modifier must be held
#endif

// One entry in an accelerator table.  Mirrors the WinAPI ACCEL structure.
typedef struct {
  uint8_t  fVirt;  // FVIRTKEY combined with zero or more of FSHIFT/FCONTROL/FALT
  uint16_t key;    // axKEY value of the accelerator key (or lowercase ASCII char)
  uint16_t cmd;    // command ID sent as LOWORD(wparam) in kWindowMessageCommand
} accel_t;

// Opaque handle returned by load_accelerators().
typedef struct accel_table_s accel_table_t;

// Notification code placed in HIWORD(wparam) of kWindowMessageCommand when
// fired by translate_accelerator (analogous to WinAPI's value of 1).
#define kAcceleratorNotification 1

// Build an accelerator table from a static array of accel_t entries.
// Returns NULL on allocation failure.  The caller owns the returned table
// and must free it with free_accelerators().
accel_table_t *load_accelerators(const accel_t *entries, int count);

// Release memory for an accelerator table created by load_accelerators().
void free_accelerators(accel_table_t *table);

// Test whether evt matches any entry in table.
// On a match, sends kWindowMessageCommand with
//   MAKEDWORD(cmd, kAcceleratorNotification)
// to win and returns true.  Returns false for any other event.
// Call this in your event loop before dispatch_message().
bool translate_accelerator(window_t *win, ui_event_t *evt,
                           accel_table_t *table);

// Return a pointer to the first entry in table whose cmd field equals cmd,
// or NULL if no such entry exists.  The returned pointer is valid until
// free_accelerators() is called on table.
const accel_t *accel_find_cmd(const accel_table_t *table, uint16_t cmd);

// Format an accelerator entry as a human-readable shortcut string,
// e.g. "Ctrl+Z" or "Ctrl+Shift+S".
// Writes at most bufsize bytes including the NUL terminator.
// Returns the number of characters written (not counting NUL).
int accel_format(const accel_t *a, char *buf, int bufsize);

#endif // __UI_ACCEL_H__
