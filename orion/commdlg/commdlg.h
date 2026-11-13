#ifndef __UI_COMMDLG_H__
#define __UI_COMMDLG_H__

#include <orion/user/user.h>

// ---------------------------------------------------------------------------
// filepicker.c
// ---------------------------------------------------------------------------

// commdlg/filepicker.h — WinAPI GetOpenFileName / GetSaveFileName analogue.
//
// Usage (open):
//
//   char path[512] = {0};
//   openfilename_t ofn = {0};
//   ofn.lStructSize  = sizeof(ofn);
//   ofn.hwndOwner    = my_win;
//   ofn.lpstrFile    = path;
//   ofn.nMaxFile     = sizeof(path);
//   ofn.lpstrFilter  = "PNG Files\0*.png\0All Files\0*.*\0";
//   ofn.nFilterIndex = 1;
//   ofn.Flags        = OFN_FILEMUSTEXIST;
//   if (get_open_filename(&ofn)) { ... use path ... }
//
// Usage (save):
//   Same struct, call get_save_filename(&ofn) instead.

// ---------------------------------------------------------------------------
// OFN Flags (subset of WinAPI OFN_* values)
// ---------------------------------------------------------------------------
#define OFN_PATHMUSTEXIST   (1u << 0)  // the directory part must exist
#define OFN_FILEMUSTEXIST   (1u << 1)  // the file must already exist (open)
#define OFN_OVERWRITEPROMPT (1u << 2)  // prompt before overwriting (save)
#define OFN_PICKFOLDER      (1u << 3)  // pick a directory instead of a file

// ---------------------------------------------------------------------------
// OPENFILENAME structure (WinAPI analogue)
// ---------------------------------------------------------------------------
typedef struct {
  uint32_t    lStructSize;   // set to sizeof(openfilename_t) before calling
  window_t   *hwndOwner;     // owner/parent window, or NULL
  char       *lpstrFile;     // in/out: file-path buffer (pre-fill for default)
  uint32_t    nMaxFile;      // size of lpstrFile in bytes (including NUL)
  const char *lpstrFilter;   // double-NUL-terminated filter pairs:
                             //   "Description\0*.ext\0Description2\0*.ext2\0"
                             //   Use "*.*" for all files.
  int         nFilterIndex;  // 1-based index of the initially selected filter
  uint32_t    Flags;         // OFN_* flags
} openfilename_t;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Show a modal "Open File" dialog.
// Sandboxed macOS processes use native panels to grant access outside the container.
// Writes the selected absolute path into ofn->lpstrFile on success.
// Returns true when the user confirms, false when cancelled.
bool get_open_filename(openfilename_t *ofn);

// Show a modal picker for one picture from the user's photos.
// iOS presents the Photos library and writes a temporary sRGB JPEG copy (owned
// by the platform, replaced on the next call); elsewhere it is the "Open File"
// dialog, starting where ofn->lpstrFile points, with the caller's filter.
// Returns true when a picture was chosen, false when cancelled.
bool get_open_image_filename(openfilename_t *ofn);

// Show a modal "Save File" dialog.
// Writes the selected/entered absolute path into ofn->lpstrFile on success.
// Returns true when the user confirms, false when cancelled.
bool get_save_filename(openfilename_t *ofn);

// Show a modal "Select Folder" dialog (set OFN_PICKFOLDER in Flags).
// Writes the chosen directory's absolute path into ofn->lpstrFile on success.
// Returns true when the user confirms, false when cancelled.
bool get_folder_name(openfilename_t *ofn);

// ---------------------------------------------------------------------------
// msgbox.c
// ---------------------------------------------------------------------------

// commdlg/msgbox.h — WinAPI-style message_box() modal dialog.
//
// Usage:
//   int result = message_box(parent_win, "Proceed?", "Confirm", MB_YESNOCANCEL);
//   if (result == IDYES) { ... }

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

// ---------------------------------------------------------------------------
// splash.c
// ---------------------------------------------------------------------------

// Splash screen dialog API.
// Displays an image in a borderless, always-on-top window that closes on click.
window_t *show_splash_screen(const char *image_path, hinstance_t hinstance);

#endif
