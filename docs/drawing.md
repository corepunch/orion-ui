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

Page panes redirect subtree painting to a reusable child surface. The drawing
system keeps coordinates and clipping relative to the root while translating the
viewport into that surface. The window system composites it once with
`theme.tab.corner_radius`; controls do not paint corner covers. Internal
`set_draw_surface()` / `get_draw_surface()` select and restore the paint target.

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

`fill_gradient_rounded_top_rect(top, bottom, r, radius)` uses the same single-pass
gradient with rounded top corners and square bottom corners. Modern, Light and
Navy inactive tabs fade from their current face colour to `brTabBarBg` at the
bottom, giving a recessed shadow. Selected tabs use equal `brControlBg` endpoints
to merge with the pane.

`THEME_PART_PAGE` paints the Page background with `brPanelDarker`, including pane
margins and splitter gaps. The tab strip uses `brTabBarBg`, lighter than the Page
background and darker than inactive tab faces (`brPanelDark`).

Modern and Navy share blue surfaces and cyan selection. Selected/pressed
toolbar items, list/sidebar rows, and active menu items use `brSelectionTop`
and `brSelectionBottom` from the active palette. `set_sys_colors()` can override
either endpoint. Light keeps equal endpoints for a solid selection; Classic
keeps its original palette and drawing.

Amber is a light business palette for data-entry applications: white wells
(`brColumnViewBg`) for lists and fields, warm grey chrome, a graphite caption
(`brActiveTitlebar`), and pale amber selection. Text and icons drawn on an
accent fill (selected rows, the default button, the hovered menu item) use
`brAccentText`, which is dark here and white in the other palettes. Buttons and
fields take their corner radii from `button.corner_radius` and
`field.corner_radius` (`CORNER_RADIUS_CIRCULAR` for capsules; Amber uses small
rounded rectangles). A field's text inset, `field.padding_x`, follows its
roundness: a capsule keeps `TEXTEDIT_PADDING_CAPSULE` clear of its round ends,
a squarer field takes `FIELD_PADDING_X(radius)`. Build an app with it via `make THEME_<app>=amber`.

Porcelain shares Amber's metrics with cool white surfaces, white wells and a
solid indigo selection carrying white text; Porcelain Dark is the same design
on graphite. In both, a merged title row (`THEME_PART_TITLE_TOOLBAR`) shares
the window face over a hairline, and a sidebar (`THEME_PART_SIDEBAR`,
`brPanelDark`) is the tinted column, as in Finder. Other themes paint the title
row like any toolbar band. `brDesktopBg` colours the desktop behind root windows.

### Light and dark appearance

Each `theme_t` records whether it is dark and names its counterpart in the
other appearance (`appearance.dark`, `appearance.counterpart`): Light ↔ Modern
and Porcelain ↔ Porcelain Dark. Classic, Navy and Amber have none.
`set_appearance(APPEARANCE_SYSTEM)`, the default, follows the OS: a visible UI
switches to the matching counterpart at start-up and on `kEventAppearanceChanged`,
which the macOS and Windows platforms post when the user changes the system
appearance (Linux and WebGL report the appearance at start-up only).
`APPEARANCE_LIGHT` and `APPEARANCE_DARK` pin a variant. `set_theme()` still
chooses the theme; the next OS change or `set_appearance()` may switch it to its
counterpart. Headless (`UI_INIT_HIDDEN`) contexts never follow the OS, so tests
do not depend on the machine. A standalone app takes `--appearance light|dark|system`.

```c
// Card with an optional accent edge; the theme rounds (or squares) card and edge together.
draw_card(r, CTRL_HOVER | CTRL_SELECTED | CTRL_FOCUSED, color_with_alpha(get_sys_color(brTextWarning), 0x78));

// Tinted label ("3 modified"); returns its width so badges can be chained.
int w = draw_badge(FONT_SMALL, "3 modified", x, y, 18, get_sys_color(brTextWarning));

// Independent foreground and background, with capsule/circular ends.
draw_badge_ex(FONT_SMALLEST, "1", R(x, y, 16, 16), get_sys_color(brActiveTitlebarText),
              color_with_alpha(get_sys_color(brModalOverlay), 0xB8), true);

uint32_t soft = color_with_alpha(color, 0x40);   // same colour, different alpha
```

