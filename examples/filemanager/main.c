#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "../../ui.h"

#define USE_LUA

extern bool running;

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
    case kWindowMessageCreate:
      // lparam = NULL: win_filelist starts in the current working directory.
      return win_filelist(win, msg, wparam, NULL);

    case kWindowMessageCommand: {
      uint16_t code = HIWORD(wparam);

      if (code == FLN_NAVDIR && lparam) {
        // Update the status bar to show the new directory.
        send_message(win, kWindowMessageStatusBar, 0, lparam);
        return false;
      }

#ifdef USE_LUA
      if (code == FLN_FILEOPEN && lparam) {
        const fileitem_t *item = (const fileitem_t *)lparam;
        if (item->path && strstr(item->path, ".lua")) {
          show_window(
            create_window("Terminal", 0, MAKERECT(16, 16, 240, 120),
                          NULL, win_terminal, item->path),
            true);
          return true;
        }
      }
#endif

      // Forward anything else (e.g. future FLN_* codes) to win_filelist.
      return win_filelist(win, msg, wparam, lparam);
    }

    case kWindowMessageDestroy:
      running = false;
      return win_filelist(win, msg, wparam, lparam);

    default:
      return win_filelist(win, msg, wparam, lparam);
  }
}

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
    NULL
  );

  if (!main_window) {
    printf("Failed to create window!\n");
    ui_shutdown_graphics();
    return 1;
  }

  show_window(main_window, true);

  ui_event_t e;
  while (running) {
    while (get_message(&e)) dispatch_message(&e);
    repost_messages(-1);
  }

  destroy_window(main_window);
  ui_shutdown_graphics();
  return 0;
}


