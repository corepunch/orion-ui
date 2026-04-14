// Terminal emulator example
// Wraps the built-in win_terminal control as a standalone window or .gem.
// Pass an optional Lua script path as argv[1] to run it on startup;
// omit it for an interactive terminal session.

#include "../../ui.h"
#include "../../gem_magic.h"

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  const char *script = argc > 1 ? argv[1] : NULL;
  window_t *win = create_window(
    "Terminal",
    0,
    MAKERECT(20, 20, 500, 300),
    NULL,
    win_terminal,
    hinstance,
    (void *)script
  );
  if (!win) return false;
  show_window(win, true);
  return true;
}

GEM_DEFINE("Terminal", "1.0", gem_init, NULL, NULL)

#ifndef BUILD_AS_GEM
int main(int argc, char *argv[]) {
  const char *script = argc > 1 ? argv[1] : NULL;

  if (!ui_init_graphics(UI_INIT_DESKTOP, "Terminal", 520, 340)) {
    printf("Failed to initialize graphics!\n");
    return 1;
  }

  window_t *win = create_window(
    "Terminal",
    0,
    MAKERECT(20, 20, 500, 300),
    NULL,
    win_terminal,
    0,
    (void *)script
  );
  if (!win) {
    ui_shutdown_graphics();
    return 1;
  }

  show_window(win, true);

  ui_event_t e;
  while (ui_is_running()) {
    while (get_message(&e)) dispatch_message(&e);
    repost_messages();
  }

  ui_shutdown_graphics();
  return 0;
}
#endif /* BUILD_AS_GEM */
