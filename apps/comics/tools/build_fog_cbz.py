#!/usr/bin/env python3
"""Build the lettered issue and its interactive artwork into one CBZ."""
from functools import lru_cache
from io import BytesIO
from pathlib import Path
from zipfile import ZIP_STORED, ZipFile
from xml.etree import ElementTree

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps

APP = Path(__file__).resolve().parents[1]
ART = APP / "art/fog-comic"
TARGET = APP / "share/books/the-boat-in-the-fog.cbz"
FACE = ImageFont.truetype(str(APP.parents[1] / "share/fonts/BalsamiqSans-Bold.ttf"), 28)
PAPER = "#fff7e4"
INK = "#29241e"
PAGE = (900, 1320)
GUTTER = 7
STORY = ElementTree.parse(ART / "story.xml").getroot()
BUBBLE_ANCHORS = {"dock": 66, "lesson": 33, "point": 72, "lantern": 60, "sail": 78, "harbor": 30}
NEW_SHEETS = ("moorings", "lantern-walk", "search", "return", "home")
AUTHORED_INK = {"lantern-walk-2", "lantern-walk-5", "search-2", "search-5", "home-2", "home-5"}
PREVIEW_INK = "preview-ink.png"
PREVIEW_INK_CELLS = ("dock", "knots", "point", "rowing")


# Where each shot's artwork lies: a cell of a 3 x 2 sheet, or the whole harbor plate. `top` keeps
# the top of a shot in view when its panel is wider than the cell. Matches fog_shot_art in the app.
SHOTS = {
    "boast": ("inserts.jpg", 0, True), "dock": ("panels.jpg", 0, False),
    "lesson": ("panels.jpg", 1, True), "knots": ("inserts.jpg", 1, False),
    "hut": ("inserts.jpg", 2, False), "point": ("panels.jpg", 2, False),
    "lantern": ("inserts.jpg", 3, True), "harbor": ("harbor.jpg", None, False),
    "rowing": ("inserts.jpg", 5, True), "sail": ("panels.jpg", 4, False),
}
for sheet in NEW_SHEETS:
    for cell in range(6):
        SHOTS[f"{sheet}-{cell + 1}"] = (f"{sheet}.jpg", cell, False)


@lru_cache
def shot(name):
    sheet, cell, _ = SHOTS[name]
    art = Image.open(ART / sheet).convert("RGB")
    if cell is not None:
        # The same hairline inset as fog_cell_uv, so the ink lines up with the reader's layer.
        u, v = cell % 3 / 3, cell // 3 / 2
        art = art.crop((round((u + .002) * art.width), round((v + .003) * art.height),
                        round((u + .331) * art.width), round((v + .497) * art.height)))
    return art


def percentile(image, q):
    histogram = image.histogram()
    total, seen = sum(histogram), 0
    for value, count in enumerate(histogram):
        seen += count
        if seen >= total * q:
            return value
    return 255


