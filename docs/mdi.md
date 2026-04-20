---
layout: default
title: MDI Application Architecture
nav_order: 5
---

# MDI Application Architecture

MDI (Multiple Document Interface) is the **recommended pattern for building
full Orion applications**.  Every serious Orion app follows it: imageeditor,
taskmanager, and formeditor all use this architecture.

In MDI style the desktop shows:

| Component | Role |
|---|---|
| Menu bar | Full-width `WINDOW_ALWAYSONTOP` strip at y=0 (created by `set_app_menu`) |
| Document windows | Regular top-level windows, one per open document |
| Floating palettes | `WINDOW_ALWAYSONTOP` tool/color/property panels (optional) |
| Status bar | Per-document `WINDOW_STATUSBAR` strip at the window bottom |

The application lifecycle (`gem_init` / `gem_shutdown`) and the accelerator-aware
event loop are identical in every MDI app — `gem_magic.h` ships a macro to
generate this boilerplate automatically.

---

## Quick-start skeleton

```c
// myapp.h — shared types and forward declarations
#ifndef __MYAPP_H__
#define __MYAPP_H__

#include "../../ui.h"

#define SCREEN_W 640
#define SCREEN_H 480

// Menu command IDs
#define ID_FILE_NEW   1
#define ID_FILE_OPEN  2
#define ID_FILE_SAVE  3
#define ID_FILE_QUIT  4
#define ID_HELP_ABOUT 100

// Application state — contains menubar_win, accel, and hinstance along with
// app-specific fields.  The GEM_STANDALONE_MAIN macro takes these as explicit
// expressions, so no particular field ordering is required.
typedef struct {
  window_t      *menubar_win;
  accel_table_t *accel;
  hinstance_t    hinstance;
  // ... per-app fields ...
} app_state_t;

extern app_state_t *g_app;

// View functions
result_t app_menubar_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam);
extern menu_def_t  kMenus[];
extern const int   kNumMenus;
void handle_menu_command(uint16_t id);

#endif // __MYAPP_H__
```

```c
// main.c — application entry points
#include "myapp.h"
#include "../../gem_magic.h"

app_state_t *g_app = NULL;

static const accel_t kAccelEntries[] = {
  { FCONTROL|FVIRTKEY, AX_KEY_N, ID_FILE_NEW  },
  { FCONTROL|FVIRTKEY, AX_KEY_O, ID_FILE_OPEN },
  { FCONTROL|FVIRTKEY, AX_KEY_S, ID_FILE_SAVE },
};

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  (void)argc; (void)argv;
  g_app = calloc(1, sizeof(app_state_t));
  if (!g_app) return false;

  g_app->hinstance = hinstance;

  // Create menu bar (standalone: real window; gem: contributes menus to shell)
  g_app->menubar_win = set_app_menu(app_menubar_proc, kMenus, kNumMenus,
                                    handle_menu_command, hinstance);

  // Load accelerators and attach to menu bar for shortcut hints
  g_app->accel = load_accelerators(kAccelEntries,
      (int)(sizeof(kAccelEntries)/sizeof(kAccelEntries[0])));
  if (g_app->menubar_win)
    send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators,
                 0, g_app->accel);

  // Open initial document
  handle_menu_command(ID_FILE_NEW);
  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;
  free_accelerators(g_app->accel);
  g_app->accel = NULL;
  free(g_app);
  g_app = NULL;
}

GEM_DEFINE("My App", "1.0", gem_init, gem_shutdown, NULL)

// Generates the standard standalone main() — no-op in gem mode.
GEM_STANDALONE_MAIN("My App", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
                    g_app->menubar_win, g_app->accel)
```

---

## `GEM_STANDALONE_MAIN` — the boilerplate eliminator

Every MDI standalone `main()` is identical:

```c
int main(int argc, char *argv[]) {
  if (!ui_init_graphics(flags, title, w, h)) return 1;
  if (!gem_init(argc, argv, 0)) { ui_shutdown_graphics(); return 1; }
  while (ui_is_running()) {
    ui_event_t e;
    while (get_message(&e)) {
      if (!translate_accelerator(menubar_win, &e, accel))
        dispatch_message(&e);
    }
    repost_messages();
  }
  gem_shutdown();
  ui_shutdown_graphics();
  return 0;
}
```

The macro in `gem_magic.h` generates this for you:

```c
GEM_STANDALONE_MAIN(title, flags, screen_w, screen_h,
                    menubar_win_expr, accel_expr)
```

| Parameter | Meaning |
|---|---|
| `title` | String literal passed to `ui_init_graphics` |
| `flags` | Init flags, e.g. `UI_INIT_DESKTOP` |
| `screen_w`, `screen_h` | Logical screen dimensions |
| `menubar_win_expr` | Expression yielding `window_t *` for the menu bar |
| `accel_expr` | Expression yielding `accel_table_t *` |

In `BUILD_AS_GEM` mode the macro expands to nothing — the shell owns the
event loop and no `main()` should be emitted by the gem.

Place the macro **after** `GEM_DEFINE` at file scope:

```c
GEM_DEFINE("My App", "1.0", gem_init, gem_shutdown, NULL)
GEM_STANDALONE_MAIN("My App", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
                    g_app->menubar_win, g_app->accel)
```

---

## Menu bar

`set_app_menu` (defined in `gem_magic.h`) creates the menu bar in standalone
mode and contributes menus to the shell in gem mode — the same call works in
both contexts.

