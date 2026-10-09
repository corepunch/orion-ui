
## Design skill

For UI design and review, use the [`orion-ui-design`](.agents/skills/orion-ui-design/SKILL.md) skill (adapted from Apple HIG principles for Orion's retro style).
# Debug macros

These macros control verbose debug logging. Set to `1` at build time to enable,
`0` to disable. Default is `0` (off).

| Macro | File | Description |
|---|---|---|
| `GITCLIENT_DEBUG` | `examples/gitclient/gitclient.h` | gitclient app logging |
| `TABLEVIEW_DEBUG` | `commctl/tableview.c` | tableview control logging |
| `SOCIALFEED_DEBUG` | `examples/socialfeed/socialfeed.h` | socialfeed app logging |
| `IMAGEEDITOR_DEBUG` | `examples/imageeditor/imageeditor.h` | imageeditor app logging |
| `TASKMANAGER_DEBUG` | `examples/taskmanager/taskmanager.h` | taskmanager app logging |

Example: `make CFLAGS="-DGITCLIENT_DEBUG=1 -DTABLEVIEW_DEBUG=1"`

# Logging

Do not add always-on logging for routine user actions or state changes. Keep
verbose diagnostics behind the app or module debug flag, disabled by default.

## Framework error logging

All incorrect or rejected framework behavior must be logged unconditionally to
`stderr`; never hide these diagnostics behind a debug macro. This includes
invalid message parameters, out-of-range indices, unavailable state, failed
resource allocation, and rejected state transitions. Include the framework
module prefix, window id when available, and the relevant values so the caller
can identify the bad request and its current state. Flush after diagnostics
that precede an early return.

# Window input architecture

Before changing mouse hit-testing, scrolling, scrollbars, toolbars, or nested
window dispatch, read [ARCHITECTURE.md](ARCHITECTURE.md#window-and-input-event-routing).
The central parent-to-child mouse router is `handle_mouse()` in `user/event.c`;
coordinate conversion belongs in the window system, not in individual views.

## Coordinate delivery to child windows

`handle_mouse()` receives coordinates in the **parent's viewport space**. When
dispatching to a child, coordinates must be converted to the **child's content
space** by adding the child's scroll offset:

```c
int lx = x - c->frame.x + (int)c->hscroll.pos;
int ly = y - c->frame.y + (int)c->vscroll.pos;
```

This ensures nested child windows receive content-space coordinates including
their scroll offsets, regardless of how many layout containers they are nested
inside. Missing `+ vscroll.pos` causes click-to-select after scrolling to hit
the wrong row (off by `vscroll.pos / ENTRY_HEIGHT`). This is a system-level
responsibility — no view or control should compensate for it.

# Code style

## Naming and formatting

- C99, no C++
- K&R bracing, 2-space indent
- `snake_case` for functions and variables; `snake_case_t` for types; `SCREAMING_SNAKE_CASE` for constants and macros
- Include guards: `#ifndef __MODULE_NAME_H__`
- Prefer `stdint.h` types (`uint32_t`, `uint16_t`) when size matters
- Prefer `ipoint16_t` / `irect16_t` over bare `int x, int y` pairs — matches WinAPI `POINT` / `RECT` convention
- **Prefer high-level rect/point utilities** over manual `x,y,w,h` arithmetic. Use `rect_split_*`, `rect_trim_*`, `rect_inset`, `rect_offset`, `rect_center`, `rect_contains_point` (all in `orion/user/rect.h`) to express layout and hit-test intent declaratively. Reserve bare field access for cases where no existing utility covers the operation.
- Minimal comments — only where logic is genuinely non-obvious

## Vertical space

Minimize vertical space. When a pattern repeats (switch cases, similar
assignments, etc.), format it as a compact aligned table — each entry on one
line, columns aligned — to read like a spreadsheet. Avoid wasted lines.

```c
// Good — compact, scannable, like a spreadsheet:
case AX_KEY_ENTER:     vgat_pty_write(st->pty_fd, "\r", 1);     return true;
case AX_KEY_BACKSPACE: vgat_pty_write(st->pty_fd, "\x7f", 1);   return true;
case AX_KEY_ESCAPE:    vgat_pty_write(st->pty_fd, "\x1b", 1);   return true;

// Bad — bloated:
case AX_KEY_ENTER:
  vgat_pty_write(st->pty_fd, "\r", 1);
  return true;
```

# WinAPI → Orion reference

Orion deliberately mirrors the WinAPI mental model. Think WinAPI first, then map:

| WinAPI concept | Orion equivalent |
|---|---|
| `HWND` | `window_t *` |
| `WNDPROC` | `winproc_t` — `result_t fn(window_t*, uint32_t msg, uint32_t wparam, void *lparam)` |
| `WM_*` messages | `kWindowMessage*` constants (e.g. `evCreate`, `evPaint`, `evDestroy`) |
| `CreateWindow` | `create_window(title, flags, rect, parent, proc, userdata)` |
| `DestroyWindow` | `destroy_window(win)` |
| `ShowWindow` | `show_window(win, visible)` |
| `InvalidateRect` | `invalidate_window(win)` |
| `GetMessage` / `DispatchMessage` | `get_message(&e)` / `dispatch_message(&e)` + `repost_messages(-1)` |
| `WM_COMMAND` routing | `evCommand`, `HIWORD(wparam)` = notification code, `LOWORD(wparam)` = control id |
| `TranslateAccelerator` | `translate_accelerator(win, table, &e)` before `dispatch_message` |
| `DialogBox` / `EndDialog` | `show_dialog(parent, proc, userdata)` / `end_dialog(win, result)` |
| `SetWindowLongPtr` / user data | `win->userdata` (allocated with `allocate_window_data(win, size)`) |
| `RECT` | `irect16_t { int x, y, w, h; }` via `MAKERECT(x,y,w,h)` |
| `POINT` | `ipoint16_t { int x, y; }` |
| `BN_CLICKED` | `btnClicked` |
| `CB_ADDSTRING` / `CBN_SELCHANGE` | `CB_ADDSTRING` / `CBN_SELCHANGE` |

# Framework patterns

## Message handling

Every window proc must handle at minimum `evCreate`, `evPaint`, and `evDestroy`.
Notifications always travel as `evCommand` to the parent.

- Return `true` if you handled a message, `false` if you did not (like returning 0 vs. calling `DefWindowProc`).
- Control IDs go in `LOWORD(wparam)`, notification codes in `HIWORD(wparam)`.

```c
// Correct — HIWORD = notification, LOWORD = control ID
send_message(parent, evCommand, MAKEDWORD(id, btnClicked), (void *)win);
```

## Accelerator tables over raw key handling

Never handle `evKeyDown` directly for keyboard shortcuts. Use accelerator tables:

```c
// BAD — polling keys bypasses the framework
case evKeyDown:
  if (wparam == AX_KEY_S) save_file();
  break;

// GOOD — use load_accelerators / translate_accelerator
accel_t table[] = {
  {MOD_CTRL, AX_KEY_S, ID_FILE_SAVE},
};
accel_table_t *accel = load_accelerators(table, ARRAY_LEN(table));
```

Accelerators fire as `evCommand` with `kAcceleratorNotification` in `HIWORD(wparam)`.

## Shapes come from the theme, not from stacked rectangles

A visual that has a shape (card, badge, pill, tab, ring) is drawn by **one framework call** whose geometry
comes from the active theme (`card_corner_radius`, `card_edge_width`, ...). Apps and controls never
approximate it by layering fills, insetting overlapping rounded rects, or clipping one shape with
another; those tricks leak past the silhouette (sub-pixel slivers at the corners) and ignore the theme.

- **Composite views are sub-windows, laid out by auto-layout.** A card, tile or row is a container
  (`Card`, `StackView`, `FlowView`, `TileGrid`) whose children are ordinary controls (`Label`, `Badge`,
  buttons). Never build one as a single control that paints its content and hit-tests it with hand-computed
  coordinates; measure/arrange, truncation, wrapping and reflow belong to the layout system. Containers
  created in code are configured with `window_set_layout()`.
- An accent edge is a **plain rectangle**. Hand it to the card-drawing function
  (`draw_card(r, state, edge_color)`); the theme rounds or squares the card and its edge together,
  in a single pass that shares one silhouette.
- Colours are theme roles (`brTextError`, `brTextWarning`, `brTextInfo`, `brTextSuccess`, ...). Never
  write literal RGB values in a control. Derive softer variants with `color_with_alpha()`.
- Truncated text goes through `draw_text_ellipsized()`; tinted labels through `draw_badge()`.
- If the primitive you need does not exist, add it to `user/` (and a metric to `theme_t` if its shape
  varies by theme), document it in `docs/drawing.md`, then use it. Do not build it in the app.

## Repainting

- Any state change that affects appearance must call `invalidate_window(win)`.
- Never call drawing functions outside `evPaint`. Trigger repaint via `invalidate_window`.
- Use `allocate_window_data(win, size)` for per-window state (like `SetWindowLongPtr`).

## Resource cleanup

Every `evCreate` that allocates resources (strings, buffers, textures) must have a matching
`evDestroy` that frees them. Leaks in window procs are a bug.

## Separation of concerns

- Application-level logic stays out of controls; framework-level logic stays out of apps.
- Framework features (timers, clipboard, drag-and-drop) belong in `kernel/` or `user/`, not in app code.
- No raw OpenGL calls outside `kernel/renderer.c` / `kernel/renderer_impl.c`.

## Extend, don't reinvent

When an app needs control behaviour that existing Orion controls don't yet support,
extend the framework control in `orion/commctl/` rather than implementing a
custom version in app code.  The command-panel tab row was built with custom
toolbar-button radio groups; when tabs needed icons, the right fix was to add
`tcSetImageStrip` / `tcSetTabIcon` to `win_tabview` and switch the app to use
it, not to keep the custom implementation.  Adding a message or option to a
framework control is always preferable to duplicating the control's event
routing, hit-testing, keyboard handling, and accessibility.

## Build applications from Orion components

For app UI work, compose framework controls and layouts before writing app-specific
window procedures. Inspect the existing controls, `.orion` parser/code generator,
and reference apps (especially `socialfeed` for database-bound forms and
`formeditor` for declarative component registration) before choosing an approach.
If a reusable capability is missing, add it to Orion and make the app consume it;
do not recreate control behavior or a small UI framework inside the app.

- Treat the app's `.orion` file as the source of truth for static UI: forms,
  menus, toolbars, accelerators, datasource schemas, bindings, and repeated-item
  templates belong there whenever the engine supports them. Keep C for runtime
  behavior, data access, and genuinely dynamic state; avoid a parallel C-built
  version of a declarative view.
- Lay out related labeled form fields in one `GridView`, with a shared
  `<Column width="auto">` for labels and a stretching input column. All text
  inputs and dropdowns in that group must share their left and right edges.
  Put trailing actions such as Browse in a separate grid column when needed.
  Use `StackView` for action rows and independent groups; separate horizontal
  stacks for each field measure labels independently and misalign inputs.
  Follow `imageeditor.orion`'s `image_resize` form as the reference.
- Model data shown by engine-rendered views with datasources. Use datasource
  bindings for library/document records and other data-driven collections; use
  declarative toolbar/menu definitions for static commands, and add a toolbar
  datasource when toolbar contents are genuinely data-driven and the engine can
  render that source. Extend the engine generically if the needed binding is
  missing.
- Separate the concepts: a **form** defines a screen or document view; a
  **FlowView** or other layout control arranges its children; a repeated-item
  template defines each record's presentation; a **datasource** supplies those
  records. For example, a library can be a form containing a FlowView bound to
  a library datasource, with a card template for each block.
- Give each custom window/control a named window class and a focused window
  procedure. Register every class used by declarative forms both at runtime and
  with FormEditor/component metadata so forms can be opened and edited there.
  Keep database adaptation, command routing, and view behavior in separate
  modules with narrow responsibilities.
- When the engine cannot yet instantiate a declared form or repeat a template,
  implement the missing generic framework support first where practical. If a
  temporary app-specific bridge is unavoidable, keep it small, name the gap,
  and avoid duplicating static layout or control behavior in that bridge.

For a substantial app UI change, follow the project skill
[`orion-app-composition`](.agents/skills/orion-app-composition/SKILL.md).

# Icon design

Use only [Phosphor](https://phosphoricons.com/) for app and UI icons.

- Store app icons in `apps/<name>/share/icons/` as SVG files displayed at 24×24 px.
- Use upstream authored assets. Preserve their path geometry and viewBox; do not redraw icons or impose a different stroke weight or coordinate grid.
- Use Phosphor's regular weight for unselected icons and the matching fill weight for selected icons. Supply the filled asset through `checked-icon` where supported.
- Keep icons theme-aware with `currentColor`, preserving the upstream outline or filled artwork.
- Record the upstream revision and asset names, and include Phosphor's license with the assets.

# Sample reference

Groove sound, role, and timbre decisions are judged against two local WAV
libraries. They are listed in `.gitignore`. Leave them untracked.

| Library | Path | Catalogue |
|---|---|---|
| Dance eJay | `apps/groove/ejay-samples/` | `index.tsv`: product, group, name, variation, bpm, samples, seconds, source, path |
| MTV Music Generator | `apps/groove/mtv-samples/` | `index.tsv`: genre, group, name, sample, instrument, kit, samples, seconds, path |

Read the catalogue, then the clip. Dance eJay folders are musical families. MTV
folders are the instrument pool; the genre of a riff is the `genre` column.
The eJay container format is `apps/groove/docs/dance-ejay-pxd.md`.

Keep the commercial audio and the original titles out of the app. Groove
synthesizes or records its own material in the same roles.

# Repository layout

```
ui.h              ← include this in every app; pulls in all subsystems
user/             ← window management, message queue, drawing, text, accelerators
kernel/           ← SDL event loop, init, renderer
commctl/          ← reusable controls: button, checkbox, edit, label, list, combobox, console
tests/            ← all test source files (*.c)
tests/test_framework.h   ← the test framework
tests/test_env.h ← SDL-init helper for tests that need a display
Makefile          ← `make test` builds and runs all tests/
```

## App folder structure

Every app lives in `apps/<name>/` and follows this layout:

```
apps/<name>/
  <name>.orion          ← declarative UI definition (forms, databases, menus, toolbars)
  <name>.h              ← app header: types, column IDs, prefix aliases
  main.c                ← entry point: DB_CLASS registration, create_database, window creation
  controller.c          ← top-level event routing (evCommand dispatch, tab switching)
  view_*.c              ← top-level window procs (main window, menubar, diff viewer)
  dialogs/              ← modal dialogs, one dlg_*.c per dialog (optional)
  components/           ← reusable sub-controls specific to this app (optional)
  pages/                ← multi-page apps: one subfolder per page (optional)
    <page>/
      page_<page>.c     ← page window proc + page-specific logic
      page_<page>.h
  datasource/           ← database adaptors (optional, for apps with 2+ adaptors)
    <name>_db.c         ← dbproc_t implementation for each <database> in the .orion
  share/                ← resources: icons/, seed XML, test fixtures
    icons/              ← 24×24 SVG icons
  tests/                ← app-specific test files
```

**When to use `datasource/`:** If an app has a single database adaptor, keep it in the
app root (e.g. `db_simple_xml.c` in socialfeed). If an app has two or more adaptors,
move them into `datasource/` to make the data layer visually distinct from the view
layer. The adaptor filename should match the `<database class="...">` attribute in the
.orion file (e.g. `class="gitclient_db"` → `datasource/gitclient_db.c`).

**Database adaptor contract:** Each adaptor implements `dbproc_t` and must handle
`dbCreate`, `dbDestroy`, `dbLoad`, `dbFetch`, `dbGetObjectProc`, `dbGetFieldBindings`,
`dbGetSchema`, and `dbGetFieldMeta`. The `dbGetApi` message is optional (nothing sends
it currently). Adaptors that share table IDs (e.g. changes_db reusing `TABLE_FILES`)
must coordinate column IDs through the app header.
