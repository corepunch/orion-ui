---
layout: default
title: Architecture
nav_order: 3
---

# Architecture

Orion mirrors the layered design of classic Windows:

| Application | Examples | your code |
|---|---|---|
| commctl | Common Controls | buttons, lists, menubar |
| user | Window Management | create/destroy, draw, text |
| kernel | Event Loop + SDL | SDL2, OpenGL, input |

## `user/` – Window Management (USER.DLL)

Responsible for window lifecycle, the message queue, drawing primitives, and
text rendering.

| File | Purpose |
|---|---|
| `user.h` | `window_t`, `irect16_t`, public API |
| `messages.h` | Message constants (`kWindowMessage*`), window flags, colours |
| `window.c` | `create_window`, `destroy_window`, `move_window`, `find_window` |
| `message.c` | `send_message`, `post_message`, `get_root_window` |
| `draw.h` / `draw_impl.c` | `fill_rect`, `draw_rect`, `draw_icon8/16`, viewports |
| `text.h` / `text.c` | `draw_text_small`, `strwidth`, bitmap font atlas |

## `kernel/` – Event Loop (KERNEL.DLL)

Wraps SDL2 and OpenGL.  The application's `main()` calls
`ui_init_graphics()` then loops with `get_message` / `dispatch_message` /
`repost_messages`.

| File | Purpose |
|---|---|
| `kernel.h` | `ui_init_graphics`, `get_message`, `dispatch_message`, `UI_WINDOW_SCALE` |
| `event.c` | SDL event → Orion message translation, hit-testing, z-order |
| `init.c` | SDL window + OpenGL context creation |
| `renderer.c` | Sprite/quad rendering via VAO/VBO, orthographic projection |
| `joystick.c` | Gamepad / joystick input |

## `commctl/` – Common Controls (COMCTL32.DLL)

Each control is a window procedure (`winproc_t`) that handles a standard
message set.

| Control | Proc | Header |
|---|---|---|
| Button | `win_button` | `commctl.h` |
| Checkbox | `win_checkbox` | `commctl.h` |
| Combobox | `win_combobox` | `commctl.h` |
| Text edit | `win_textedit` | `commctl.h` |
| Label | `win_label` | `commctl.h` |
| List | `win_list` | `commctl.h` |
| Column view | `win_reportview` | `columnview.h` |
| Menu bar | `win_menubar` | `menubar.h` |
| Console | `win_console` | `commctl.h` |
| Terminal | `win_terminal` | `commctl.h` |

## Z-Order and `WINDOW_ALWAYSONTOP`

Windows are stored in a linked list.  `find_window` returns the **last**
matching window (highest z-order).  `move_to_top` places a regular window
just before the first `WINDOW_ALWAYSONTOP` entry so palette / menu-bar
windows always stay on top regardless of user clicks.

## `UI_WINDOW_SCALE`

Defined in `kernel/kernel.h` with `#ifndef` guard so it can be overridden at
compile time:

```c
#ifndef UI_WINDOW_SCALE
#define UI_WINDOW_SCALE 2   // default: SDL window is 2x the logical size
#endif
```

`SCALE_POINT(x)` divides raw SDL mouse coordinates by `UI_WINDOW_SCALE` to
produce logical pixel coordinates used throughout the framework.
