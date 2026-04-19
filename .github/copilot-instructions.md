# GitHub Copilot Instructions for Orion

## Project Overview

Orion is a UI framework library extracted from DOOM-ED, organized in a Windows-like architecture with three main layers:
- **user/** - Window management and user interface (USER.DLL equivalent)
- **kernel/** - Event loop and SDL integration (KERNEL.DLL equivalent)  
- **commctl/** - Common controls (COMCTL32.DLL equivalent)

The framework is written in C and uses SDL2 for windowing/input and OpenGL 3.2+ for rendering.

## Code Architecture and Conventions

### Directory Structure
- `user/` contains window management, message queue, drawing primitives, and text rendering
- `kernel/` contains SDL event loop, initialization, and joystick/gamepad support
- `commctl/` contains reusable UI controls (buttons, checkboxes, edit boxes, labels, lists, comboboxes, console)
- `examples/` contains example programs demonstrating framework usage
- `ui.h` is the main header that includes all UI subsystems

### WinAPI Philosophy
- The codebase stays close to **WinAPI style** — but uses snake_case for function and type names instead of PascalCase
- When implementing new features, think "how would this be done in WinAPI?" and follow those patterns
- If a required feature is missing from the core framework (e.g., hotkeys/accelerators, timers, clipboard), **add it to the framework** — do not implement workarounds in application code (e.g., do not handle `WM_KEYDOWN` manually where `WM_COMMAND` from an accelerator is the correct mechanism)
- Common WinAPI patterns to follow: message loops, window procedures, control notifications via `WM_COMMAND`, `HIWORD`/`LOWORD` packing, resource tables (menus, accelerators), dialog modal loops
- **Always search the existing framework before inventing new mechanisms.** Orion already has toolbars (`WINDOW_TOOLBAR`), toolbar buttons (`kToolBarMessageAddButtons`), bitmap strips (`bitmap_strip_t`), accelerators, dialogs, status bars, etc. If you need something that sounds like it belongs in a UI framework, look for it first.

### Scrollbars — Built-in vs. Standalone

**`WINDOW_HSCROLL` / `WINDOW_VSCROLL` — built-in scrollbars on a window**

Set these flags at creation time on the window whose **content** scrolls.  The
framework paints the bars automatically and intercepts mouse events in their
area before calling `win->proc`.  Call `set_scroll_info()` to describe the
content range; handle `kWindowMessageHScroll` / `kWindowMessageVScroll` for
position changes.

```c
// Correct: built-in scrollbars on the scrollable content window
window_t *view = create_window("View",
    WINDOW_HSCROLL | WINDOW_VSCROLL,
    MAKERECT(0, 0, w, h), parent, my_view_proc, NULL);

// In kWindowMessageCreate (or whenever content/zoom changes):
scroll_info_t si = {
    .fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
    .nMin  = 0,
    .nMax  = content_w,   // total content size
    .nPage = view_w,      // visible viewport size
    .nPos  = pan_x,       // current offset
};
set_scroll_info(view, SB_HORZ, &si, false);

// Handle scroll notifications:
case kWindowMessageHScroll:
    state->pan_x = (int)wparam;
    sync_scrollbars(win, state);
    invalidate_window(win);
    return true;
case kWindowMessageVScroll:
    state->pan_y = (int)wparam;
    sync_scrollbars(win, state);
    invalidate_window(win);
    return true;
```

**`win_scrollbar` — standalone scrollbar control**

When a scrollbar needs to exist as an independent child window (custom
layouts), use `win_scrollbar`.  Orientation comes from `lparam`:
`(void *)0` = horizontal, `(void *)1` = vertical.  Do **not** set
`WINDOW_HSCROLL` or `WINDOW_VSCROLL` on the scrollbar window itself.

```c
window_t *vsb = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
    MAKERECT(w - 8, 0, 8, h - 8),
    parent, win_scrollbar, (void *)1 /* SB_VERT */);

