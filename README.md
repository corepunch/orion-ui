<img width="1536" height="1024" alt="image" src="https://github.com/user-attachments/assets/793ada60-d34d-4dfa-ac3a-688c1d70f67b" />


Orion is a retro-styled UI framework written in C that brings the familiar Windows API message-based architecture to modern cross-platform development. Extracted from DOOM-ED, it features a clean three-layer design modeled after classic Windows DLLs (USER, KERNEL, COMCTL), making it instantly recognizable to developers who've worked with Win32. Built on the [corepunch/platform](https://github.com/corepunch/platform) layer and OpenGL 3.2+, Orion delivers hardware-accelerated rendering with a nostalgic bitmap font aesthetic reminiscent of DOS and early Windows interfaces. The framework provides a complete set of common controls (buttons, checkboxes, edit boxes, lists, combo boxes, and a console) all following message-driven patterns that feel both vintage and powerful. Perfect for game tools, retro-style applications, or anyone who misses the simplicity and directness of classic GUI programming.

**filemanager.c** example:
![536838382-1474fcfa-17eb-4731-8af5-06a83ace958f](https://github.com/user-attachments/assets/ec7bce63-7595-418c-9d71-66b860cd699c)

**helloworld.c** example:
![Screenshot 2026-01-16 at 15 20 19](https://github.com/user-attachments/assets/57ef5b20-56ff-4d4c-8057-d7f2e699a08e)

## Architecture

Orion follows a layered architecture similar to Windows:

### ui/user/ - Window Management Layer
Handles window creation, destruction, message passing, and basic rendering primitives.

**Key Components:**
- Window structure (`window_t`)
- Window creation and lifecycle management
- Message queue and dispatch
- Drawing primitives (rectangles, text, icons)
- Window messages (kWindowMessageCreate, kWindowMessagePaint, kWindowMessageLeftButtonUp, etc.)

### ui/kernel/ - Event Management Layer
Manages the platform (corepunch/platform) event loop and translates platform events into window messages. Also provides the Renderer API for OpenGL abstraction.

**Key Components:**
- Platform initialization (WI_Init)
- Event loop (`get_message`, `dispatch_message`)
- Global state (screen dimensions, running flag)
- **Renderer API**: High-level OpenGL abstraction (`R_Mesh`, `R_Texture`, `R_MeshDrawDynamic`)
  - See [docs/RENDERER_API.md](docs/RENDERER_API.md) for detailed documentation

### ui/commctl/ - Common Controls Layer
Implements standard UI controls that can be used to build interfaces.

**Available Controls:**
- **Button**: Clickable button with text label
- **Checkbox**: Toggle control with checkmark
- **Edit**: Single-line text input control
- **Label**: Static text display
- **List**: Scrollable list of items
- **Combobox**: Dropdown selection control
- **Console**: Message display console with automatic fading and scrolling
- **ColumnView**: Multi-column item view with icons, colors, and double-click support
- **Terminal**: Interactive Lua script terminal with input/output (process finishes like Windows CMD)

## Building

Orion supports Linux, macOS, and Windows platforms.

### Dependencies

**Linux (Ubuntu/Debian):**
```bash
git submodule update --init
sudo apt-get install liblua5.4-dev libgl-dev libegl-dev libx11-dev libcglm-dev
```

**macOS:**
```bash
brew install sdl2 cglm lua
```

**Windows (MSYS2/MinGW64):**
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make  mingw-w64-x86_64-lua mingw-w64-x86_64-cglm make
```

### Build Commands

```bash
# Build library (static and shared)
make library

# Build examples
make examples

# Build and run tests
make test

# Clean build artifacts
make clean
```

### Build Output

- **Linux**: `build/lib/liborion.a`, `build/lib/liborion.so`
- **macOS**: `build/lib/liborion.a`, `build/lib/liborion.dylib`
- **Windows**: `build/lib/liborion.a`, `build/lib/liborion.dll`
- **Examples/Tests**: `build/bin/`

## Usage

Include the main header in your code:

```c
#include "ui/ui.h"
```

Or include specific subsystems:

```c
#include "ui/user/user.h"
#include "ui/commctl/commctl.h"
```

### Creating a Window

```c
rect_t frame = {100, 100, 200, 150};
window_t *win = create_window("My Window", 0, &frame, NULL, my_window_proc, NULL);
show_window(win, true);
```

### Creating Controls

```c
// Create a button
window_t *btn = create_window("Click Me", 0, &btn_frame, parent, win_button, NULL);

// Create a checkbox
window_t *chk = create_window("Enable Feature", 0, &chk_frame, parent, win_checkbox, NULL);

// Create an edit box
window_t *edit = create_window("Enter text", 0, &edit_frame, parent, win_textedit, NULL);

// Create a console window
window_t *console = create_window("Console", 0, &console_frame, parent, win_console, NULL);

// Create a columnview
window_t *columnview = create_window("", WINDOW_NOTITLE | WINDOW_TRANSPARENT, &cv_frame, parent, win_columnview, NULL);
```

### Using the ColumnView

```c
#include "ui/commctl/columnview.h"

// Create a columnview control
rect_t cv_rect = {0, 0, 400, 300};
window_t *cv = create_window("", WINDOW_NOTITLE | WINDOW_TRANSPARENT, &cv_rect, parent, win_columnview, NULL);
show_window(cv, true);

// Add items to the columnview
columnview_item_t item;
strncpy(item.text, "Item 1", sizeof(item.text) - 1);
item.text[sizeof(item.text) - 1] = '\0';
item.icon = ICON_FOLDER;  // 8x8 icon index
item.color = COLOR_TEXT_NORMAL;  // RGBA color
item.userdata = my_data_ptr;  // Optional user data pointer
send_message(cv, CVM_ADDITEM, 0, &item);

// Set column width (optional, default is 160)
send_message(cv, CVM_SETCOLUMNWIDTH, 180, NULL);

// Handle notifications in parent window procedure
case kWindowMessageCommand: {
  uint16_t id = LOWORD(wparam);
  uint16_t code = HIWORD(wparam);
  
  if (id == cv->id) {
    if (code == CVN_SELCHANGE) {
      int index = (int)(intptr_t)lparam;
      // Selection changed to index
    } else if (code == CVN_DBLCLK) {
      int index = (int)(intptr_t)lparam;
      // Item at index was double-clicked
    }
  }
  break;
}

// Clear all items
send_message(cv, CVM_CLEAR, 0, NULL);

// Get item count
int count = send_message(cv, CVM_GETITEMCOUNT, 0, NULL);

// Get/set selection
int sel = send_message(cv, CVM_GETSELECTION, 0, NULL);
send_message(cv, CVM_SETSELECTION, new_index, NULL);
```

### Using the Console

```c
#include "ui/commctl/console.h"

// Initialize the console system (call once at startup)
init_console();

// Print messages to the console
conprintf("Game started");
conprintf("Player health: %d", player_health);

// Messages automatically fade after 5 seconds
// Draw the console overlay (called from your render loop or window procedure)
// draw_console() is called automatically by win_console in kWindowMessagePaint

// Toggle console visibility
toggle_console();

// Clean up (call at shutdown)
shutdown_console();
```

### Using the Terminal

The terminal control supports two modes:

#### Lua Script Mode
```c
// Create a terminal window that runs a Lua script
// The script path is passed as lparam in kWindowMessageCreate
window_t *terminal = create_window("Terminal", 0, &term_frame, parent, win_terminal, "/path/to/script.lua");
show_window(terminal, true);

// The terminal automatically handles:
// - Running Lua scripts with custom print() and io.read() functions
// - Interactive input when script calls io.read()
// - Displaying "Process finished" message when script completes
// - Preventing further input after process finishes (like Windows CMD)
// - Text wrapping to fit window width
// - Vertical scrolling for long output (use mouse wheel to scroll)

// Example Lua script (interactive.lua):
// print("What is your name?")
// local name = io.read()
// print("Hello, " .. name .. "!")
```

#### Command Mode
```c
// Create an interactive command terminal (pass NULL as lparam)
window_t *terminal = create_window("Terminal", 0, &term_frame, parent, win_terminal, NULL);
show_window(terminal, true);

// Built-in commands:
// - "help"  - Lists available commands
// - "clear" - Clears the terminal screen
// - "exit"  - Closes the terminal window
//
// Features:
// - Text wrapping to fit window width
// - Vertical scrolling for long output (use mouse wheel to scroll)
// - New commands can be added by editing the terminal_commands[] array in commctl/terminal.c
```

## Window Messages

The framework uses a message-based architecture. Common messages include:

- `kWindowMessageCreate` - Window is being created
- `kWindowMessageDestroy` - Window is being destroyed
- `kWindowMessagePaint` - Window needs to be redrawn
- `kWindowMessageLeftButtonDown` - Left mouse button pressed
- `kWindowMessageLeftButtonUp` - Left mouse button released
- `kWindowMessageKeyDown` - Key pressed
- `kWindowMessageKeyUp` - Key released
- `kWindowMessageCommand` - Control notification

## Control-Specific Messages

### Button Messages
- `kButtonNotificationClicked` - Button was clicked

### Checkbox Messages
- `kButtonMessageSetCheck` - Set checkbox state
- `kButtonMessageGetCheck` - Get checkbox state

### Combobox Messages
- `kComboBoxMessageAddString` - Add item to combobox
- `kComboBoxMessageGetCurrentSelection` - Get currently selected item
- `kComboBoxMessageSetCurrentSelection` - Set currently selected item
- `kComboBoxNotificationSelectionChange` - Selection changed notification

### Edit Box Messages
- `kEditNotificationUpdate` - Text was modified

### ColumnView Messages
- `CVM_ADDITEM` - Add item with icon, color, text, and userdata
- `CVM_DELETEITEM` - Remove item by index
- `CVM_GETITEMCOUNT` - Get total item count
- `CVM_GETSELECTION` - Get current selection index
- `CVM_SETSELECTION` - Set selection by index
- `CVM_CLEAR` - Clear all items
- `CVM_SETCOLUMNWIDTH` - Set column width (default 160)
- `CVM_GETCOLUMNWIDTH` - Get current column width
- `CVM_GETITEMDATA` - Get item data by index
- `CVM_SETITEMDATA` - Update item data
- `CVN_SELCHANGE` - Selection changed notification
- `CVN_DBLCLK` - Item double-clicked notification

## Text Rendering API

Orion provides text rendering through `ui/user/text.h`:

### Small Bitmap Font (6x8 pixels)

```c
#include "ui/user/text.h"

// Initialize text rendering system (call once at startup)
init_text_rendering();

// Draw text with small bitmap font
draw_text_small("Hello World", x, y, 0xFFFFFFFF); // color as RGBA

// Measure text width
int width = strwidth("Hello World");
int partial_width = strnwidth("Hello", 5);

// Advanced text rendering with wrapping and scrolling (NEW)
// Calculate text height with wrapping
int height = calc_text_height(text, window_width);

// Draw text with wrapping and viewport clipping
draw_text_wrapped(text, x, y, width, height, 0xFFFFFFFF);

// Clean up (call at shutdown)
shutdown_text_rendering();
```

### DOOM/Hexen Game Font

```c
// Load game font from WAD file (call after loading WAD)
load_console_font();

// Draw text with game font (includes fade-out effect)
draw_text_gl3("DOOM", x, y, 1.0f); // alpha 0.0-1.0

// Measure game font text width
int width = get_text_width("DOOM");
```

### Notes
- Small bitmap font supports all 128 ASCII characters
- DOOM/Hexen font supports characters 33-95 (printable ASCII)
- Font atlas is created automatically for efficient rendering
- Text rendering uses OpenGL for hardware acceleration
- `draw_text_wrapped()` automatically wraps text to fit within specified width
- `calc_text_height()` calculates total height needed for wrapped text
- Both functions handle text that exceeds viewport bounds efficiently


## Status

This is an in-progress refactoring. The framework currently:

✅ Has header files defining the API structure
✅ Has extracted common control implementations (button, checkbox, edit, label, list, combobox, console)
✅ Has extracted text rendering to `ui/user/text.c` (small bitmap font and DOOM/Hexen fonts)
✅ Has moved console to `ui/commctl/console.c` (console message management and display)
✅ Has integrated with build system (Makefile)
✅ Has comprehensive cleanup functions with no memory leaks
⏳ Still needs core window management code to be moved from mapview/window.c
⏳ Still needs drawing primitives to be moved from mapview/sprites.c

## Memory Management

Orion has been verified to have proper cleanup with no memory leaks:

### Cleanup Functions
- `ui_shutdown_graphics()` - Main cleanup function that:
  - Destroys all windows
  - Cleans up all window hooks
  - Shuts down joystick subsystem (if initialized)
  - Cleans up renderer resources (shaders, VAO, VBO)
  - Cleans up white texture
  - Cleans up console and text rendering
  - Destroys OpenGL context and platform window
  - Calls WI_Shutdown()

All cleanup functions are idempotent and safe to call multiple times.

### Memory Leak Verification
Orion has been tested with Valgrind to ensure no memory leaks:
- **definitely lost: 0 bytes** ✅
- **indirectly lost: 0 bytes** ✅
- **possibly lost: 0 bytes** ✅

To verify cleanup yourself:
```bash
# Build tests
make test

# Run with valgrind
valgrind --leak-check=full ./build/bin/test_memory_leak_test
valgrind --leak-check=full ./build/bin/test_integration_cleanup
```

## Recent Changes

### Cleanup and Memory Management (January 2026)
- **Comprehensive cleanup functions**: Added `cleanup_all_windows()` and `cleanup_all_hooks()`
- **Idempotent cleanup**: All cleanup functions now safe to call multiple times
- **Memory leak verification**: Verified with Valgrind - zero memory leaks
- **Resource cleanup**: Proper cleanup of OpenGL resources (textures, VAOs, VBOs, shaders)
- **State clearing**: All static state properly cleared on shutdown

### Text Rendering Module (ui/user/text.c, ui/user/text.h)
- **Small bitmap font rendering**: `draw_text_small()`, `strwidth()`, `strnwidth()`
- **DOOM/Hexen font rendering**: `draw_text_gl3()`, `get_text_width()`, `load_console_font()`
- **Font atlas management**: Automatic creation of font texture atlas for efficient rendering
- Extracted from mapview/windows/console.c to make text rendering reusable

### Console Module (ui/commctl/console.c, ui/commctl/console.h)
- **Console message management**: Circular buffer for console messages with timestamps
- **Message display**: Automatic fading and scrolling of recent messages
- **Public API**: `init_console()`, `conprintf()`, `draw_console()`, `shutdown_console()`, `toggle_console()`
- Uses text rendering module for display
- Moved from mapview/windows/console.c to UI framework

## Future Work

1. Move core window management functions to `ui/user/window.c`
2. Move message queue to `ui/user/message.c`
3. Move drawing primitives to `ui/user/draw.c`
4. Update Xcode project with new file locations
5. Add documentation for each function
6. Add example code
7. Add unit tests for UI components
