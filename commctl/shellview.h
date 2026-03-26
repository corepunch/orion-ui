#ifndef __UI_SHELLVIEW_H__
#define __UI_SHELLVIEW_H__

#include <stdbool.h>
#include "../user/user.h"

// ---------------------------------------------------------------------------
// File filter callback
// Called for every non-hidden directory entry (hidden entries, i.e. those
// whose name starts with '.', are always excluded before the filter runs).
// Return true to include the entry; false to exclude it.
// The ".." parent-directory entry is always shown regardless of the filter.
// ---------------------------------------------------------------------------
typedef bool (*shellview_filter_t)(const char *name, bool is_dir);

// ---------------------------------------------------------------------------
// Messages sent TO win_shellview
// ---------------------------------------------------------------------------
enum {
  // Navigate to an absolute directory path.
  // wparam: unused  lparam: const char* (path string)
  SVM_NAVIGATE = kWindowMessageUser + 200,

  // Copy the current directory path into a caller-supplied buffer.
  // wparam: buffer size (including NUL)  lparam: char* (destination buffer)
  SVM_GETPATH,

  // Set the entry filter.  Pass NULL to remove filtering (show all).
  // wparam: unused  lparam: shellview_filter_t function pointer
  SVM_SETFILTER,

  // Reload the current directory (re-reads the filesystem).
  // wparam: unused  lparam: unused
  SVM_REFRESH,

  // Copy the selected filename into a caller-supplied buffer.
  // Empty string if nothing is selected.
  // wparam: buffer size (including NUL)  lparam: char* (destination buffer)
  SVM_GETSELECTION,
};

// ---------------------------------------------------------------------------
// Notification codes sent FROM win_shellview to get_root_window(win)
// via kWindowMessageCommand.  HIWORD(wparam) = code, LOWORD(wparam) = index.
// ---------------------------------------------------------------------------
enum {
  // Selection changed (single click).
  // lparam: const char* filename of selected item, or NULL if a directory was
  //         clicked (directories do not populate the filename field).
  SVN_SELCHANGE = 300,

  // A non-directory file was activated (double-click or Enter).
  // Shellview has already navigated into directories before emitting this.
  // lparam: const char* filename of the activated file (base name only).
  SVN_ITEMACTIVATE,

  // The current directory changed (after navigating into a subdirectory or
  // pressing ".." to go up).
  // lparam: const char* new absolute path.
  SVN_PATHCHANGE,
};

// Window procedure
result_t win_shellview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif // __UI_SHELLVIEW_H__
