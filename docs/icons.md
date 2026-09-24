---
layout: default
title: Icon System
nav_order: 13
---

# Icon System

Orion's toolbar and system icons use **SVG files from [iconoir](https://iconoir.com)**
in `share/icons/`. The Form Editor Components grid uses the restored
`apps/formeditor/share/controls-icons-48.png` bitmap sheet. Both paths draw
through GPU textures; the icon grid receives a `bitmap_strip_t` and tile indices.

---

## Architecture

```
share/icons/*.svg
      │
      │  startup (svg_build_strip)
      ▼
nanosvg → RGBA pixel buffer → R_CreateTextureRGBA → GPU texture
      │
      ▼
bitmap_strip_t  { tex, icon_w, icon_h, cols, sheet_w, sheet_h }
      │
      ├─ g_sysicon_strip   (sysicon_* enum,  SYSICON_SIZE × SYSICON_SIZE, 32 cols)
      ├─ g_icons_strip     (icon_id_t enum,  16 × 16,                    16 cols)
      └─ g_tool_strip      (IE_ICONS enum,   24 × 24,                    16 cols)
             ↑ imageeditor only
```

`currentColor` in every SVG is patched to `white` before rasterizing, so icons
render as white-on-alpha and can be tinted at draw time by passing any `uint32_t`
color to `draw_icon16` / `draw_toolbar_icon_in_rect`.

---

## Icon strips

| Strip | Index enum | Tile size | Source | Loaded by |
|---|---|---|---|---|
| **sysicon** | `sysicon_*` in `orion/user/icons.h` | `SYSICON_SIZE` (24 px) | `share/icons/*.svg` | `orion/user/init.c` |
| **picker** | `icon_id_t` in `orion/user/sysicons.h` | 16 px | `share/icons/*.svg` | `orion/user/init.c` |
| **imageeditor tools** | `IE_ICONS` in `apps/imageeditor/image-editor.h` | 24 px | `share/icons/*.svg` | `apps/imageeditor/windows/win_toolpalette.c` |
| **Form Editor components** | `IC_*` in `apps/formeditor/controls-icons.h` | 48 px source, drawn at 24 px | `apps/formeditor/share/controls-icons-48.png` | `apps/formeditor/win_components.c` |

The SVG strips read from `share/icons/`. The Form Editor strip is an indexed PNG
sheet, so its 48 px tiles remain sharp at a 24 point size on Retina displays.

---

## SYSICON_SIZE and toolbar sizing

`SYSICON_SIZE = 24` is defined in `orion/user/messages.h` and drives the toolbar
button area:

```c
#define SYSICON_SIZE      24              // canonical SVG tile size
#define TOOLBAR_HEIGHT    (SYSICON_SIZE + 4)  // button = icon + 2 px each side
#define TB_SPACING        TOOLBAR_HEIGHT  // toolbar buttons are square
```

Changing `SYSICON_SIZE` automatically resizes toolbar buttons, vertical toolbars,
and the icon sheet tile size.  No other constants need touching.

---

## Using icons in code

### Toolbar buttons (`.orion` files — preferred)

```xml
<!-- reference icons by SVG base name; loaded on demand, no enum needed -->
<Button name="commit" command="commit.commit" icon="git-commit" text="Commit"/>
<Button name="undo"   command="commit.undo"   icon="undo"       text="Undo"  />
```

### Toolbar buttons (manual C arrays)

```c
static const toolbar_item_t k_items[] = {
    { TOOLBAR_ITEM_BUTTON, ID_DELETE, "trash",  0, 0, "Delete", NULL },
    { TOOLBAR_ITEM_BUTTON, ID_UNDO,   "undo",   0, 0, "Undo",   NULL },
};
```

The `icon` field is a `const char*` SVG base name.  `NULL` = no icon.  The
framework calls `sysicon_resolve()` which checks the preloaded strip first,
then loads the SVG from disk on first use.

### Owner-drawn code (integer sysicon IDs)

```c
// draw_icon16 still uses sysicon_* integer IDs from the preloaded strip
draw_icon16(sysicon_eye_show, x, y, 0xFFFFFFFF);   // white
draw_icon16(sysicon_warning,  x, y, 0xFF2244CC);   // tinted
draw_sysicon(sysicon_eye_show, x, y, 12, 0xFFFFFFFF); // explicitly sized
```

`draw_icon16` scales the source tile to 16×16 pixels. This keeps compact
owner-drawn controls independent of the canonical 24×24 toolbar tile size.
The tint color multiplies against the white-on-alpha icon (`0xFFFFFFFF` = white,
`0xFF0000FF` = red).

