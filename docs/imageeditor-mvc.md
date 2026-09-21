---
layout: default
title: Image Editor — MVC Refactoring Plan
nav_exclude: true
---

# Image Editor — MVC Refactoring Plan

This document describes the suggested MVC split for the image editor, following
the same pattern already applied in `apps/socialfeed/` and documented in
the Orion coding conventions.

> **Status:** Historical proposal. The implemented command/history contract is
> documented in [Image Editor architecture](../apps/imageeditor/ARCHITECTURE.md).
>
> Original proposal:  The filters subsystem has already been migrated
> (`filtermgr.c` / `win_filtergallery.c`).  The rest of the editor still
> mixes document logic, pixel operations, and window procedures inside the
> same files.

---

## 1  Current State (What Is Mixed Today)

### The "God File" problem — `win_menubar.c`

`win_menubar.c` currently plays four roles at once:

| Role | Examples |
|---|---|
| **Menu view** | `editor_menubar_proc`, `view_menu_rebuild`, `window_menu_rebuild` |
| **Controller** | `handle_menu_command`, `imageeditor_open_file_path` |
| **Palette window factory** | `create_tool_palette_window`, `create_color_palette_window`, `create_tool_options_window`, `create_main_toolbar_window` |
| **Document I/O** | `png_save`, `show_file_picker` glue code |

### Other boundary violations

| File | Problem |
|---|---|
| `win_document.c` | Contains both `doc_win_proc` (view) and `create_document` / `close_document` / `doc_update_title` / `doc_confirm_close` (model/controller) |
| `canvas.c` | `canvas_upload` (GL) is mixed with pure pixel operations (should be view helper or kept behind a thin upload facade) |
| `layer_t.preview` | `ui_render_effect_t` stored on the document struct — live-preview render state that belongs in the dialog's local struct, not the document model |
| `app_state_t` | Holds both application configuration (palette, filters, accelerators) and tool/UI state (`current_tool`, `brush_size`, `shape_filled`, `wand.*`) |

---

## 2  Proposed Directory Structure

```
apps/imageeditor/
├── model/       Persistent documents, layers, selection, undo, and filters
├── controller/  Application state, commands, and document orchestration
├── views/       Menus, documents, canvas, palettes, layers, and timeline
├── dialogs/     Focused modal editing and configuration workflows
├── io/          Image loading, saving, and file selection
├── animation/   Frame rendering and animation export
└── share/       Icons, palettes, filters, and sample assets
```

---

## 3  Layer Rules

### Model (`model_*.c`)

A model file **must not**:
- Include `window_t *` arguments
- Call `send_message`, `invalidate_window`, `create_window`, `destroy_window`
- Call `draw_*` or `fill_rect`
- Reference `g_app` (global controller state)

A model file **may**:
- Use OpenGL as a *compute tool* (upload texture → run shader → readback → delete) — this is the `filtermgr.c` pattern
- Allocate / free heap memory
- Read and write `canvas_doc_t` and its nested structs

**Rule of thumb:** If you could run the function in a headless test that never
calls `ui_init_graphics`, it belongs in a model file.

### Controller (`controller_app.c`)

The controller owns `g_app` (the `app_state_t` singleton) and is the only
layer allowed to reach across model and view.

Responsibilities:
- Dispatch `handle_menu_command` — decide *what* happens, delegate to model
  or open/close views
- Own the active-document pointer (`g_app->active_doc`)
- Call model functions (canvas mutations, undo, filters)
- Call view factory functions (`create_tool_palette_window`, etc.)
- Push status-bar text after a command completes

The controller **must not** do its own painting or contain `evPaint` handlers.

### View (`view_*.c`)

A view file contains one or more window procedures (`evCreate`, `evPaint`,
`evCommand`, `evDestroy`, …).

**View rules:**
- All transient GPU state (preview textures, thumbnail strips) lives in the
  window's local `state_t` struct, allocated in `evCreate` and freed in
  `evDestroy`.
- A view **reads** from the model through the controller or via direct
  `canvas_doc_t` field access (read-only is fine).
- A view **mutates** the model only by calling a named controller or model
  function — never by reaching into `doc->pixels` directly.
- Commit to the model only on explicit user acceptance (OK / double-click),
  not during live preview.

---

## 4  Specific Migrations

### 4.1  Split `win_menubar.c` → `view_menubar.c` + `controller_app.c`

**Move to `controller_app.c`:**
```
handle_menu_command()
imageeditor_open_file_path()
create_tool_palette_window()
create_main_toolbar_window()
create_tool_options_window()
create_color_palette_window()
imageeditor_sync_main_toolbar()
```

**Keep in `view_menubar.c`:**
```
editor_menubar_proc()
view_menu_rebuild()
window_menu_rebuild()
imageeditor_sync_filter_menu()
s_filter_items[] / s_window_items[] statics
```

`window_menu_rebuild()` mutates file-static menu item arrays (`s_window_items[]`,
`kMenus[]`) and calls `send_message(g_app->menubar_win, ...)`, so it is tightly
coupled to the view's own data structures.  Keep it in `view_menubar.c`; the
controller calls it as a notification after any document open/close event.

`handle_menu_command` may remain in `view_menubar.c` temporarily if the
refactoring is done incrementally — but the long-term target is for it to
live in the controller.

### 4.2  Split `win_document.c` → `view_document.c` + `model_document.c`

**Move to `model_document.c`:**
```c
canvas_doc_t *create_document(const char *filename, int w, int h);
void close_document(canvas_doc_t *doc);
void doc_update_title(canvas_doc_t *doc);
```

