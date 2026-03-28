# Orion Examples

This directory contains example programs demonstrating the use of Orion.

## Hello World Example

**File:** `helloworld.c`

A simple standalone application that demonstrates:
- Initializing SDL and OpenGL
- Creating windows using Orion
- Handling window messages and events
- Drawing text in a window
- Basic event loop

### Building

```bash
make ui-helloworld
```

### Running

```bash
./ui-helloworld
```

### What it does

The example creates a simple window with "Hello World!" text displayed in the center. The window can be moved, and clicking the close button will exit the application.

### Code Structure

1. **Initialization** - Sets up SDL, creates an OpenGL context
2. **Window Creation** - Uses Orion to create a desktop and a hello world window
3. **Event Loop** - Processes SDL events and window messages
4. **Rendering** - Draws the windows and their contents
5. **Cleanup** - Properly shuts down SDL

### Current Limitations

The example currently links against some mapview files (sprites.c, font.c, icons.c) for drawing functions. This is temporary until these functions are fully extracted into Orion.

## File Manager Example

**File:** `filemanager.c`

A Norton Commander-style file manager that demonstrates:
- Two-column directory listing
- Directory navigation with mouse clicks
- Double-click to navigate into folders or go up with ".."
- Visual selection feedback
- Separating folders from files (folders first)

### Building

```bash
make examples
```

### Running

```bash
./build/bin/filemanager
```

### What it does

The example creates a file manager window showing the current directory contents in two columns. Folders are listed first with a "/" suffix. Click to select an entry, double-click on a folder to navigate into it, or double-click on ".." to go up one level.

## Image Editor Example

**File:** `imageeditor.c`

A MacPaint-inspired raster image editor with a 16-color palette, demonstrating:
- Rendering a pixel buffer as an OpenGL texture updated in real time
- Multiple drawing tools: Pencil (1 px), Brush (5 px soft circle), Eraser, Flood Fill
- 16-color swatch palette and live foreground/background color preview
- Modal file-picker dialog (reuses the columnview-based file browser from `filemanager.c`)
  that filters to **PNG files only**
- PNG open/save via the built-in **stb_image** framework API (`load_image` / `save_image_png`)
- Toolbar with New, Open, and Save actions
- Status bar showing the current file path

### Canvas

The editable canvas is **320 × 200 pixels** displayed at 2× (640 × 400 logical pixels),
matching the classic MacPaint resolution.

### Building

```bash
make examples          # builds imageeditor together with all other examples
```

PNG I/O is provided by the Orion framework via `load_image()` / `save_image_png()` (backed by stb_image).
No additional library installation is required.

### Running

```bash
./build/bin/imageeditor
```

### Tools

| Tool        | Description                                                    |
|-------------|----------------------------------------------------------------|
| Pencil      | 1-pixel precise drawing                                        |
| Brush       | 5-pixel circular brush for smooth strokes                      |
| Eraser      | Paints in the current background color                         |
| Fill        | Flood-fills a contiguous region                                |
| Select      | Rectangular selection (drag to move, cut/copy/paste)           |
| Hand        | Pan the canvas by dragging                                     |
| Zoom        | Left-click to zoom in, right-click to zoom out                 |
| Line        | Straight line between two points                               |
| Rect        | Rectangle (hold Shift for square)                              |
| Ellipse     | Ellipse (hold Shift for circle)                                |
| RoundRect   | Rectangle with rounded corners                                 |
| Polygon     | Click to add vertices; right-click to close and commit         |
| Spray       | Airbrush: scatters random pixels within an 8-pixel radius      |
| Eyedropper  | Left-click to pick foreground color; right-click for background|
| Magnifier   | Shows a 16×16 pixel loupe at 4× zoom in the canvas top-right, centered on the hovered pixel |

Click a tool name in the left panel to activate it.
Click a color swatch to set the foreground color.

### Open / Save

Click **Open** or **Save** in the toolbar. A modal dialog opens showing the file system,
filtered to directories and `.png` files. Navigate into folders by double-clicking them,
select a file (or type a filename for Save), then press **Open**/**Save** to confirm or
**Cancel** to abort.

Only PNG files are supported.

## Future Examples

Additional examples to be added:
- **Button Example** - Demonstrating button controls
- **Form Example** - Creating forms with various controls
- **Dialog Example** - Modal dialogs and message boxes
- **Multi-Window Example** - Managing multiple windows

## Dependencies

Orion examples require:
- SDL2
- OpenGL 3.2+

### Installing Dependencies

**Ubuntu/Debian:**
```bash
sudo apt-get install libsdl2-dev libgl1-mesa-dev
```

**macOS:**
```bash
brew install sdl2
```

**Fedora/RHEL:**
```bash
sudo dnf install SDL2-devel mesa-libGL-devel
```