```c
// Declare menus (typically in a view_menubar.c file):
static const menu_item_t kFileItems[] = {
  { "New",   ID_FILE_NEW  },
  { "Open",  ID_FILE_OPEN },
  { "Save",  ID_FILE_SAVE },
  { NULL, 0 },             // separator
  { "Quit",  ID_FILE_QUIT },
};
menu_def_t kMenus[] = {
  { "File", kFileItems, (int)(sizeof(kFileItems)/sizeof(kFileItems[0])) },
};
const int kNumMenus = (int)(sizeof(kMenus)/sizeof(kMenus[0]));
```

The menu bar window procedure receives `kMenuBarNotificationItemClick`
notifications via `evCommand` and routes them to `handle_menu_command`:

```c
result_t app_menubar_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam) {
  if (msg == evCommand &&
      HIWORD(wparam) == kMenuBarNotificationItemClick) {
    handle_menu_command(LOWORD(wparam));
    return true;
  }
  return win_menubar(win, msg, wparam, lparam);
}
```

---

## Accelerators

Declare a static `accel_t[]` array, load it once in `gem_init`, attach it to
the menu bar for shortcut hints, and pass it to `GEM_STANDALONE_MAIN`:

```c
static const accel_t kAccelEntries[] = {
  { FCONTROL|FVIRTKEY, AX_KEY_Z, ID_EDIT_UNDO },
  { FCONTROL|FVIRTKEY, AX_KEY_S, ID_FILE_SAVE },
  { FVIRTKEY,          AX_KEY_DEL, ID_EDIT_DELETE },
};

// In gem_init:
g_app->accel = load_accelerators(kAccelEntries,
    (int)(sizeof(kAccelEntries)/sizeof(kAccelEntries[0])));
if (g_app->menubar_win)
  send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators,
               0, g_app->accel);
```

Accelerator commands arrive in the window procedure as `evCommand` with
`HIWORD(wparam) == kAcceleratorNotification`.  They share the same command
IDs as menu items, so a single `handle_menu_command` dispatcher handles both.

---

## Document management

Each open document has its own top-level window. A linked list in `app_state_t`
tracks open documents; `active_doc` points to the last-activated one.

```c
typedef struct doc_s {
  // document data...
  char           filename[512];
  bool           modified;
  window_t      *win;         // the document window
  struct doc_s  *next;
} doc_t;

// Create a document and its window
doc_t *create_document(const char *path) {
  doc_t *doc = calloc(1, sizeof(doc_t));
  if (!doc) return NULL;
  if (path) strncpy(doc->filename, path, sizeof(doc->filename) - 1);

  doc->win = create_window(
      path ? path : "Untitled",
      WINDOW_STATUSBAR | WINDOW_TOOLBAR,
      MAKERECT(g_app->next_x, g_app->next_y, DOC_WIN_W, DOC_WIN_H),
      NULL, doc_win_proc, g_app->hinstance, doc);
  if (!doc->win) { free(doc); return NULL; }

  doc->next   = g_app->docs;
  g_app->docs = doc;
  g_app->active_doc = doc;
  show_window(doc->win, true);
  return doc;
}

// Refresh the title bar after rename or modification flag change
void doc_update_title(doc_t *doc) {
  const char *name = doc->filename[0] ? doc->filename : "Untitled";
  snprintf(doc->win->title, sizeof(doc->win->title),
           "%s%s", name, doc->modified ? " *" : "");
  invalidate_window(doc->win);
}
```

### Confirming close with unsaved changes

```c
bool doc_confirm_close(doc_t *doc, window_t *parent_win) {
  if (!doc->modified) { close_document(doc); return true; }
  // show a modal "Save changes?" dialog...
  uint32_t r = show_dialog("Unsaved Changes", 200, 80,
                            parent_win, confirm_proc, NULL);
  if (r == 1) { /* save */ }
  if (r != 0) { close_document(doc); return true; }
  return false;  // user pressed Cancel
}
```

---

## Floating palettes

Tool and property panels are `WINDOW_ALWAYSONTOP` top-level windows, typically
placed just below the menu bar:

```c
window_t *tp = create_window(
    "Tools",
    WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
    MAKERECT(4, MENUBAR_HEIGHT + 4, PALETTE_WIN_W, PALETTE_WIN_H),
    NULL, win_tool_palette_proc, hinstance, NULL);
show_window(tp, true);
g_app->tool_win = tp;
```

Use `win_toolbox` for a 2-column tool-selector grid (Photoshop-style palette).
See the [Toolbox control](controls.md) for the full API.

---

## Debug logging

Both imageeditor and taskmanager use `axSetLogFile` / `axLog` behind a
compile-time debug flag:

```c
#ifndef MYAPP_DEBUG
#define MYAPP_DEBUG 1
#endif

#if MYAPP_DEBUG
#define MY_DEBUG(...) axLog("[myapp] " __VA_ARGS__)
#else
#define MY_DEBUG(...) ((void)0)
#endif

// In gem_init:
#if MYAPP_DEBUG
{
  char log_path[1024];
  int n = snprintf(log_path, sizeof(log_path), "%s/myapp.log",
                   axSettingsDirectory());
  if (n > 0 && (size_t)n < sizeof(log_path))
    axSetLogFile(log_path);
}
#endif
```

Set `MYAPP_DEBUG=0` for release builds.  The debug macro calls are compiled
away entirely when the flag is off.

---

## Real-world examples

| Example | What to read |
|---|---|
| `examples/imageeditor/` | Full MDI with tool palette, color palette, PNG I/O, zoom, undo/redo |
| `examples/taskmanager/` | MDI with MVC layout, column view, file I/O |
| `examples/formeditor/`  | MDI with live form canvas, property inspector |