scrollbar_info_t info = { 0, content_h, view_h, pos };
send_message(vsb, kScrollBarMessageSetInfo, 0, &info);
```

**Common scrollbar mistakes to avoid**

| ❌ Wrong | ✅ Correct |
|---|---|
| Setting `WINDOW_HSCROLL` on a `win_scrollbar` child to indicate orientation | Pass `(void *)0` or `(void *)1` as `lparam`; those flags are for the **parent** |
| Creating `win_scrollbar` children when you want built-in scrollbars | Add `WINDOW_HSCROLL`/`WINDOW_VSCROLL` to the scrollable window and call `set_scroll_info()` |
| Manually painting scrollbar children from the parent `kWindowMessagePaint` | The framework paints built-in bars automatically after calling `win->proc` |
| Forwarding mouse events to scrollbar children | Not needed; the framework intercepts clicks in the bar area before `win->proc` |
| Handling `kScrollBarNotificationChanged` for built-in scrollbars | Handle `kWindowMessageHScroll` / `kWindowMessageVScroll` |
| Forgetting scrollbar interdependence | If one bar appears it shrinks the viewport on the other axis — re-check both `need_h` / `need_v` after setting either |

### Toolbars and Bitmap-Strip Icon Buttons

**WINDOW_TOOLBAR — built-in toolbar strip above a window's client area**

Any window can have a toolbar strip by setting the `WINDOW_TOOLBAR` flag at creation time. The strip is painted automatically by the framework above the title bar. Buttons are added with `kToolBarMessageAddButtons`, each described by a `toolbar_button_t {icon, ident, active}` where `icon` is a `sysicon_*` value from `user/icons.h`.

**Built-in system icons (sysicon_\* / SYSICON_BASE)**

Orion ships a 20×20 grid PNG icon sheet.  In the source tree, the asset lives at `share/icon_sheet_16x16.png`; at runtime it is deployed and loaded from `share/orion/icon_sheet_16x16.png` automatically at startup.  All ~398 icons are listed in `user/icons.h` as `sysicon_<name>` enum values starting at `SYSICON_BASE` (0x10000).  When a toolbar button's `icon` field is `>= SYSICON_BASE` the engine draws it from the built-in sheet — **no `kToolBarMessageLoadStrip` call is needed**.

```c
#include "user/icons.h"

// Correct: use sysicon_* values directly — framework sources them from
//          the built-in PNG sheet automatically.
static const toolbar_button_t kDocToolbar[] = {
  { sysicon_add,    ID_FILE_NEW,  0 },
  { sysicon_folder, ID_FILE_OPEN, 0 },
  { sysicon_save,   ID_FILE_SAVE, 0 },
};
send_message(doc, kToolBarMessageAddButtons,
             sizeof(kDocToolbar)/sizeof(kDocToolbar[0]),
             (void *)kDocToolbar);
```

For `win_toolbar_button` windows, use `ui_get_sysicon_strip()` to obtain the pre-loaded strip and pass `sysicon_X - SYSICON_BASE` as the index:

```c
bitmap_strip_t *s = ui_get_sysicon_strip();
if (s)
    send_message(btn, kButtonMessageSetImage,
                 (uint32_t)(sysicon_add - SYSICON_BASE), s);
```

**win_toolbar_button + bitmap_strip_t — sprite-sheet icon buttons (TB_ADDBITMAP style)**

When icons come from a *custom* PNG sprite sheet (not the built-in sheet), use `win_toolbar_button` and `bitmap_strip_t`. This is the Orion equivalent of WinAPI's `TB_ADDBITMAP` / `TBBUTTON.iBitmap`:
- Load the strip once; store a single `bitmap_strip_t {tex, icon_w, icon_h, cols, sheet_w, sheet_h}`.
- Each button stores only an integer **index** (iBitmap). The icon at index `n` occupies tile `(n % cols, n / cols)`.
- Send `kButtonMessageSetImage(wparam=index, lparam=&strip)` — the button owns a private copy, the caller needs no lifetime guarantee.

```c
// Correct: one strip loaded once, each button gets only an index
bitmap_strip_t strip = { .tex=tex, .icon_w=16, .icon_h=16,
                         .cols=2, .sheet_w=32, .sheet_h=160 };
