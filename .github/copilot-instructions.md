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

### Toolbars and Bitmap-Strip Icon Buttons

**WINDOW_TOOLBAR — built-in toolbar strip above a window's client area**

Any window can have a toolbar strip by setting the `WINDOW_TOOLBAR` flag at creation time. The strip is painted automatically by the framework above the title bar. Buttons are added with `kToolBarMessageAddButtons`, each described by a `toolbar_button_t {icon, ident, active}` where `icon` is an `icon16_t` enum value rendered via `draw_icon16()`.

```c
// Correct: attach a toolbar to a document window
window_t *doc = create_window("Untitled", WINDOW_TOOLBAR | WINDOW_STATUSBAR,
    MAKERECT(x, y, w, h), NULL, my_doc_proc, NULL);

static const toolbar_button_t kDocToolbar[] = {
    { icon16_new,  ID_FILE_NEW,  false },
    { icon16_open, ID_FILE_OPEN, false },
    { icon16_save, ID_FILE_SAVE, false },
};
send_message(doc, kToolBarMessageAddButtons,
             sizeof(kDocToolbar)/sizeof(kDocToolbar[0]),
             (void *)kDocToolbar);
```

**win_toolbar_button + bitmap_strip_t — sprite-sheet icon buttons (TB_ADDBITMAP style)**

When icons come from a PNG sprite sheet rather than built-in `icon16_t` enums, use `win_toolbar_button` and `bitmap_strip_t`. This is the Orion equivalent of WinAPI's `TB_ADDBITMAP` / `TBBUTTON.iBitmap`:
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

## When Adding Features

- Maintain compatibility with the existing message-based architecture
- Follow Windows API patterns where applicable (familiar to many developers)
- Keep the layered architecture clean (user/kernel/commctl separation)
- **Extend the framework rather than making workarounds**: if something logically belongs in the framework (e.g., timers, clipboard, accelerators, drag-and-drop), add it to the appropriate layer (`user/`, `kernel/`, or `commctl/`) and expose a clean API
- **Search existing framework before implementing anything new**: grep the codebase for the concept first (e.g., "toolbar", "bitmap", "strip"). Orion already ships toolbars, toolbar buttons, bitmap strips, accelerators, dialogs, and status bars. Reimplementing these as custom structs or flags is always wrong.
- Add documentation to README.md for new public APIs
- Consider adding examples for non-trivial new functionality

### Anti-Patterns (learned from real mistakes)

1. **Don't store per-icon `{col, row}` in each button.** A sprite sheet is a strip of fixed-size tiles. Load it once, derive `cols = texture_w / icon_w`, and give each button only an integer index. WinAPI's `TBBUTTON.iBitmap` is the canonical model — follow it exactly.

2. **Don't add a new flag to an existing control class when a new control class is the right answer.** `win_button` is a text-label button. Icon-strip buttons are `win_toolbar_button`. These are distinct classes, just as `TBSTYLE_BUTTON` and `BS_PUSHBUTTON` are distinct WinAPI styles.

3. **Don't build a custom floating palette window from scratch when `WINDOW_TOOLBAR` already exists.** Any window gains a built-in toolbar strip via `WINDOW_TOOLBAR` + `kToolBarMessageAddButtons`. Only create a separate palette/floating window when the design genuinely requires it (e.g., Photoshop's detachable toolbox), and even then, use `win_toolbar_button` children rather than custom paint code.

4. **Don't hard-code texture dimensions.** When loading a PNG, always propagate the actual loaded `w`/`h` into the strip descriptor (`strip.sheet_w = loaded_w; strip.cols = loaded_w / icon_w`). Never assume a fixed size — the file found on a fallback path may differ.
