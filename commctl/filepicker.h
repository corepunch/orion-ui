#ifndef __UI_FILEPICKER_H__
#define __UI_FILEPICKER_H__

// commctl/filepicker.h — WinAPI GetOpenFileName / GetSaveFileName analogue.
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

#include "../user/user.h"

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
// Writes the selected absolute path into ofn->lpstrFile on success.
// Returns true when the user confirms, false when cancelled.
bool get_open_filename(openfilename_t *ofn);

// Show a modal "Save File" dialog.
// Writes the selected/entered absolute path into ofn->lpstrFile on success.
// Returns true when the user confirms, false when cancelled.
bool get_save_filename(openfilename_t *ofn);

// Show a modal "Select Folder" dialog (set OFN_PICKFOLDER in Flags).
// Writes the chosen directory's absolute path into ofn->lpstrFile on success.
// Returns true when the user confirms, false when cancelled.
bool get_folder_name(openfilename_t *ofn);

#endif