@lru_cache
def lineart(name):
    art = shot(name)
    if name in PREVIEW_INK_CELLS:
        cell = PREVIEW_INK_CELLS.index(name)
        with Image.open(ART / PREVIEW_INK) as sheet:
            half = sheet.width // 2
            x, y = cell % 2 * half, cell // 2 * half
            art = sheet.crop((x + 3, y + 3, x + half - 3, y + half - 3))
        art = art.resize(shot(name).size, Image.Resampling.LANCZOS)
    gray = ImageOps.grayscale(art)
    if name in AUTHORED_INK or name in PREVIEW_INK_CELLS:
        # Lift the authored contours off their original paper for the live wash layer.
        alpha = gray.point(lambda v: max(0, min(255, (218 - v) * 255 // 178)))
    else:
        # Derive ink from a painted shot if the panel has no authored outlines.
        gray = ImageOps.autocontrast(gray, cutoff=1)
        dark = ImageChops.subtract(gray.filter(ImageFilter.GaussianBlur(4)), gray)
        low = max(14, percentile(dark, .87))
        high = max(low + 8, percentile(dark, .9675))
        alpha = dark.point(lambda v: 0 if v < low else min(255, (v - low) * 255 // (high - low)))
        alpha = alpha.filter(ImageFilter.MedianFilter(3))
    ink = Image.new("RGBA", art.size, INK)
    ink.putalpha(alpha)
    return ink


def coloring(name):
    ink = lineart(name)
    page = Image.new("RGB", ink.size, PAPER)
    page.paste(ink, mask=ink)
    return page


def frame(page, image, box, top=False):
    x, y, w, h = box
    photo = ImageOps.fit(image, (w, h), centering=(.5, 0 if top else .5))
    page.paste(photo, (x, y))


def percent(panel, name, default):
    value = panel.get(name, "")
    if value.isascii() and value.isdecimal():
        digits = value.lstrip("0") or "0"
        if len(digits) <= 3 and int(digits) <= 100:
            return int(digits)
    return default


def balloon(page, box, panel):
    x, y, w, h = box
    shot_name = panel.attrib["shot"]
    edge = panel.get("bubble-edge")
    at_bottom = shot_name != "harbor" if edge not in ("top", "bottom") else edge == "bottom"
    anchor = percent(panel, "bubble-x", BUBBLE_ANCHORS.get(shot_name, 50))
    text = panel.findtext("text[@lang='en']")
    pen = ImageDraw.Draw(page)
    lines = []
    for word in text.split():
        merged = f"{lines[-1]} {word}" if lines else word
        if lines and pen.textlength(merged, font=FACE) < w - 72:
            lines[-1] = merged
        else:
            lines.append(word)
    text = "\n".join(lines)
    bounds = pen.multiline_textbbox((0, 0), text, font=FACE, spacing=5)
    width = min(w - 28, bounds[2] + 36)
    height = bounds[3] + 31
    aim = x + w * anchor // 100
    left = max(x + 14, min(aim - width // 2, x + w - width - 14))
    min_y, max_y = y + 15, y + h - height - 30
    travel = max(0, (max_y - min_y) // 2)
    if shot_name == "harbor":
        travel = max(0, (h - 45) // 2 - height)
    inset = travel * percent(panel, "bubble-inset", 0) // 100
    top = max_y - inset if at_bottom else min_y + inset
    bottom = top + height
    aim = max(left + 24, min(aim, left + width - 24))
    edge, direction = (top, -1) if at_bottom else (bottom, 1)
    pen.polygon([(aim - 12, edge - 2 * direction), (aim + 10, edge - 2 * direction),
                 (aim + 2, edge + 17 * direction)], fill=PAPER, outline=INK, width=2)
    pen.rounded_rectangle((left, top, left + width, bottom), 17, fill=PAPER, outline=INK, width=2)
    pen.line((aim - 10, edge, aim + 8, edge), fill=PAPER, width=3)
    pen.multiline_text((left + 18, top + 11), text, font=FACE, fill=INK, spacing=5)


def boats(page, box):
    x, y, w, h = box
    boat = Image.open(ART / "rowboat.png").convert("RGBA")
    boat = boat.crop(boat.getchannel("A").getbbox())
    boat.thumbnail((w // 3 - 40, h // 6), Image.Resampling.LANCZOS)
    for i in range(3):
        page.paste(boat, (x + 20 + i * w // 3, y + h * 3 // 5), boat)


def cover():
    with Image.open(ART / "cover.jpg") as art:
        page = ImageOps.fit(art.convert("RGB"), PAGE)
    pen = ImageDraw.Draw(page)
    pen.text((PAGE[0] // 2, 105), "The Boat", font=FACE.font_variant(size=134),
             fill=PAPER, stroke_width=1, stroke_fill=INK, anchor="mt")
    pen.text((PAGE[0] // 2, 248), "in the Fog", font=FACE.font_variant(size=118),
             fill=PAPER, stroke_width=1, stroke_fill=INK, anchor="mt")
    return page


def is_paintable(panel):
    return panel.get("paint") == "true"


def pages(story_pages):
    """The cover, then each page with one wide panel above any lower panels."""
    yield cover()
    w, h = PAGE
    for node in story_pages:
        page = Image.new("RGB", PAGE, PAPER)
        panels = node.findall("panel")
        row = (h - GUTTER) // 2 if len(panels) > 1 else h
        boxes = [(0, 0, w, row)]
        lower = panels[1:]
        for i in range(len(lower)):
            left = i * (w + GUTTER) // len(lower)
            right = (i + 1) * (w + GUTTER) // len(lower) - GUTTER
            boxes.append((left, row + GUTTER, right - left, h - row - GUTTER))
        for panel, box in zip(panels, boxes):
            name = panel.attrib["shot"]
            art = coloring(name) if is_paintable(panel) else shot(name)
            frame(page, art, box, SHOTS[name][2])
            if name == "harbor":
                boats(page, box)
            if panel.find("text") is not None:
                balloon(page, box, panel)
        yield page


def main():
    story_pages = STORY.findall("page")
    if not len(story_pages) or len(story_pages) % 2 or STORY[0].tag != "cover":
        raise ValueError("Expected a cover and pairs of facing story pages")
    for i in range(0, len(story_pages), 2):
        if sum(is_paintable(panel) for page in story_pages[i:i + 2] for panel in page.findall("panel")) != 1:
            raise ValueError(f"Spread {i // 2 + 1} must have exactly one coloring panel")
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(TARGET, "w", ZIP_STORED) as cbz:
        for i, page in enumerate(pages(story_pages), 1):
            data = BytesIO()
            page.save(data, "JPEG", quality=92, optimize=True)
            cbz.writestr(f"{i:02}.jpg", data.getvalue())
        cbz.writestr("ComicInfo.xml", '<?xml version="1.0" encoding="UTF-8"?>\n'
                     f'<ComicInfo><Title>The Boat in the Fog</Title><PageCount>{len(story_pages) + 1}</PageCount>'
                     '<LanguageISO>en</LanguageISO></ComicInfo>\n')
        cbz.write(ART / "story.xml", "orion/story.xml")
        for asset in sorted(ART.iterdir()):
            if asset.suffix in (".jpg", ".png") and asset.name != PREVIEW_INK:
                cbz.write(asset, f"orion/{asset.stem}.art")
        painted = sorted({panel.attrib["shot"] for panel in STORY.iter("panel") if is_paintable(panel)})
        for name in painted:
            data = BytesIO()
            lineart(name).save(data, "PNG", optimize=True)
            cbz.writestr(f"orion/lineart/{name}.art", data.getvalue())


if __name__ == "__main__":
    main()
