---
layout: default
title: Gem Plugin System
nav_order: 9
---

# Gem Plugin System

Orion programs can be compiled in two ways:

| Mode | Output | Runs as |
|---|---|---|
| **Standalone** | executable (`build/bin/myapp`) | `./build/bin/myapp` |
| **Gem** | shared library (`build/gem/myapp.gem`) | loaded by `orion-shell` |

Both modes share the same source code. The difference is entirely in how the
binary is built and how the event loop is driven.

## How it works

All programs — executables, gems, and the shell itself — link against
`liborion.so` (the shared library). This means every gem loaded by
`orion-shell` shares the **same window manager instance**: same window list,
same message queue, same event infrastructure. The shell's single
`get_message` / `dispatch_message` loop therefore drives all gem windows
transparently.

When a `.gem` is loaded with `dlopen`, the shell:
1. Resolves `gem_get_interface()` from the gem
2. Calls `iface->init(argc, argv)` — the gem creates its windows and returns
3. The shell's event loop runs; all gem windows respond normally
4. When the gem's top-level window is closed, the shell detects it and
   calls `iface->shutdown()`, then `dlclose()`s the gem

## Writing a gem app

Include `gem_magic.h` and implement `gem_init` / `gem_shutdown`, then use the
`GEM_DEFINE` macro to export the interface. Guard the standalone `main()` with
`#ifndef BUILD_AS_GEM` so the same file builds both ways.

```c
// examples/myapp/main.c
#include "../../ui.h"
#include "../../gem_magic.h"

#define SCREEN_W 480
#define SCREEN_H 320

static window_t *g_win;

// ---- window procedure ------------------------------------------------
static result_t my_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
    if (msg == kWindowMessagePaint) {
        fill_rect(0xff202020, 0, 0, win->frame.w, win->frame.h);
        draw_text_small("Hello from a gem!", 10, 10, 0xffffffff);
        return true;
    }
    return false;
}

// ---- gem lifecycle ---------------------------------------------------
bool gem_init(int argc, char *argv[]) {
    g_win = create_window("My App", 0, MAKERECT(50, 50, SCREEN_W, SCREEN_H),
                          NULL, my_proc, NULL);
    show_window(g_win, true);
    return g_win != NULL;
}

void gem_shutdown(void) {
    // Windows are destroyed by the framework — nothing to free here.
}

// Register the gem's ABI entry point.
// Arguments: name, version, init fn, shutdown fn, file_types (or NULL)
GEM_DEFINE("My App", "1.0", gem_init, gem_shutdown, NULL)

// ---- standalone entry point (skipped in gem mode) -------------------
#ifndef BUILD_AS_GEM
int main(int argc, char *argv[]) {
    if (!ui_init_graphics(UI_INIT_DESKTOP, "My App", SCREEN_W, SCREEN_H))
        return 1;
    if (!gem_init(argc, argv)) { ui_shutdown_graphics(); return 1; }
    while (ui_is_running()) {
        ui_event_t e;
        while (get_message(&e)) dispatch_message(&e);
        repost_messages();
    }
    gem_shutdown();
    ui_shutdown_graphics();
    return 0;
}
#endif
```

Key points:
- `gem_init` creates windows and returns `true` on success
- `gem_shutdown` cleans up heap allocations (GL textures, app state, etc.)
- `GEM_DEFINE` emits the `gem_get_interface()` export — every `.gem` must have it
- In gem mode `ui_is_running()` always returns `false` (a macro in
  `gem_magic.h`), so the event-loop body is dead code and gets eliminated
- In gem mode `ui_request_quit()` is a no-op — a gem cannot shut down the shell

## Compiling

```bash
# Build everything (library, examples, gems, shell)
make all

# Build only the gem shared libraries
make gems

# Build only the standalone executables
make examples

# Build only the shell
make shell
```

The Makefile automatically:
- Compiles `.gem` files with `-DBUILD_AS_GEM -fPIC -shared` (macOS: `-dynamiclib`)
- Force-includes `gem_magic.h` at the top of every gem's unity build, so you
  don't need to add `#include "gem_magic.h"` manually to every source file
