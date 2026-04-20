---
layout: default
title: Getting Started
nav_order: 2
---

# Getting Started

## Prerequisites

| Platform | Packages |
|---|---|
| Linux (Ubuntu/Debian) | `libsdl2-dev libgl1-mesa-dev libcglm-dev liblua5.4-dev` |
| macOS | `brew install sdl2 cglm lua` |
| Windows (MSYS2/MinGW) | `mingw-w64-x86_64-SDL2 mingw-w64-x86_64-cglm` |

## Building

```bash
# Clone the repository
git clone https://github.com/corepunch/orion-ui.git
cd orion-ui

# Build the static and shared libraries
make library

# Build all example programs
make examples

# Build loadable .gem plugins + orion-shell (POSIX only)
make gems shell

# Build everything
make all

# Build and run the test suite
make test
```

## Running an Example

```bash
./build/bin/helloworld
./build/bin/filemanager
./build/bin/imageeditor
```

To run examples as gems under the shell:

```bash
./build/bin/orion-shell build/gem/imageeditor.gem \
                         build/gem/filemanager.gem
```

## Running at 1× Scale

Orion defaults to 2× window scaling (`UI_WINDOW_SCALE=2`).  To run at native
1× resolution (useful on high-DPI monitors or for larger logical canvas sizes):

```bash
make examples CFLAGS="-DUI_WINDOW_SCALE=1"
./build/bin/imageeditor
```

## Async HTTP/HTTPS

Orion includes a message-driven HTTP/HTTPS client in the kernel layer.

- API reference and examples: [Async HTTP/HTTPS Client](http.md)
- Main README quick-start snippet: [README.md](../README.md)

## Building a Full Application

For applications with menus, multiple documents, and keyboard shortcuts,
use the MDI pattern. See [MDI Application Architecture](mdi.md) for the
complete guide, including `GEM_STANDALONE_MAIN`, accelerator tables, and
document management.

## Minimal Program

```c
#include "ui.h"

static result_t my_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
  if (msg == evPaint) {
    fill_rect(0xff202020, 0, 0, win->frame.w, win->frame.h);
    draw_text_small("Hello, Orion!", 10, 10, 0xffffffff);
    return true;
  }
  return false;
}

int main(void) {
  ui_init_graphics(0, "Hello", 640, 480);
  window_t *win = create_window("Hello", 0, MAKERECT(50,50,300,200),
                                NULL, my_proc, NULL);
  show_window(win, true);
  ui_event_t e;
  while (ui_is_running()) {
    while (get_message(&e)) dispatch_message(&e);
    repost_messages();
  }
  ui_shutdown_graphics();
  return 0;
}
```

## Linking

```makefile
CC     = gcc
CFLAGS = -Wall -std=c11 -I/path/to/orion-ui
LIBS   = -lSDL2 -lGL -lm -llua5.4

myapp: myapp.c build/lib/liborion.so
	$(CC) $(CFLAGS) -o $@ $< -Lbuild/lib -lorion -Wl,-rpath,build/lib $(LIBS)
```
