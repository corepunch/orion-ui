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

### Cards, badges and theme colours

`fill_gradient_rounded_rect(top, bottom, r, radius)` paints a vertical gradient
in one SDF pass. The endpoints are packed sRGB colours; interpolation happens
in linear light, with alpha interpolated independently. Radius 0 fills the
complete rectangle, while a positive radius masks the same fill with one
antialiased silhouette. Coordinates and radius are logical pixels, including
Retina scaling. No gradient texture or per-row geometry is allocated.

Modern and Navy share blue surfaces and cyan selection. Selected/pressed
toolbar items, list/sidebar rows, and active menu items use `brSelectionTop`
and `brSelectionBottom` from the active palette. `set_sys_colors()` can override
either endpoint. Light keeps equal endpoints for a solid selection; Classic
keeps its original palette and drawing.

```c
// Card with an optional accent edge; the theme rounds (or squares) card and edge together.
draw_card(r, CTRL_HOVER | CTRL_SELECTED | CTRL_FOCUSED, color_with_alpha(get_sys_color(brTextWarning), 0x78));

// Tinted label ("3 modified"); returns its width so badges can be chained.
int w = draw_badge(FONT_SMALL, "3 modified", x, y, 18, get_sys_color(brTextWarning));

uint32_t soft = color_with_alpha(color, 0x40);   // same colour, different alpha
```

The accent edge is a plain `card_edge_width` rectangle clipped by the card's silhouette in the shader, so
there is nothing to align or mask by hand. Geometry lives in `theme_t` (`card_corner_radius`,
`card_edge_width`, `card_ring_width`). Pass an edge colour with zero alpha for no edge.

Status colours come from the theme: `brTextError`, `brTextWarning`, `brTextInfo`, `brTextSuccess`, `brTextDisabled`.

A rounded outline is one stroke, not two stacked fills. `thickness` is the inside band in logical pixels; `radius` 0 is a square stroke.

Modern text fields and comboboxes use capsule ends: their radius follows half the
field height, including desktop and iPad toolbar sizes. Classic keeps its bevelled geometry.

```c
stroke_rounded_rect(get_sys_color(brAccent), r, get_theme()->card_corner_radius, 2);
```

`draw_gradient_card(r, state, color)` draws a light-to-saturated diagonal gradient, a light rim (brightest on top),
and optional `CTRL_SELECTED` ring in one shader pass with one rounded silhouette.
`CTRL_HOVER` brightens the face. The input colour's alpha applies to the entire card,
including its ring and highlight. Theme metrics `card_corner_radius`, `card_ring_width`,
and `card_highlight_width` own the geometry; Classic uses square corners and no highlight.
The ring margin is reserved in every state, so selection never moves the content.
Inset waveform or text content past that margin before painting it.
Use `brTextOnColor` for dark labels and waveforms on these bright tinted surfaces.

### Image-backed backgrounds and skins

`image_atlas_load()` loads a PNG unchanged into a shared sRGB texture. An
`image_background_t` names source regions for normal, selected, pressed, hover
and disabled artwork. Backgrounds preserve authored color and alpha; disabled
art is not tinted again. State priority is disabled, pressed, selected, hover,
normal. Empty optional states reuse normal. Rejected reloads retain the old atlas.

```c
image_atlas_t atlas = {0};
image_atlas_load(&atlas, "skin.png");
image_background_t face = {
  .atlas = &atlas,
  .states = { {0, 0, 200, 128} },
  .source_border = {28, 0, 28, 0},
  .border = {8, 0, 8, 0}
};
window_set_image_background(button, &face);
```

Borders use `irect16_t` fields as **left, top, right, bottom**, in source pixels
and destination logical pixels respectively. Zero top/bottom borders draw only
three horizontal slices: fixed end caps and a stretched middle, preserving the
full-height artwork without vertical slicing. Groove uses this form for clips
in both the library and canvas. Nonzero top/bottom borders enable nine-slice
scaling for other skins. Small destinations shrink opposing borders
proportionally without overlap or painting outside the requested bounds.