for (int i = 0; i < NUM_TOOLS; i++) {
    window_t *btn = create_window(tool_names[i], flags,
        MAKERECT(bx, by, bw, bh), parent, win_toolbar_button, NULL);
    send_message(btn, kButtonMessageSetImage, (uint32_t)tool_icon_idx[i], &strip);
}
```

**Common mistakes to avoid**

| ❌ Wrong | ✅ Correct |
|---|---|
| Per-button `{col, row, sheet_w, sheet_h}` stored in a custom struct | Single `bitmap_strip_t` shared across all buttons; each button stores only an **index** |
| Adding a `BUTTON_BITMAP` flag to `win_button` | Use `win_toolbar_button` proc (separate class for bitmap buttons) |
| Handling icon clicks in a custom `WM_LBUTTONDOWN` in a palette window proc | Use `WINDOW_TOOLBAR` + `kToolBarMessageAddButtons`; clicks fire `kToolBarMessageButtonClick` |
| Inventing a bespoke floating-window class for a toolbar | Use `WINDOW_TOOLBAR` on the document window, or a separate toolbar window with `win_toolbar_button` children |
| Hard-coding texture dimensions (`TOOLS_TEX_W/H`) | Derive `cols` from the actually loaded PNG width: `cols = loaded_w / icon_w` |
| Calling `kToolBarMessageLoadStrip` just to use common icons | Use `sysicon_*` values directly — the engine loads the built-in sheet automatically |

### Naming Conventions
- Use snake_case for function names (e.g., `create_window`, `draw_text_small`)
- Use snake_case with _t suffix for type names (e.g., `window_t`, `rect_t`, `winproc_t`)
- Use SCREAMING_SNAKE_CASE for constants and macros (e.g., `WM_CREATE`, `SCREEN_WIDTH`)
- Window message constants start with `WM_` (e.g., `WM_PAINT`, `WM_LBUTTONDOWN`)
- Button messages start with `BN_` (e.g., `kButtonNotificationClicked`)
- Combobox messages start with `CB_` or `CBN_` (e.g., `CB_ADDSTRING`, `CBN_SELCHANGE`)
- Edit box messages start with `EN_` (e.g., `kEditNotificationUpdate`)

### Code Style
- Use K&R-style bracing with opening brace on same line
- Use 2-space indentation
- Functions should have minimal comments unless explaining complex logic
- Header files use include guards with pattern `#ifndef __UI_SUBSYSTEM_H__`
- Prefer standard C types (int, bool, etc.) with stdint.h types when size matters (uint32_t, uint16_t)
- Use forward declarations to minimize header dependencies

### Struct Design
- Always prefer named structs over loose coordinate pairs: use `point_t { int x, y; }` instead of `int x, int y` pairs, and `rect_t { int x, y, w, h; }` instead of `int x1, y1, x2, y2`
- `point_t` and `rect_t` are defined in `user/user.h` and available everywhere via `ui.h`
- When a concept naturally groups two or more related values, define a struct for it (e.g., `size_t` for `w, h`; `point_t` for `x, y`)
- Do not scatter parallel `_x` / `_y` (or `_start` / `_end`) fields across a struct when a `point_t` member would be cleaner

### Message-Based Architecture
- All UI interaction uses a Windows-style message system
- Window procedures follow the signature: `result_t (*winproc_t)(window_t *, uint32_t msg, uint32_t wparam, void *lparam)`
- Common messages include WM_CREATE, WM_DESTROY, WM_PAINT, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_KEYDOWN, WM_KEYUP, WM_COMMAND
- Return true from window proc if message was handled, false otherwise

### Drawing and Rendering
- Use OpenGL for all rendering (hardware accelerated)
- Text rendering uses small bitmap font (6x8 pixels)
- Drawing functions include: `draw_text_small()`, `draw_rect()`, `fill_rect()`, `draw_icon8()`, `draw_icon16()`
- Always call `init_text_rendering()` at startup and `shutdown_text_rendering()` at cleanup
- Colors are specified as RGBA uint32_t values
- Use `ui_begin_frame()` and `ui_end_frame()` to manage rendering context

### Memory Management
- Use malloc/free for dynamic allocations
- Window structures are managed by Orion - don't manually free them
- Always pair init functions with corresponding shutdown functions

## Common Tasks

