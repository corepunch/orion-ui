#!/usr/bin/env python3
"""Render the universal App Store header and search asset (Pillow)."""
import math
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parent
WIDTH, HEIGHT = 5244, 2950
PALETTE = ("#ffd956", "#ff8557", "#ff61a4", "#54e396", "#59d3ff", "#a77aff")


def backdrop():
    im = Image.new("RGB", (656, 369))
    pixels = []
    for y in range(im.height):
        for x in range(im.width):
            u, v = x / im.width, y / im.height
            blue = math.exp(-((u - .39)**2 / .16 + (v - .36)**2 / .30))
            pink = math.exp(-((u - .90)**2 / .11 + (v - .11)**2 / .25))
            pixels.append((int(12 + 21*blue + 24*pink),
                           int(10 + 20*blue + 3*pink),
                           int(38 + 68*blue + 34*pink)))
    im.putdata(pixels)
    return im.resize((WIDTH, HEIGHT), Image.Resampling.BICUBIC)


def arrangement(canvas):
    ink = Image.new("RGBA", canvas.size)
    draw = ImageDraw.Draw(ink)
    step, track = 205, 128
    left, top = 161, 968
    for beat in range(25):
        x = left + beat * step
        draw.line((x, top, x, top + 8*track),
                  fill=(172, 163, 249, 31 if beat % 4 == 0 else 13), width=3)
    for row in range(9):
        y = top + row * track
        draw.line((left, y, left + 24*step, y), fill=(172, 163, 249, 20), width=3)

    clips = (
        (0, 0, 2, 4), (0, 4, 2, 5), (1, 1, 4, 2), (2, 0, 3, 1),
        (4, 1, 3, 5), (5, 3, 2, 3), (6, 0, 2, 2), (7, 2, 3, 0),
        (0, 18, 3, 0), (0, 22, 2, 1), (1, 21, 3, 4), (2, 20, 4, 2),
        (3, 22, 2, 5), (5, 20, 4, 4), (6, 18, 3, 3), (7, 21, 3, 2),
    )
    for row, beat, length, color in clips:
        x, y = left + beat*step + 8, top + row*track + 12
        right, bottom = x + length*step - 17, y + track - 24
        rgb = bytes.fromhex(PALETTE[color][1:])
        draw.rounded_rectangle((x, y, right, bottom), 20, fill=(*rgb, 52),
                               outline=(*rgb, 103), width=3)
        for i in range(12, right - x - 13, 11):
            pulse = abs(math.sin(i*.058 + row) * math.cos(i*.023 + beat))
            height = 5 + int(32*pulse)
            center = (y + bottom) // 2
            draw.line((x+i, center-height, x+i, center+height), fill=(*rgb, 93), width=4)
    canvas.paste(ink, mask=ink.getchannel("A"))


def main():
    canvas = backdrop()
    arrangement(canvas)
    with Image.open(ROOT.parent / "share/branding/groove.png") as source:
        brand = source.convert("RGBA")
    brand = brand.resize((2710, round(2710 * brand.height / brand.width)),
                         Image.Resampling.LANCZOS)
    canvas.paste(brand, ((WIDTH - brand.width)//2, (HEIGHT - brand.height)//2), brand)
    out = ROOT / "creative"
    out.mkdir(exist_ok=True)
    canvas.save(out / "groove-universal.png", optimize=True)


if __name__ == "__main__":
    main()
