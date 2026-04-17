---
layout: default
title: Examples
nav_order: 9
---

# Examples

All examples live in `examples/` and can be built with `make examples`.
Each example also compiles as a loadable `.gem` (`make gems`) and can be
run under `orion-shell`. See [Gem Plugin System](gems.md) for details.

## Hello World (`helloworld.c`)

The minimal Orion program: one window, one label, one button.

```bash
./build/bin/helloworld
```

Key patterns shown:
- `ui_init_graphics` / `ui_shutdown_graphics`
- `create_window` + `show_window`
- `get_message` / `dispatch_message` / `repost_messages` loop
- `kWindowMessagePaint` → `fill_rect` + `draw_text_small`
- `kWindowMessageCommand` → button click handling

## File Manager (`filemanager.c`)

A two-pane file browser using `win_reportview`.

```bash
./build/bin/filemanager
```

Key patterns shown:
- `win_reportview` with `RVM_ADDITEM` / `RVM_CLEAR`
- `RVN_SELCHANGE` / `RVN_DBLCLK` notifications
- `kWindowMessageStatusBar` for path display
- Directory traversal with `opendir` / `readdir` / `stat`

## Image Editor (`imageeditor.c`)

A MacPaint-inspired MDI raster image editor with PNG open/save.

```bash
./build/bin/imageeditor
```

Demonstrates the full framework surface:

| Feature | API used |
|---|---|
| MDI document windows | `create_window` with `WINDOW_TOOLBAR \| WINDOW_STATUSBAR` |
| Floating tool palette | `WINDOW_ALWAYSONTOP` top-level window |
| Floating colour palette | `WINDOW_ALWAYSONTOP`, left/right click = FG/BG colour |
| Menu bar (File menu) | `win_menubar`, `kMenuBarMessageSetMenus`, chained proc |
| Canvas rendering | OpenGL texture (`glTexImage2D` / `glTexSubImage2D`) + `draw_rect` |
| Drawing tools | Pencil, brush, eraser, flood-fill (BFS) |
| File picker dialog | Modal dialog with `win_reportview` |
| PNG I/O | `load_image` / `save_image_png` (stb_image, built into the framework) |

### Running at larger size

By default Orion uses 2× window scaling.  Build without it for a larger
logical canvas:

```bash
make examples CFLAGS="-DUI_WINDOW_SCALE=1"
./build/bin/imageeditor
```

### Mouse coordinate note for canvas children

Child windows discovered via `kWindowMessageHitTest` receive absolute
logical coordinates in `wparam`.  Convert to child-local coords with:

```c
window_t *root = get_root_window(win);
int lx = (int16_t)LOWORD(wparam) - root->frame.x - win->frame.x;
int ly = (int16_t)HIWORD(wparam) - root->frame.y - win->frame.y;
```

## Writing Your Own Example

1. Create `examples/myapp.c` and `#include "../ui.h"`
2. Implement your window procedure(s)
3. In `main()`:
   ```c
   ui_init_graphics(0, "My App", 800, 600);
   window_t *w = create_window("My App", 0, MAKERECT(50,50,700,500),
                                NULL, my_proc, NULL);
   show_window(w, true);
   ui_event_t e;
   while (ui_is_running()) {
     while (get_message(&e)) dispatch_message(&e);
     repost_messages();
   }
   ui_shutdown_graphics();
   ```
4. Add a Makefile rule if your example needs extra libraries:
   ```makefile
   $(BIN_DIR)/myapp$(EXE_EXT): examples/myapp.c $(STATIC_LIB) | $(BIN_DIR)
       $(CC) $(CFLAGS) -o $@ $< $(STATIC_LIB) $(LDFLAGS) $(LDFLAGS_EXAMPLE) $(LIBS)
   ```
