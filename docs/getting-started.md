---
layout: default
title: Getting Started
nav_order: 2
---

# Getting Started

This guide takes you from a source checkout to a small Orion application that
runs both as a standalone executable and as a loadable GEM.

## Choose An Installation

For released SDK packages, install Orion with the
[package manager](package-manager):

```bash
curl -fsSL https://raw.githubusercontent.com/corepunch/orion-ui/main/install.sh | sh
export PATH="/opt/orion/bin:$PATH"
```

For framework development or to build the bundled applications, use a checkout:

```bash
git clone https://github.com/corepunch/orion-ui.git
cd orion-ui
git submodule update --init --recursive
make library
```

Orion requires a C toolchain and Git. Its Platform layer provides native
windowing, input, and rendering. Lua is optional and is needed only for
scripting-enabled features.

## Build And Explore

```bash
make apps                         # all standalone applications
make build/bin/helloworld        # one application
make gems                         # loadable GEM modules
make tools                        # orionc and utility programs
make test                         # complete test suite
```

Start with the minimal app, then inspect the complete application gallery:

```bash
build/bin/helloworld
```

- [Applications](examples) shows representative workflows and source patterns.
- [Controls](controls) maps reusable controls to real application screens.
- [Architecture](architecture) explains ownership and message flow.

## A Minimal Application

Orion applications expose `gem_init` and optionally `gem_shutdown`. The same
source becomes a standalone executable through `GEM_STANDALONE_MAIN` and a
loadable module through `GEM_DEFINE`.

```c
#include <orion/ui.h>
#include <orion/gem.h>

#define ID_HELLO 100

static window_t *g_main_win;

static result_t main_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      window_t *button = create_window(
        "Hello", WINDOW_NOTITLE, MAKERECT(16, 16, 72, 19),
        win, win_button, 0, NULL);
      button->id = ID_HELLO;
      return true;
    }
    case evCommand:
      if (LOWORD(wparam) == ID_HELLO && HIWORD(wparam) == btnClicked) {
        message_box(win, "Hello from Orion.", "My App", MB_OK);
        return true;
      }
      return false;
    case evPaint:
      draw_text_small("My first Orion app", 16, 48,
                      get_sys_color(brTextNormal));
      return true;
    case evDestroy:
#ifndef BUILD_AS_GEM
      ui_request_quit();
#endif
      return true;
    default:
      return false;
  }
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  (void)argc;
  (void)argv;
  register_commctl_classes();
  g_main_win = create_window(
    "My App", 0, MAKERECT(32, 32, 320, 200),
    NULL, main_proc, hinstance, NULL);
  if (!g_main_win) return false;
  show_window(g_main_win, true);
  return true;
}

void gem_shutdown(void) {
  g_main_win = NULL;
}

GEM_DEFINE("My App", "1.0", gem_init, gem_shutdown, NULL)
GEM_STANDALONE_MAIN("My App", UI_INIT_DESKTOP, 640, 480, NULL, NULL)
```

The important contracts are:

- Window behavior lives in a `winproc_t` and is driven by messages.
- `create_window` receives title, flags, frame, parent, class/proc, instance,
  and creation parameter.
- Controls notify the root window with `evCommand`; decode the control ID with
  `LOWORD` and the notification with `HIWORD`.
- Drawing happens only during `evPaint`. State changes call
  `invalidate_window()`.
- Standalone apps own the event loop; GEMs share the Shell event loop.

## Prefer Declarative Forms

The hand-built window above teaches the lifecycle. For dialogs and panels with
multiple standard controls, use a `.orion` resource instead of creating every
child in `evCreate`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<orion version="1" name="myapp" title="My App" root="apps/myapp">
  <forms>
    <form name="main" title="My App" width="320" height="180"
          spacing="8" padding="8">
      <Label name="heading" text="My first Orion app" />
      <StackView name="actions" orientation="horizontal" spacing="6">
        <Space name="fill" />
        <Button name="hello" value="100" text="Hello" />
      </StackView>
    </form>
  </forms>
</orion>
```

`orionc` generates typed form, menu, toolbar, accelerator, and database
metadata. Create the form with `create_window_from_form()` and continue to use
`evCommand` for notifications. See [Dialogs & DDX](dialogs),
[Database Forms](database-forms), and [MDI Application Architecture](mdi).

Optional status labels can use `flags="LABEL_COLLAPSE_EMPTY"`. An empty label
occupies no layout space or gap; setting its text restores it and updates layout.
A `BUTTON_COMMANDLINK` button accepts the same flag, for a fixed set of choice
slots that appear only when they have text.

## Add A New Repository App

Use one directory per application:

```text
apps/myapp/
├── dialogs/     Modal workflows when needed
├── components/  App-specific reusable controls when needed
├── share/       Icons, seed data, and other runtime assets
└── tests/       App-specific tests
```

Keep persistent models independent of windows and rendering. Put message
procedures in views, application state and command dispatch in a controller,
and static UI/resources in the `.orion` definition. The
[Application Presentation Guide](app-presentation) explains how to document and
capture the finished app.

## Build Against An Installed SDK

```makefile
CC = cc
CFLAGS = -Wall -Wextra -std=c11 -I/opt/orion/include
LIBS = -L/opt/orion/lib -lkernel -lcommctl -lcommdlg -luser -lplatform

myapp: main.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)
```

Use `<orion/ui.h>` for the framework and `<orion/gem.h>` for the dual
standalone/GEM lifecycle. Add Lua flags only when the app uses Lua.

## Where To Go Next

| Goal | Guide |
|---|---|
| Understand windows and client geometry | [Window System](window-system) |
| Handle commands, input, and custom messages | [Messages & Events](messages) |
| Use standard controls and layouts | [Controls](controls) |
| Build declarative dialogs | [Dialogs & DDX](dialogs) |
| Bind forms to records | [Database Forms](database-forms) |
| Build menus and icon toolbars | [Toolbars](toolbars) |
| Build a multi-document application | [MDI Architecture](mdi) |
| Load applications into Orion Shell | [GEM Plugin System](gems) |
| Understand framework ownership | [Architecture](architecture) |
