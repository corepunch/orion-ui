#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "../../ui.h"

#define USE_LUA

extern bool running;

// ---------------------------------------------------------------------------
// filemanager_proc — thin wrapper around win_shellview.
// All directory listing and navigation is handled inside win_shellview.
// This proc intercepts SVN_* notifications for filemanager-specific behaviour
// (status bar path display, Lua script execution).
// ---------------------------------------------------------------------------
static result_t filemanager_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      // Start in cwd; lparam=NULL means win_shellview uses getcwd().
      return win_shellview(win, msg, wparam, NULL);

    case kWindowMessageCommand: {
      uint16_t code = HIWORD(wparam);

      if (code == SVN_PATHCHANGE && lparam) {
        // Update the window's status bar to show the new path.
        send_message(win, kWindowMessageStatusBar, 0, lparam);
        return false;
      }

#ifdef USE_LUA
      if (code == SVN_ITEMACTIVATE && lparam) {
        const char *name = (const char *)lparam;
        if (strstr(name, ".lua")) {
          // Build full path and launch a terminal to execute the script.
          char curpath[512], fullpath[768];
          send_message(win, SVM_GETPATH, sizeof(curpath), curpath);
          snprintf(fullpath, sizeof(fullpath), "%s/%s", curpath, name);
          show_window(
            create_window("Terminal", 0, MAKERECT(16, 16, 240, 120),
                          NULL, win_terminal, fullpath),
            true);
          return true;
        }
      }
#endif

      // Let win_shellview handle any remaining CVN_* it may emit internally.
      return win_shellview(win, msg, wparam, lparam);
    }

    case kWindowMessageDestroy:
      running = false;
      return win_shellview(win, msg, wparam, lparam);

    default:
      return win_shellview(win, msg, wparam, lparam);
  }
}

int main(int argc, char* argv[]) {
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
    while (get_message(&e)) {
      dispatch_message(&e);
    }
    repost_messages();
  }

  destroy_window(main_window);
  ui_shutdown_graphics();
  return 0;
}

