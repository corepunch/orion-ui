#ifndef __UI_FILELIST_H__
#define __UI_FILELIST_H__

// commctl/filelist.h - A file-browser specific control
//
// win_filelist extends win_reportview: it provides directory listing with
// sorting, optional extension filtering, path navigation, and file metadata
// (size, modification time).  All directory management is internal; callers
// receive FLN_* notifications and drive the control through FLM_* messages.

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "../user/user.h"

// ---------------------------------------------------------------------------
// File-item descriptor.  path is heap-allocated (owned by win_filelist).
// Callers must never free it; the pointer is valid only for the duration of a
// synchronous notification callback.
// ---------------------------------------------------------------------------
typedef struct {
  char    *path;         // full absolute path
  int      icon;         // icon index passed to draw_icon8()
  bool     is_directory;
  bool     is_hidden;    // entry is a hidden file (name starts with '.')
  size_t   size;         // file size in bytes (0 for directories)
  time_t   modified;     // last-modification timestamp
} fileitem_t;

// ---------------------------------------------------------------------------
// Messages sent TO win_filelist
// ---------------------------------------------------------------------------
enum {
  // Navigate to a directory.
  // wparam: unused   lparam: const char* absolute path
  FLM_SETPATH = kWindowMessageUser + 200,

  // Copy the current directory path into a caller buffer.
  // wparam: buffer size (incl. NUL)   lparam: char* destination
  FLM_GETPATH,

  // Reload the current directory from disk.
  // wparam: unused   lparam: unused
  FLM_REFRESH,

  // Copy the full path of the selected item into a caller buffer.
  // Returns an empty string when nothing is selected or when the selected
  // item is a directory (use FLM_GETPATH to obtain the current directory).
  // wparam: buffer size (incl. NUL)   lparam: char* destination
  FLM_GETSELECTEDPATH,

  // Set a file-extension filter.  Only files whose names end with the given
  // extension (case-insensitive) are shown; directories are always shown.
  // Pass NULL or "" to remove the filter.
  // wparam: unused   lparam: const char* extension (e.g. ".png", "png")
  FLM_SETFILTER,
};

// ---------------------------------------------------------------------------
// Notification codes emitted by win_filelist via kWindowMessageCommand to
// get_root_window(win).  HIWORD(wparam) = code, LOWORD(wparam) = item index.
// ---------------------------------------------------------------------------
enum {
  // Selection changed (single click).
  // lparam: const fileitem_t* of the selected item (file or directory),
  //         or NULL when the selection is cleared.
  FLN_SELCHANGE = 300,

  // A non-directory file was activated (double-click / Enter).
  // Directory navigation is handled internally before this fires.
  // lparam: const fileitem_t* of the activated file.
  FLN_FILEOPEN,

  // The current directory changed after navigation.
  // lparam: const char* new absolute directory path.
  FLN_NAVDIR,
};

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
result_t win_filelist(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif // __UI_FILELIST_H__
