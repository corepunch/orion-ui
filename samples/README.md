# Orion UI — Samples

This directory contains sample applications that demonstrate and exercise the Orion UI framework.

Each sample:
- Lives in its own subdirectory (e.g. `samples/notepad/`)
- Contains a `main.c` as its entry point
- Includes `../../ui.h` to access the full framework
- Follows the same WinAPI-style patterns used throughout the framework

## Building

Samples are built alongside the framework. From the repository root:

```bash
make samples
```

Or build a single sample:

```bash
make samples/notepad/notepad
```

## Running

```bash
./build/samples/notepad/notepad
```

## Adding a new sample

1. Create a subdirectory under `samples/` (e.g. `samples/myapp/`)
2. Add `main.c` using the standard Orion init/shutdown sequence:

```c
#include "../../ui.h"

extern bool running;

result_t main_wnd_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      /* create child controls here */
      return true;
    case evPaint:
      /* draw here */
      return false;
    case evDestroy:
      running = false;
      return true;
    default:
      return false;
  }
}

int main(int argc, char *argv[]) {
  if (!ui_init_graphics(UI_INIT_DESKTOP, "My App", 640, 480))
    return 1;

  window_t *win = create_window("My App", 0, MAKERECT(40, 40, 560, 400), NULL, main_wnd_proc, NULL);
  show_window(win, true);

  ui_event_t e;
  while (running) {
    while (get_message(&e))
      dispatch_message(&e);
    repost_messages(-1);
  }

  destroy_window(win);
  ui_shutdown_graphics();
  return 0;
}
```

3. Add the sample to the `Makefile` under the `samples` target.

## Samples

| Directory | Description |
|-----------|-------------|
| *(none yet — add yours here)* | |
