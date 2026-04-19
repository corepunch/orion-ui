---
layout: default
title: Drawing & Rendering
nav_order: 6
---

# Drawing & Rendering

All drawing is hardware-accelerated via **OpenGL 3.2+**.  Drawing calls are
only valid inside a `evPaint` handler; the framework sets the
correct viewport and projection before calling your proc.

## Coordinate System

Inside `evPaint` the coordinate origin **(0, 0)** is the
**top-left corner of the window's content area**.  Positive Y goes
**downward**.  Units are logical pixels (screen pixels ÷ `UI_WINDOW_SCALE`).

## Colour Format

Colours are `uint32_t` values in **0xAABBGGRR** byte order on little-endian
systems (matching `GL_RGBA, GL_UNSIGNED_BYTE`):

```c
// Helper macro: build a colour from components
#define RGBA(r,g,b,a) \
  ((uint32_t)(a)<<24 | (uint32_t)(b)<<16 | (uint32_t)(g)<<8 | (uint32_t)(r))

// Named constants defined in messages.h
COLOR_PANEL_BG          // 0xff3c3c3c – dark grey panel
COLOR_TEXT_NORMAL       // 0xffc0c0c0 – light grey text
COLOR_FOCUSED           // 0xff5EC4F3 – blue focus highlight
```

## Primitives

### `fill_rect`

Fill a solid-colour rectangle.

```c
void fill_rect(uint32_t color, int x, int y, int w, int h);

// Example: dark background
fill_rect(COLOR_PANEL_BG, 0, 0, win->frame.w, win->frame.h);

// Example: red square
fill_rect(RGBA(255,0,0,255), 10, 10, 50, 50);
```

### `draw_rect`

Render a textured quad (OpenGL texture).

```c
void draw_rect(GLuint texture, int x, int y, int w, int h);

// Example: canvas texture displayed at 2× scale
draw_rect(my_tex, 0, 0, CANVAS_W * 2, CANVAS_H * 2);
```

### `draw_bevel`

Draw a 3-D bevelled border around a rect.

```c
void draw_bevel(rect_t const *r);
```

### Icons

```c
// 8×8 icon from the built-in icon sheet
void draw_icon8(int icon_id, int x, int y, uint32_t color);

// 16×16 icon
void draw_icon16(int icon_id, int x, int y, uint32_t color);
```

Icon IDs are defined in the `icon8_t` / `icon16_t` enums in `messages.h`.

## Text Rendering

### Small Bitmap Font (6×8 pixels)

```c
// Draw a string; color = -1 uses default text colour
void draw_text_small(const char *text, int x, int y, uint32_t color);

// Measure a string in pixels
int strwidth(const char *text);

// Example
int w = strwidth("Hello");
draw_text_small("Hello", (win->frame.w - w) / 2, 10, COLOR_TEXT_NORMAL);
```

### Initialisation

```c
// Call once at startup (before any draw_text_small calls)
void init_text_rendering(void);
void shutdown_text_rendering(void);
```

`ui_init_graphics` calls `init_text_rendering` automatically; you only need
to call it manually when building without the kernel layer.

## OpenGL Textures (Canvas Pattern)

Create a texture once, update it with `glTexSubImage2D` only when the pixel
buffer is dirty:

```c
GLuint tex = 0;
bool dirty = true;

// In evPaint:
if (!tex) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
} else if (dirty) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}
dirty = false;
draw_rect(tex, 0, 0, W * SCALE, H * SCALE);
```

## Status Bar Text

```c
// Update the status bar string (triggers a repaint)
send_message(win, evStatusBar, 0, (void *)"File saved");
```