Buttons, toolbar-button controls and Card containers honor
`window_set_image_background()`. Passing NULL restores their themed face.
Custom views can call `draw_image_background()` or
`draw_window_image_background()` inside `evPaint`. The setter copies the
descriptor and invalidates the window; destruction frees that copy. The caller
owns the atlas and must keep it alive until all users are destroyed, then call
`image_atlas_free()`. Reloaded atlases must keep all source regions in bounds.

### `draw_rect`

Render a textured quad (OpenGL texture).

```c
void draw_rect(GLuint texture, int x, int y, int w, int h);

// Example: canvas texture displayed at 2x scale
draw_rect(my_tex, 0, 0, CANVAS_W * 2, CANVAS_H * 2);
```

### `draw_bevel`

Draw a 3-D bevelled border around a rect.

```c
void draw_bevel(irect16_t const *r);
```

### Icons

```c
// 8x8 icon from the built-in icon sheet
void draw_icon8(int icon_id, int x, int y, uint32_t color);

// 16x16 icon
void draw_icon16(int icon_id, int x, int y, uint32_t color);
```

Icon IDs are defined in the `icon8_t` / `icon16_t` enums in `messages.h`.

## Text Rendering

### Small Bitmap Font (6x8 pixels)

```c
// Draw a string; color = -1 uses default text colour
void draw_text_small(const char *text, int x, int y, uint32_t color);

// Measure a string in pixels
int strwidth(const char *text);

// Example
int w = strwidth("Hello");
draw_text_small("Hello", (win->frame.w - w) / 2, 10, COLOR_TEXT_NORMAL);
```

### Fitting text

```c
// Cut on a UTF-8 boundary and append "..." so the result is at most max_w pixels wide
draw_text_ellipsized(FONT_SMALL, title, x, y, max_w, color);
int w = text_ellipsize(FONT_SMALL, title, max_w, buf, sizeof(buf));   // when you need the string
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

## Signed-distance fonts

`orion/user/font_sdf.h` builds a signed-distance-field glyph atlas from a TTF
for text that must stay sharp at any size, scale or rotation (motion graphics,
zoomable views). It is CPU only: glyphs are generated lazily into an R8 atlas,
and `font_sdf_take_dirty()` reports the region to upload with your renderer.
Metrics and `font_sdf_kern()` are in pixels at the atlas base size. The edge
value is 128 and each texel of distance changes it by `128 / padding`, so a
shader recovers screen-space coverage from the sample and its derivatives.
Scener's reel renderer (`apps/scener/reel_draw.c`) is the reference user.

### Procedural plastic surfaces

`draw_plastic_button(rect, state, color, icon)` draws a tinted plastic button and
solid white SVG glyph in one shader pass. `color` is packed `0xAABBGGRR`; zero
uses `brAccent`. Icon names use the SVG cache at their final draw size, avoiding a second scaling
of the glyph mask. Glyph placement and press offsets snap to device pixels, and
the glyph has no emboss, highlight halo or inner shadow. The shader adds a smooth
vertical gradient, directional button bevel and a small drop shadow.
Normal, hover, selected, pressed and disabled states share the same allocated
rectangle. Disabled takes precedence over interaction flags.

The shadow stays inside `rect`; no extra layer, framebuffer or neighbouring
repaint margin is needed. Theme metrics `plastic_corner_radius`,
`plastic_bevel_width` and `plastic_shadow_size` are logical pixels. Radius clamps
to half the face size, allowing rounded rectangles, capsules and circles. SDF
antialiasing uses screen derivatives to follow display density and transforms.

`draw_plastic_card(rect, state, color)` uses the same shader with the theme's
card radius and a shallow bevel, without a glyph. Waveforms and labels remain
ordinary content drawn over the procedural surface.
