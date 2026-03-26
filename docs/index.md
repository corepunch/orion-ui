---
layout: home
title: Home
nav_order: 1
permalink: /
---

<img width="1536" height="1024" alt="image" src="https://github.com/user-attachments/assets/793ada60-d34d-4dfa-ac3a-688c1d70f67b" />

Orion is a retro-styled UI framework written in C that brings the familiar Windows API message-based architecture to modern cross-platform development. Extracted from DOOM-ED, it features a clean three-layer design modeled after classic Windows DLLs (USER, KERNEL, COMCTL), making it instantly recognizable to developers who've worked with Win32. Built on SDL2 and OpenGL 3.2+, Orion delivers hardware-accelerated rendering with a nostalgic bitmap font aesthetic reminiscent of DOS and early Windows interfaces. The framework provides a complete set of common controls (buttons, checkboxes, edit boxes, lists, combo boxes, and a console) all following message-driven patterns that feel both vintage and powerful. Perfect for game tools, retro-style applications, or anyone who misses the simplicity and directness of classic GUI programming.

**filemanager.c** example:
![Screenshot 2026-01-16 at 15 21 17](https://github.com/user-attachments/assets/1474fcfa-17eb-4731-8af5-06a83ace958f)

**helloworld.c** example:
![Screenshot 2026-01-16 at 15 20 19](https://github.com/user-attachments/assets/57ef5b20-56ff-4d4c-8057-d7f2e699a08e)

## Directory Structure

```
ui/
├── ui.h              # Main header that includes all UI subsystems
├── user/             # Window management and user interface (USER.DLL equivalent)
│   ├── user.h        # Window structures and management functions
│   ├── messages.h    # Window message constants and macros
│   ├── draw.h        # Drawing primitives (rectangles, icons)
│   ├── text.h        # Text rendering functions
│   ├── text.c        # Text rendering implementation (small font, DOOM/Hexen fonts)
│   ├── window.c      # Window management implementation
│   ├── message.c     # Message queue implementation
│   └── draw_impl.c   # Drawing primitives implementation
├── kernel/           # Event loop and SDL integration (KERNEL.DLL equivalent)
│   ├── kernel.h      # Event management and SDL initialization
│   ├── renderer.h    # Renderer API - OpenGL abstraction
│   ├── renderer_impl.c # Renderer API implementation
│   ├── renderer.c    # Sprite rendering implementation
│   ├── event.c       # Event loop implementation
│   ├── init.c        # SDL initialization
│   └── joystick.c    # Joystick/gamepad support
└── commctl/          # Common controls (COMCTL32.DLL equivalent)
    ├── commctl.h     # Common control window procedures
    ├── button.c      # Button control implementation
    ├── checkbox.c    # Checkbox control implementation
    ├── edit.c        # Text edit control implementation
    ├── label.c       # Label (static text) control implementation
    ├── list.c        # List control implementation
    ├── combobox.c    # Combobox (dropdown) control implementation
    ├── console.c     # Console control implementation
    ├── columnview.h  # ColumnView control header
    ├── columnview.c  # Multi-column item view implementation
    ├── menubar.h     # Menu bar control header
    ├── menubar.c     # Horizontal menu bar implementation
    └── terminal.c    # Lua script terminal implementation
```

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

Manages the SDL event loop and translates SDL events into window messages. Also provides the Renderer API for OpenGL abstraction.

**Key Components:**
- SDL initialization
- Event loop (`get_message`, `dispatch_message`)
- Global state (screen dimensions, running flag)
- **Renderer API**: High-level OpenGL abstraction (`R_Mesh`, `R_Texture`, `R_MeshDrawDynamic`)

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
- **MenuBar**: Horizontal menu bar with dropdown menus
- **Terminal**: Interactive Lua script terminal with input/output

## Building

Orion supports Linux, macOS, and Windows platforms.

### Dependencies

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install liblua5.4-dev libsdl2-dev libgl1-mesa-dev libcglm-dev libpng-dev
```

**macOS:**
```bash
brew install sdl2 cglm lua libpng
```

**Windows (MSYS2/MinGW64):**
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-SDL2 mingw-w64-x86_64-lua mingw-w64-x86_64-cglm mingw-w64-x86_64-libpng make
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