The accent edge is a plain `card.edge_width` rectangle clipped by the card's silhouette in the shader, so
there is nothing to align or mask by hand. Geometry lives in `theme_t` (`card.corner_radius`,
`card.edge_width`, `card.ring_width`). Pass an edge colour with zero alpha for no edge.
The hovered card face is the 50% midpoint of `brControlBg` and `brButtonHover`.

`fill_rounded_rect_bands(color, rect, radius, band_width, offset)` overlays every
other vertical band inside one rounded silhouette. Band zero is transparent;
`offset` anchors the bands to a source wider than the visible content, so padding
and cropping preserve their positions. Width, offset and radius use logical pixels.

Status colours come from the theme: `brTextError`, `brTextWarning`, `brTextInfo`, `brTextSuccess`, `brTextDisabled`.

A rounded outline is one stroke, not two stacked fills. `thickness` is the inside band in logical pixels; `radius` 0 is a square stroke.

Modern text fields and comboboxes use capsule ends: their radius follows half the
field height, including desktop and iPad toolbar sizes. Classic keeps its bevelled geometry.

```c
stroke_rounded_rect(get_sys_color(brAccent), r, get_theme()->card.corner_radius, 2);
```

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

### Tooltip bubbles

`SpeechBubble` is a reusable vertical layout container for interactive bubble
content. It reserves the bottom tail and shadow outside its children and accepts
`bbSetTailOffset` to point at an anchor. `theme_t.speechbubble` owns its corner
radius, tail width/length and shadow independently of ordinary tooltips. Its informational surface uses `brInfoBackground` and `brInfoText`: pale
yellow in light palettes and a muted warm surface in dark palettes.
`draw_speech_bubble()` draws the fill, one-pixel outline and tail from one signed
distance silhouette. The default tail is 10 pixels wide and 16 pixels long, with
one upright edge and one diagonal edge, mirrored toward the anchor. These
proportions follow the four 10×16 tail sprites in
[clippy.js](https://github.com/clippyjs/clippy.js/blob/master/build/clippy.css), an
open reconstruction of the assistant. The outline follows only the compound
shape's exterior; the tail's base never becomes an internal stroke.
`speech_bubble_body_rect()` supplies the matching layout/hit-test body. The
control is registered with the runtime and FormEditor like `Card`.

`draw_tooltip_bubble(rect, tail_offset, side)` draws a rounded face and attached
tail in one fill pass. Its shadow reuses `draw_rect_shadow()` on the rectangular
body, with a downward offset; the tail does not change shadow rendering.
The tint comes from `theme_t.tooltip.shadow.color`, independently of border colors.
`rect` includes the theme's shadow margin, and `tail_offset` is relative to that
rectangle along the arrow's edge. `side` is `TOOLTIP_TAIL_TOP`, `BOTTOM`, `LEFT`,
or `RIGHT`. `tooltip_bubble_body_rect(rect, side)` returns the body without the
shadow margin or arrow, for laying out text. The theme supplies the geometry
and spacing. Toolbar tooltips appear toward the workspace: right of a left
toolbar, left of a right toolbar, below a top toolbar, and above a bottom toolbar.
They flip to the opposite side when needed to stay on-screen. Other controls
prefer a tooltip above the element. All tooltips appear immediately.
Hover lookup starts with the deepest child and continues through its parents
until one supplies tooltip text. That source also supplies the anchor, so moving
across a card's labels keeps its tooltip stable. A child's own tooltip takes
precedence over its parent's.

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

Plastic is a theme material, reached through the theme like every other look:
`theme_draw_ex(part, rect, state | CTRL_PLASTIC, &(theme_draw_opts_t){.color, .icon, .control_size, .round})`
for `THEME_PART_BUTTON`, `THEME_PART_TOOLBAR_BUTTON` and `THEME_PART_CARD`. Buttons opt in with
`WINDOW_PLASTIC` (and `WINDOW_ROUND` for a circular silhouette); toolbars with `TOOLBAR_STYLE_PLASTIC`.
`theme_content_rect(THEME_PART_CARD, rect, state | CTRL_PLASTIC)` returns the area left for content.
The material comes from `theme_t.plastic` (`gloss`, `rim`, `ink`, `lift`, `glyph_size`, `shadow_color`)
and reaches the shader as uniforms; the kernel entry `render_plastic_surface` takes a `plastic_look_t`
of floats and never sees `ctrl_state_t`. `draw_plastic_button` below is the framework painter behind it:
it draws a tinted plastic button and solid white SVG glyph in one shader pass. `color` is packed `0xAABBGGRR`; zero
uses `brAccent`. Icon names use the SVG cache at their final draw size, avoiding a second scaling
of the glyph mask. `control_size` scales the glyph by the theme's `control.size_pct`
(see the toolbar guide for the size system). Glyph placement and press offsets snap to device pixels, and
the glyph has no emboss, highlight halo or inner shadow. The shader adds a smooth
vertical gradient, directional button bevel and a small drop shadow.
Normal, hover, selected, pressed and disabled states share the same allocated
rectangle. Disabled takes precedence over interaction flags.

The shadow stays inside `rect`; no extra layer, framebuffer or neighbouring
repaint margin is needed. Theme metrics `plastic.corner_radius`,
`plastic.bevel_width` and `plastic.shadow_size` are logical pixels. Radius clamps
to half the face size, allowing rounded rectangles, capsules and circles;
`CORNER_RADIUS_CIRCULAR` (-1) asks for that full rounding at any size, like
AppKit's circular bezel. SDF
antialiasing uses screen derivatives to follow display density and transforms.

`draw_plastic_card(rect, state, color)` (also reached via `THEME_PART_CARD`) uses the same shader with the theme's
card radius and a shallow bevel, without a glyph. `card.shadow_size` reserves the
shadow margin inside the bounds; `theme_content_rect()` excludes that margin and
the selection ring. Waveforms and labels remain
ordinary content drawn over the procedural surface.

## Bitmaps and block transfers (`user/gdi.h`)

`bitmap_t` is the HBITMAP analogue: straight-alpha sRGB RGBA8 pixels, created with
`bitmap_create`, `bitmap_load` or `bitmap_load_memory` (PNG, JPEG, BMP including
RLE8). The GPU texture is created on the first blit and shared by every window.
`bitmap_set_color_key(bm, rgb)` makes one colour transparent, `bitmap_pixel`
reads a pixel.

Inside `evPaint`, `bit_blt(dst, bm, src_origin)` copies at 1:1, `stretch_blt(dst, bm, src)`
scales a bitmap rect to `dst`, and `tile_blt(dst, bm, src)` repeats it. Scaling is
nearest-neighbour. Source areas outside the bitmap are clipped and `dst` shrinks
by the same fraction, so a short sprite sheet leaves the rest untouched.

`user/zip.h` reads `.zip` / `.wsz` archives from memory, matching entries by file
name. `SpriteButton` and `SpriteSlider` (`commctl/sprite.c`) are the controls built
on these calls: sprites are bitmap rects stretched to the control's frame, so a
skin scales with the window. SpriteSlider answers the Slider messages
(`slSetRange`, `slSetPos`, `slGetPos`, `sliderValueChanged`) and adds `spsnReleased`.

`SpriteButton` accepts `SPRITE_BUTTON_DOUBLECLICK` to notify its parent with
`evCommand`, `HIWORD(wparam) = btnDoubleClicked`, `LOWORD(wparam) = control id`,
and `lparam = control`. It consumes the second release so it cannot also fire
`btnClicked`. Without this flag, ordinary click behavior is unchanged.

Theme settings are grouped in inline structs by component: `scrollbar`, `caption`,
`menubar`, `toolbar`, `control`, `button`, `window`, `card`, `plastic`, `drag`,
`tab`, `menu`, `badge`, `field`, and `tooltip`. Shadows live under `window.shadow`,
`drag.shadow`, and `tooltip.shadow`; shared control sizes use `control.size_pct`.

TabView header geometry uses `theme_t.tab.header.height`, `tab.header.start`,
`tab.min_width`, and `tab.padding`. Bundled themes use `TITLEBAR_HEIGHT` for the
horizontal header, a zero leading inset, an 80-pixel minimum tab width, and
16-pixel horizontal label padding. TabView clips labels to
the space before its header toolbar; the normal window paint pass draws its
selected content and toolbar children.

### Asymmetric content insets

`rect_inset_edges(rect, insets)` interprets the inset fields as left, top, right,
and bottom, and clamps exhausted width or height to zero. Layout containers and
window-managed scroll content share this utility for their padded content area.