`doc_confirm_close(canvas_doc_t *doc, window_t *parent_win)` takes a
`window_t *` so it violates the model rule that bans UI arguments.  Move it to
`controller_app.c` instead, or split it into a pure model predicate
(`doc_needs_save_prompt`) plus a controller helper that shows the dialog and
calls `close_document`.

**Keep in `view_document.c`:**
```c
static result_t doc_win_proc(...)
imageeditor_document_workspace_rect()
imageeditor_max_document_frame_size()
imageeditor_max_canvas_viewport_size()
imageeditor_document_frame_for_viewport()
```

### 4.3  Remove `layer_t.preview` from the document struct

Currently `layer_t` carries:
```c
struct {
  bool                      active;
  ui_render_effect_t        effect;
  ui_render_effect_params_t params;
} preview;
```

This is live-preview render state that belongs in the dialog (blur, levels,
etc.) as a local field, not in the document model.

**Suggested fix:**

1. Remove the `preview` sub-struct from `layer_t`.
2. In each adjustment dialog's local state struct, add:
   ```c
   ui_render_effect_t        preview_effect;
   ui_render_effect_params_t preview_params;
   bool                      preview_active;
   ```
3. The `evPaint` of `view_canvas.c` checks the *dialog's* state, not the
   document.  Pass a pointer via `g_app->active_preview` (a single global
   pointer to a `preview_override_t`) set by the dialog on open and cleared
   on close / cancel.

This matches exactly what `win_filtergallery.c` already does correctly.

### 4.4  Rename files

Pure renames with no code changes (do these first to establish the naming
convention before splitting):

| Old name | New name |
|---|---|
| `undo.c` | `model_undo.c` |
| `anim.c` | `model_anim.c` |
| `filtermgr.c` | `model_filters.c` |
| `canvas.c` | `model_canvas.c` (after extracting `png_save` / image I/O into `model_image_io.c`) |
| `win_canvas.c` | `view_canvas.c` |
| `win_toolpalette.c` | `view_toolpalette.c` |
| `win_tooloptions.c` | `view_tooloptions.c` |
| `win_colorpalette.c` | `view_colorpalette.c` |
| `win_layers.c` | `view_layers.c` |
| `win_timeline.c` | `view_timeline.c` |
| `win_filtergallery.c` | `view_filtergallery.c` |
| `win_blur.c` | `view_blur.c` |
| `win_levels.c` | `view_levels.c` |
| `win_imageresize.c` | `view_imageresize.c` |
| `win_newlayer.c` | `view_newlayer.c` |
| `win_newimage.c` | `view_newimage.c` |
| `win_textdialog.c` | `view_textdialog.c` |
| `win_selectmodify.c` | `view_selectmodify.c` |
| `win_gridoptions.c` | `view_gridoptions.c` |
| `win_extractmask.c` | `view_extractmask.c` |
| `win_menubar.c` | `view_menubar.c` (partial — see §4.1) |
| `win_document.c` | `view_document.c` (partial — see §4.2) |

No `Makefile` source-list edits are needed for renames within
`apps/imageeditor/`: the build discovers source files below the application
directory, so renamed files are
picked up automatically.  The only Makefile change required is updating the
`-DIMAGEEDITOR_SINGLE_LAYER` override on the `imagelite` target if any
extracted files change how single-layer mode is expressed.

---

## 5  Migration Order

The migrations below are ordered so that each step leaves the editor in a
working state.  Do one step per PR.

| Step | Action | Risk |
|---|---|---|
| 1 | Rename `undo.c`, `anim.c`, `filtermgr.c` to `model_*.c` | Low — pure renames; no Makefile change needed (wildcard build) |
| 2 | Rename all `win_*.c` → `view_*.c` (except menubar / document) | Low — pure renames; update any explicit `#include` paths |
| 3 | Extract `create_document` / `close_document` / `doc_update_title` into `model_document.c` | Medium — several callers, update `imageeditor.h` declarations |
| 4 | Create `controller_app.c`; move `handle_menu_command` and `imageeditor_open_file_path` | Medium — large function, test all menu commands |
| 5 | Rename `win_menubar.c` → `view_menubar.c` | Low — cosmetic after step 4 |
| 6 | Remove `layer_t.preview`; add `g_app->active_preview` indirection | High — touch canvas paint path + all adjustment dialogs |
| 7 | Extract `png_save` / image I/O from `canvas.c` into `model_image_io.c`; rename `canvas.c` → `model_canvas.c` | Low — small extraction, then pure rename |

---

## 6  What Must Not Change

- The `app_state_t` struct stays as-is (it is the controller's state bag).
  Clean it up as a separate task after the file split is stable.
- `imageeditor.h` remains the shared umbrella header.  Forward-declare new
  functions there; do not create per-layer headers until the split is complete.
- `main.c` is not touched — `gem_init` / `gem_shutdown` call the controller
  and view factories, which is already correct.
- All accelerator and menu-command IDs are unchanged.
- The `Makefile` uses source discovery, so no source-list edits are needed for renames within `apps/imageeditor/`. Any extracted source file is picked up automatically.

---

## 7  Testing After Each Step

1. **Build** both the `imageeditor` binary and the `imagelite` (single-layer) binary.
2. **Open** a PNG file, edit, undo, redo, save — basic document round-trip.
3. **Filters** — open Photo > Filter Gallery, apply a filter, verify undo.
4. **Layers** — add a layer, merge down, flatten, save.
5. **Animation** — add a frame, switch frames, export GIF.
6. **Close with unsaved changes** — confirm the MB_YESNO dialog appears.

There are no automated tests for the UI paths today.  After the MVC split is
stable, `model_canvas.c` and `model_document.c` are the best candidates for
headless unit tests because they have no window dependencies.
