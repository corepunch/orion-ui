---
layout: default
title: Getting Started
nav_order: 2
---

# Getting Started

## Prerequisites

| Platform | Packages |
|---|---|
| Linux (Ubuntu/Debian) | `libsdl2-dev libgl1-mesa-dev libcglm-dev libpng-dev liblua5.4-dev` |
| macOS | `brew install sdl2 cglm lua libpng` |
| Windows (MSYS2/MinGW) | `mingw-w64-x86_64-SDL2 mingw-w64-x86_64-cglm mingw-w64-x86_64-libpng` |

## Building

```bash
# Clone the repository
git clone https://github.com/corepunch/orion-ui.git
cd orion-ui

# Build the static and shared libraries
make library

# Build all example programs
make examples

# Build and run the test suite
make test
```

## Running an Example

```bash
./build/bin/helloworld
./build/bin/filemanager
./build/bin/imageeditor
```

## Running at 1× Scale

Orion defaults to 2× window scaling (`UI_WINDOW_SCALE=2`).  To run at native
1× resolution (useful on high-DPI monitors or for larger logical canvas sizes):

```bash
make examples CFLAGS="-DUI_WINDOW_SCALE=1"
./build/bin/imageeditor
```

## Minimal Program

```c
#include "ui.h"

extern bool running;

static result_t my_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
  if (msg == kWindowMessagePaint) {
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
  while (running) {
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

myapp: myapp.c build/lib/liborion.a
	$(CC) $(CFLAGS) -o $@ $< build/lib/liborion.a $(LIBS)
```