- Validates each `.gem` exports `gem_get_interface` via `nm`

## Running with the shell

```bash
# Load a gem at shell startup:
./build/bin/orion-shell build/gem/myapp.gem

# Load multiple gems:
./build/bin/orion-shell build/gem/imageeditor.gem \
                         build/gem/filemanager.gem
```

Gems listed on the command line are loaded in order after the shell creates
its desktop window and menu bar.

## Contributing a menu bar

When running under the shell, a gem can contribute top-level menus that
the shell merges into its shared menu bar. Populate three fields of the gem
interface **inside `gem_init`** before it returns:

```c
// myapp.c — menu definitions (same as standalone)
static const menu_item_t kFileItems[] = {
    { "Open",  ID_FILE_OPEN,  false },
    { "Save",  ID_FILE_SAVE,  false },
    { "-",     0,             false },  // separator
    { "Quit",  ID_FILE_QUIT,  false },
};
static const menu_def_t kMenus[] = {
    { "File", kFileItems, sizeof(kFileItems)/sizeof(kFileItems[0]) },
};
static const int kNumMenus = sizeof(kMenus)/sizeof(kMenus[0]);

static void handle_menu_command(uint16_t id) {
    switch (id) {
        case ID_FILE_OPEN: /* … */ break;
        case ID_FILE_QUIT:
#ifdef BUILD_AS_GEM
            // Destroy the gem's tracked window so the shell unloads it.
            destroy_window(g_win);
#else
            ui_request_quit();
#endif
            break;
    }
}

bool gem_init(int argc, char *argv[]) {
    // … create windows …

#ifdef BUILD_AS_GEM
    // Feed menus to the shell's shared menu bar.
    gem_interface_t *iface = gem_get_interface();
    iface->menus          = kMenus;
    iface->menu_count     = kNumMenus;
    iface->handle_command = handle_menu_command;
#else
    // Standalone: create a local menu bar window as usual.
    create_menubar_window(kMenus, kNumMenus, handle_menu_command);
#endif
    return true;
}
```

In standalone mode, create a `win_menubar` window as you normally would.
In gem mode, set the three `iface` fields — the shell builds a combined
menu bar from all loaded gems' contributions automatically.

## File type associations

A gem can declare the file extensions it handles so the shell (and the file
manager) can open matching files with it:

```c
static const char *my_types[] = { ".png", ".bmp", ".jpg", NULL };
GEM_DEFINE("Image Editor", "1.0", gem_init, gem_shutdown, my_types)
```

The shell uses these when `ui_open_file()` is called with a non-`.gem` path:
it finds the first loaded gem that claims the extension and calls its `init()`
again with `argv[1]` set to the file path.

Inside `gem_init`, read the file path like this:

```c
bool gem_init(int argc, char *argv[]) {
    // argv[0] is always the gem path.
    // argv[1] (when argc >= 2) is the file to open.
    if (argc >= 2 && argv[1])
        open_document(argv[1]);
    // …
    return true;
}
```

## Opening files programmatically

Any gem can open a file without knowing which gem (or program) handles it:

```c
// In filemanager or any other gem:
ui_open_file("/path/to/photo.png");
```

The shell registers its `shell_handle_open_file` callback at startup via
`ui_register_open_file_handler()`. The routing logic is:

| Path | Action |
|---|---|
| Ends in `.gem` | Load the gem directly |
| Other extension | Find the first loaded gem claiming that extension; call its `init()` with the file path |
| No handler registered | Returns `false` silently (standalone mode) |

## Shell exit sequence

The shell uses a 3-phase teardown to avoid crashes when gem window procs
are called after `dlclose()`:

1. **`shell_notify_gem_shutdown()`** — calls every gem's `shutdown()` while
   the GL context is still active (safe for `glDeleteTextures` etc.)
2. **`ui_shutdown_graphics()`** — destroys all windows; gem code is still in
   memory so `kWindowMessageDestroy` handlers remain valid
3. **`shell_cleanup_all_gems()`** — `dlclose()`s all gem handles; no window
   proc calls are made after this point