---

## Picking and adding icons

### 1. Find the iconoir name

Browse [iconoir.com](https://iconoir.com) or search the GitHub tree:

```bash
curl -s "https://api.github.com/repos/iconoir-icons/iconoir/git/trees/<SHA>" \
  | python3 -c "import sys,json; [print(i['path'].replace('.svg',''))
    for i in json.load(sys.stdin)['tree']
    if 'keyword' in i['path']]"
```

Iconoir uses **lowercase kebab-case** (`arrow-up`, `folder-open`, `fill-color`).
Some names you might guess wrong:

| What you'd guess | Real iconoir name |
|---|---|
| `brush` | `design-nib` |
| `eraser` | `erase` |
| `scissors` | `scissor` |
| `paint-bucket` | `fill-color` |
| `cursor` | `cursor-pointer` |
| `eye-off` | `eye-closed` |
| `pencil` | `edit-pencil` or `design-pencil` |
| `clipboard` | `paste-clipboard` |
| `fast-forward` | `forward` |
| `file-plus` | `page-plus` |
| `hand-gesture` | `drag-hand-gesture` |
| `music-note-beamed` | `music-double-note` |

### 2. Add the SVG file

```bash
# Download directly
curl -fsSo share/icons/my-icon.svg \
  https://raw.githubusercontent.com/iconoir-icons/iconoir/main/icons/regular/my-icon.svg
```

Or drop a custom 24 × 24 SVG (stroke-based, `currentColor` for the stroke) in
`share/icons/` and the system picks it up on next build.

### 3. Map it

**For a new sysicon** — add an entry to `k_sysicon_names[]` in
`orion/user/svg_icon_loader.c`:

```c
[sysicon_my_new_icon - SYSICON_BASE] = "my-icon",
```

Then add the enum value to `orion/user/icons.h`:

```c
sysicon_my_new_icon,    // inside the anonymous enum
```

**For an imageeditor tool** — add to `k_tool_svg_names[]` in
`apps/imageeditor/windows/win_toolpalette.c` and add to `IE_ICONS` in
`apps/imageeditor/image-editor.h`.

### 4. Copy to build

```bash
make share      # copies share/ → build/share/orion/
```

Icons are loaded at next run; no recompile needed unless you added an enum value.

---

## Download script

`tools/download_iconoir.sh` fetches all icons referenced by the current mappings
in one shot:

```bash
./tools/download_iconoir.sh               # → share/icons/
./tools/download_iconoir.sh /other/path   # → custom directory
```

After downloading, run `make share` to copy into the build tree.

Any iconoir name that returns HTTP 404 is printed as `MISSING: <name>` — drop a
hand-crafted SVG with that filename to fill the gap.

---

## NULL mappings and startup diagnostics

Icons with no iconoir equivalent are mapped to `NULL` in the C arrays.  At
startup you will see:

```
UNMAPPED icon[N]      ← enum entry is NULL in the mapping array
MISSING icon[N] "x"   ← mapped to "x" but share/icons/x.svg not found
```

`UNMAPPED` lines for game-engine-specific sysicons (`sysicon_voxel`,
`sysicon_sword`, etc.) are expected and harmless.  `MISSING` lines mean a named
SVG isn't on disk — run the download script or add a custom file.

If **every** icon in a strip is missing or unmapped, `svg_build_strip` returns
`false` and the strip stays empty (icons render as blank tiles — no crash).

---

## Toolbar icons — string-based, no enum needed

Toolbar buttons reference icons by **SVG base name** (a `const char*`), not a
sysicon integer.  The icon is resolved on demand the first time a button is drawn:

1. The preloaded sysicon strip is scanned for the name.
2. If not found, each registered icons directory is tried in order.
3. The first matching `<name>.svg` is rasterized and cached as a GPU texture.

**Icon directory pools** (searched in registration order):

| Directory (source) | Runtime path | Registered by |
|---|---|---|
| `share/icons/` | `share/orion/icons/` | `init_sysicon_strip` (automatic) |
| `apps/<app>/share/icons/` | `share/<app>/icons/` | app `gem_init` via `svg_add_icons_dir()` |

Icons in an app's own pool are found without polluting the global namespace:
`"git-commit"` resolves from `share/gitclient/icons/` while `"floppy-disk"`
comes from `share/orion/icons/`.

**Adding a new icon to gitclient (or any app):**

```xml
<!-- gitclient.orion — just use the SVG base name -->
<Button name="commit" command="commit.commit" icon="git-commit" text="Commit" />
```

1. Drop `git-commit.svg` in `apps/gitclient/share/icons/`.
2. Run `make share` (copies it to `build/share/gitclient/icons/`).
3. Launch — no recompile needed.

No C enum entry is needed.

For owner-drawn code that uses `draw_icon16()` you still need a `sysicon_*`
enum entry and a `k_sysicon_names[]` mapping — that path uses integer strip
indices.

---

## Adding custom icons not in iconoir

Drop any hand-crafted SVG in `share/icons/<name>.svg`.  Strict requirements to
ensure the icon looks correct at every size:

| Property | Value |
|---|---|
| `viewBox` | `0 0 24 24` (square, 24 × 24 units) |
| `width` / `height` | `24` (matches viewBox) |
| `fill` | `none` on root element |
| `stroke` | `currentColor` (loader replaces with `white` at rasterise time) |
| `stroke-width` | `1.5` (iconoir standard) |
| `stroke-linecap` | `round` |
| `stroke-linejoin` | `round` |
| Allowed elements | `<path>`, `<line>`, `<rect>`, `<circle>`, `<polyline>` only |
| Forbidden | fills, gradients, masks, `<text>`, raster images, editor metadata |
| Legibility | must read clearly at 16 × 16 actual display size |

The loader centers and scales the SVG to the target tile size automatically.

### Stroke-width note

Iconoir uses exactly **`stroke-width="1.5"`**.  Do not use `2` — it produces
heavier marks than the surrounding iconoir icons and breaks visual consistency.

---

## Drawing new icons with an AI model

When an icon does not exist in iconoir and must be custom-drawn, use this prompt
template to generate a compliant SVG:

> Create a 24 × 24 monochrome SVG icon for **"[describe the concept]"**.
> Match iconoir's outline style: `stroke-width="1.5"`, `stroke="currentColor"`,
> `stroke-linecap="round"`, `stroke-linejoin="round"`, `fill="none"`.
> Use only `<path>`, `<circle>`, `<rect>`, `<line>`, or `<polyline>`.
> The icon must remain legible at 16 × 16.
> No fills, gradients, masks, text nodes, or raster images.
> Return only the valid SVG — no explanation, no wrapper HTML.

**Example** (git commit node — circle on a horizontal line):

```svg
<svg width="24" height="24" viewBox="0 0 24 24"
     stroke-width="1.5" fill="none" xmlns="http://www.w3.org/2000/svg">
  <circle cx="12" cy="12" r="3.5" stroke="currentColor"
          stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M2 12H8.5" stroke="currentColor" stroke-linecap="round"/>
  <path d="M15.5 12H22" stroke="currentColor" stroke-linecap="round"/>
</svg>
```

After generating:
1. Verify `stroke-width="1.5"` is on the root `<svg>` (not per-path).
2. Confirm no `fill` values other than `none`, no `style=` attributes.
3. Open in a browser and check readability at 16 × 16 by scaling the viewport.
4. Save to `share/icons/<name>.svg`, run `make share`, launch the app.

---

## File locations

| Path | Purpose |
|---|---|
| `share/icons/*.svg` | Global icon pool (orion system icons) |
| `apps/<name>/share/icons/*.svg` | App-specific icon pool (loaded via `svg_add_icons_dir`) |
| `orion/user/svg_icon_loader.h/.c` | Strip builder, sysicon/picker mappings, `sysicon_resolve()`, `svg_add_icons_dir()` |
| `orion/user/icons.h` | `sysicon_*` enum and `SYSICON_BASE` (owner-drawn code only) |
| `orion/user/sysicons.h` | `icon_id_t` enum for file-picker icons |
| `orion/user/messages.h` | `SYSICON_SIZE`, `TOOLBAR_HEIGHT`, `TB_SPACING`; `toolbar_item_t` |
| `orion/user/draw_impl.c` | `draw_icon16`, `draw_icon`, `draw_theme_icon` |
| `orion/user/init.c` | `init_sysicon_strip`, `init_icons_strip`, `svg_set_icons_dir()` |
| `apps/imageeditor/image-editor.h` | `IE_ICONS` enum |
| `apps/imageeditor/windows/win_toolpalette.c` | Imageeditor tool mapping + strip load |
| `tools/download_iconoir.sh` | Bulk SVG download script |
| `tools/nanosvg.h` / `tools/nanosvgrast.h` | Bundled nanosvg rasterizer |