### Adding a New Control
1. Create implementation file in `commctl/` directory (e.g., `newcontrol.c`)
2. Add window procedure function following pattern `win_newcontrol()`
3. Declare the window procedure in `commctl/commctl.h`
4. Handle at minimum: WM_CREATE, WM_PAINT, WM_DESTROY, and any control-specific messages
5. Add usage example to README.md if it's a major control

### Creating a Dialog or Panel with Multiple Controls (use forms)

**Always use `form_def_t` + `show_dialog_from_form()` for any dialog or panel
that contains two or more standard controls.**  Never build children imperatively
inside `kWindowMessageCreate` when a static form definition can express the
same layout.

```c
// ── 1. Declare children (static, compile-time) ────────────────────
static const form_ctrl_def_t kMyDlgChildren[] = {
  { FORM_CTRL_TEXTEDIT, 1, {60, 8, 80, 13}, 0,              "",       "name"   },
  { FORM_CTRL_BUTTON,   2, {50,30, 40, 13}, BUTTON_DEFAULT, "OK",     "ok"     },
  { FORM_CTRL_BUTTON,   3, {94,30, 50, 13}, 0,              "Cancel", "cancel" },
};
static const form_def_t kMyDlg = {
  .name="My Dialog", .w=160, .h=52,
  .children=kMyDlgChildren, .child_count=3,
};

// ── 2. Window procedure — children already exist at kWindowMessageCreate ──
static result_t my_dlg_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      win->userdata = lparam;
      set_window_item_text(win, 1, "default value"); // populate at runtime
      return true;
    case kWindowMessagePaint:
      draw_text_small("Name:", 4, 11, get_sys_color(kColorTextDisabled));
      return false;
    case kWindowMessageCommand:
      if (HIWORD(wparam) == kButtonNotificationClicked) {
        window_t *src = (window_t *)lparam;
        if (src->id == 2) { end_dialog(win, 1); return true; }
        if (src->id == 3) { end_dialog(win, 0); return true; }
      }
      return false;
    default: return false;
  }
}

// ── 3a. Show as modal dialog ────────────────────────────────────────
// show_dialog_from_form() auto-centers, adds WINDOW_DIALOG flags, runs loop.
show_dialog_from_form(&kMyDlg, "My Dialog", parent, my_dlg_proc, &st);

// ── 3b. Instantiate as modeless / embedded window ──────────────────
create_window_from_form(&kMyDlg, x, y, parent, my_dlg_proc, NULL);
```

**Key rules:**
- `form_ctrl_def_t` supports: `FORM_CTRL_BUTTON`, `FORM_CTRL_CHECKBOX`,
  `FORM_CTRL_LABEL`, `FORM_CTRL_TEXTEDIT`, `FORM_CTRL_LIST`, `FORM_CTRL_COMBOBOX`.
- `show_dialog_from_form()` handles centering and dialog flags — no
  `MAKERECT((sw-W)/2, ...)` boilerplate needed.
- Runtime values (initial edit text, checkbox states) are set inside
  `kWindowMessageCreate` via `set_window_item_text()` / `get_window_item()` —
  the children are already present when that message fires.
- The form editor (see `examples/formeditor/`) can generate the struct literals
  directly in its saved `.h` output.

### Creating Example Programs
1. Place examples in `examples/` directory
2. Include `../ui.h` for all UI functionality
3. Follow the pattern in `examples/helloworld.c`
4. Document build and run instructions in `examples/README.md`
5. Always initialize with `ui_init_graphics()` and cleanup with `ui_shutdown_graphics()`

### Working with Text Rendering
- For small fixed-width text: use `draw_text_small()` with `strwidth()` for measurements
- Font rendering is OpenGL-based using texture atlases for efficiency
- Always initialize the text system with `init_text_rendering()` before use

## Dependencies

- SDL2 (libsdl2-dev on Ubuntu/Debian)
- OpenGL 3.2 or later (mesa-libGL-devel on Fedora/RHEL)
- Standard C library

## Current Status

✅ Completed:
- Header files defining API structure
- Common controls (button, checkbox, edit, label, list, combobox, console)
- Text rendering module (bitmap and game fonts)
- Console module for message display
- Example hello world program

⏳ In Progress:
- Extracting core window management from mapview
- Extracting drawing primitives from mapview
- Additional example programs

## Testing and Building

