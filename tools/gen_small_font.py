#!/usr/bin/env python3
"""
gen_small_font.py — generate share/Geneva9.png for Orion.

Geneva9.png is a composite 128×128 greyscale font atlas:
  Chars   0–127: Silkscreen-Regular (OFL-1.1) rasterised at 8×8 cells.
                 Silkscreen is a clean proportional pixel font that closely
                 approximates the Geneva 9 look from Mac System 1–7.
                 The exact Geneva 9 clone at fontstruct.com/fontstructions/show/349277
                 carries an "All Rights Reserved" licence and cannot be
                 distributed; Silkscreen is used as a freely-licensed stand-in.
  Chars 128–255: Original SmallFont.png icon glyphs (bottom half preserved).
                 These are the 8×8 UI icons used by draw_icon8() / draw_icon16().

The file is loaded at runtime as the FONT_SMALL atlas (Geneva9 / SmallFont).

Usage:
    python3 tools/gen_small_font.py [silkscreen.ttf [SmallFont.png [Geneva9.png [font_atlas_bin]]]]

All arguments are optional; defaults are resolved relative to the repository root.
The font_atlas binary must already be compiled.  Build it with:
    gcc -std=c11 -O2 -Itools tools/font_atlas.c -lm -o /tmp/font_atlas

Dependencies: Pillow (pip install Pillow)
"""

import sys
import os
import subprocess
import tempfile


def main():
    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))

    silkscreen_ttf = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, 'fonts', 'Silkscreen-Regular.ttf')
    smallfont_png  = sys.argv[2] if len(sys.argv) > 2 else os.path.join(repo, 'share', 'SmallFont.png')
    output_png     = sys.argv[3] if len(sys.argv) > 3 else os.path.join(repo, 'share', 'Geneva9.png')
    font_atlas_bin = sys.argv[4] if len(sys.argv) > 4 else os.path.join(repo, 'build', 'bin', 'font_atlas')

    # Locate font_atlas binary if the default doesn't exist yet.
    if not os.path.exists(font_atlas_bin):
        candidates = [
            os.path.join(repo, 'build', 'bin', 'font_atlas'),
            '/tmp/font_atlas',
            '/tmp/fonttools/font_atlas',
        ]
        for c in candidates:
            if os.path.exists(c):
                font_atlas_bin = c
                break
        else:
            sys.exit(
                'ERROR: font_atlas binary not found. Build it first:\n'
                '  gcc -std=c11 -O2 -Itools tools/font_atlas.c -lm -o /tmp/font_atlas'
            )

    for path, label in [(silkscreen_ttf, 'Silkscreen TTF'),
                        (smallfont_png,  'SmallFont.png')]:
        if not os.path.exists(path):
            sys.exit(f'ERROR: {label} not found: {path}')

    try:
        from PIL import Image
    except ImportError:
        sys.exit('ERROR: Pillow is required.  Install with:  pip install Pillow')

    # ── Step 1: rasterise Silkscreen into a temp 128×128 greyscale atlas ──────
    with tempfile.NamedTemporaryFile(suffix='.png', delete=False) as tf:
        temp_atlas = tf.name
    try:
        result = subprocess.run(
            [font_atlas_bin, silkscreen_ttf, temp_atlas,
             '-pixelsize=8', '-em', '-sharp', '-cellw=8', '-cellh=8'],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            sys.exit(f'ERROR: font_atlas failed:\n{result.stderr}')

        silk = Image.open(temp_atlas).convert('L')
    finally:
        try:
            os.unlink(temp_atlas)
        except OSError:
            pass

    if silk.size != (128, 128):
        sys.exit(f'ERROR: expected Silkscreen atlas 128×128, got {silk.size}')

    # ── Step 2: extract icon chars (rows 8–15, y=64..127) from SmallFont ──────
    small = Image.open(smallfont_png).convert('RGBA')
    if small.size != (128, 128):
        sys.exit(f'ERROR: expected SmallFont 128×128, got {small.size}')
    # The R channel carries glyph brightness (white glyphs on black background).
    r_channel, _, _, _ = small.split()
    icons_half = r_channel.crop((0, 64, 128, 128))   # chars 128–255

    # ── Step 3: composite and write ───────────────────────────────────────────
    out = Image.new('L', (128, 128), 0)
    out.paste(silk.crop((0, 0, 128, 64)), (0, 0))    # Silkscreen chars 0–127
    out.paste(icons_half,                 (0, 64))   # SmallFont icons 128–255

    os.makedirs(os.path.dirname(output_png), exist_ok=True)
    out.save(output_png)
    print(f'Wrote {output_png}  (128×128 greyscale, Silkscreen text + SmallFont icons)')


if __name__ == '__main__':
    main()
