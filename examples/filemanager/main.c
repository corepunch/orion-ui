#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "../../ui.h"
#include "../../gem_magic.h"

#define USE_LUA

// ---------------------------------------------------------------------------
// filemanager_proc — thin wrapper around win_filelist.
// All directory listing, sorting, and navigation is handled inside win_filelist.
// This proc intercepts FLN_* notifications for filemanager-specific behaviour:
//   FLN_NAVDIR  → update the window status bar with the new path
//   FLN_FILEOPEN → launch a terminal for .lua scripts
// ---------------------------------------------------------------------------
static result_t filemanager_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      // lparam = NULL: win_filelist starts in the current working directory.
      return win_filelist(win, msg, wparam, NULL);

    case evCommand: {
      uint16_t code = HIWORD(wparam);

      if (code == FLN_NAVDIR && lparam) {
        // Update the status bar to show the new directory.
        send_message(win, evStatusBar, 0, lparam);
        return false;
      }

#ifdef USE_LUA
      if (code == FLN_FILEOPEN && lparam) {
        const fileitem_t *item = (const fileitem_t *)lparam;
        if (item->path && strstr(item->path, ".lua")) {
          show_window(
            create_window("Terminal", 0, MAKERECT(16, 16, 240, 120),
                          NULL, win_terminal, 0, item->path),
            true);
          return true;
        }
      }
#endif

      if (code == FLN_FILEOPEN && lparam) {
        const fileitem_t *item = (const fileitem_t *)lparam;
        // Ask the shell to open the file via ui_open_file().  Handles .gem
        // files (load the gem) and any other extension a loaded gem claims.
        // Falls back silently if no handler is registered (standalone mode).
        if (item->path && ui_open_file(item->path))
          return true;
      }

      // Forward anything else (e.g. future FLN_* codes) to win_filelist.
      return win_filelist(win, msg, wparam, lparam);
    }

    case evDestroy:
      ui_request_quit();
      return win_filelist(win, msg, wparam, lparam);

    default:
      return win_filelist(win, msg, wparam, lparam);
  }
}

// ---------------------------------------------------------------------------
// .gem entry points
// ---------------------------------------------------------------------------

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  const char *start_path = argc > 1 ? argv[1] : NULL;
  window_t *win = create_window(
    "File Manager",
    WINDOW_STATUSBAR,
    MAKERECT(20, 20, 320, 240),
    NULL,
    filemanager_proc,
    hinstance,
    (void *)start_path
  );
  if (!win) return false;
  show_window(win, true);
  return true;
}

GEM_DEFINE("File Manager", "1.0", gem_init, NULL, NULL)

// ---------------------------------------------------------------------------
// Standalone entry point
// ---------------------------------------------------------------------------

#ifndef BUILD_AS_GEM
int main(int argc, char *argv[]) {
  (void)argc; (void)argv;

  if (!ui_init_graphics(UI_INIT_DESKTOP | UI_INIT_TRAY, "File Manager", 480, 320)) {
    printf("Failed to initialize graphics!\n");
    return 1;
  }

  window_t *main_window = create_window(
    "File Manager",
    WINDOW_STATUSBAR,
    MAKERECT(20, 20, 320, 240),
    NULL,
    filemanager_proc,
    0,
    NULL
  );

  if (!main_window) {
    printf("Failed to create window!\n");
    ui_shutdown_graphics();
    return 1;
  }

  show_window(main_window, true);

  ui_event_t e;
  while (ui_is_running()) {
    while (get_message(&e)) dispatch_message(&e);
    repost_messages();
  }

  destroy_window(main_window);
  ui_shutdown_graphics();
  return 0;
}
#endif /* BUILD_AS_GEM */