- No automated testing infrastructure exists yet
- Build system integration is planned (Makefile to be added)
- The framework is designed to be integrated into existing build systems

## Debugging and Reproduction Logging

- When fixing interactive bugs, log **user actions and state transitions** so the exact repro path is visible from logs.
- Prefer platform logging (`axSetLogFile`, `axLog`, `axLogFlush`) over ad-hoc `printf`/`stderr` so logs are captured in a persistent file.
- Log at action boundaries (command dispatch, mouse down/up, tool change, dialog open/close), not every frame, to keep logs readable.
- Include enough context to replay the issue: active document/window id, command id/name, selected index/item, and key mode flags.
- Keep logging behind a debug toggle (`*_DEBUG`) so it can be enabled during investigation and disabled for normal runs.

## When Adding Features

- Maintain compatibility with the existing message-based architecture
- Follow Windows API patterns where applicable (familiar to many developers)
- Keep the layered architecture clean (user/kernel/commctl separation)
- **Extend the framework rather than making workarounds**: if something logically belongs in the framework (e.g., timers, clipboard, accelerators, drag-and-drop), add it to the appropriate layer (`user/`, `kernel/`, or `commctl/`) and expose a clean API
- **Search existing framework before implementing anything new**: grep the codebase for the concept first (e.g., "toolbar", "bitmap", "strip"). Orion already ships toolbars, toolbar buttons, bitmap strips, accelerators, dialogs, status bars, and form-based window creation. Reimplementing these as custom structs or flags is always wrong.
- **Use `form_def_t` + `show_dialog_from_form()` for all dialogs/panels**: any window with two or more standard child controls must be expressed as a static `form_ctrl_def_t[]` + `form_def_t` and instantiated with `create_window_from_form()` or `show_dialog_from_form()`. Never build children imperatively inside `kWindowMessageCreate` — children defined in a form already exist when that message fires.
- Add documentation to README.md for new public APIs
- Consider adding examples for non-trivial new functionality

### Anti-Patterns (learned from real mistakes)

1. **Don't store per-icon `{col, row}` in each button.** A sprite sheet is a strip of fixed-size tiles. Load it once, derive `cols = texture_w / icon_w`, and give each button only an integer index. WinAPI's `TBBUTTON.iBitmap` is the canonical model — follow it exactly.

2. **Don't add a new flag to an existing control class when a new control class is the right answer.** `win_button` is a text-label button. Icon-strip buttons are `win_toolbar_button`. These are distinct classes, just as `TBSTYLE_BUTTON` and `BS_PUSHBUTTON` are distinct WinAPI styles.

3. **Don't build a custom floating palette window from scratch when `WINDOW_TOOLBAR` already exists.** Any window gains a built-in toolbar strip via `WINDOW_TOOLBAR` + `kToolBarMessageAddButtons`. Only create a separate palette/floating window when the design genuinely requires it (e.g., Photoshop's detachable toolbox), and even then, use `win_toolbar_button` children rather than custom paint code.

4. **Don't hard-code texture dimensions.** When loading a PNG, always propagate the actual loaded `w`/`h` into the strip descriptor (`strip.sheet_w = loaded_w; strip.cols = loaded_w / icon_w`). Never assume a fixed size — the file found on a fallback path may differ.

5. **Don't put `WINDOW_HSCROLL` / `WINDOW_VSCROLL` on a `win_scrollbar` window.** Those flags mean "this window has built-in framework scrollbars" and are intercepted by `send_message()`. The orientation of a standalone `win_scrollbar` is set via `lparam` at create time: `(void *)0` = horizontal, `(void *)1` = vertical. See the [Scrollbars](#scrollbars----built-in-vs-standalone) section above.

6. **Don't create child controls imperatively in `kWindowMessageCreate` when a `form_def_t` can do it declaratively.** Dialogs and panels with standard controls (buttons, edit boxes, labels, checkboxes, lists, comboboxes) must use `form_ctrl_def_t[]` + `form_def_t`, passed to `create_window_from_form()` or `show_dialog_from_form()`. Writing `create_window(…, win_button, …)` inside a window proc is wrong. The form's children already exist when `kWindowMessageCreate` fires — use `get_window_item()` / `set_window_item_text()` to read or initialise them.
