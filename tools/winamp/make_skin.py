#!/usr/bin/env python3
"""Draw Winamp's default look-alike skin as original pixel art.

Every bitmap uses the classic Winamp 2.x skin atlas layout (main.bmp,
cbuttons.bmp, titlebar.bmp, ...), so the player also loads any real .wsz skin.
The artwork itself is drawn here from rectangles and a small pixel font.

    python3 tools/winamp/make_skin.py apps/winamp/share/skin

The app icon is written next to the skin folder (share/icon.png).
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw

BLACK = (0, 0, 0)
FACE = (52, 52, 76)
FACE_HI = (92, 92, 124)
FACE_LO = (22, 22, 34)
FACE_MID = (40, 40, 60)
LCD = (0, 0, 0)
LCD_EDGE = (18, 18, 28)
GREEN = (0, 232, 0)
GREEN_DIM = (0, 72, 0)
GOLD = (220, 196, 96)
TEXT_FG = (0, 226, 0)
LABEL = (160, 160, 196)
BTN = (74, 74, 104)
BTN_HI = (140, 140, 176)
BTN_LO = (28, 28, 42)
BTN_DOWN = (44, 44, 64)
ICON = (196, 196, 220)

# 4x5 glyphs in 5x6 cells, '#' = lit. Layout matches TEXT.BMP's three rows.
GLYPHS = {
    'A': ".##.|#..#|####|#..#|#..#", 'B': "###.|#..#|###.|#..#|###.", 'C': ".###|#...|#...|#...|.###",
    'D': "###.|#..#|#..#|#..#|###.", 'E': "####|#...|###.|#...|####", 'F': "####|#...|###.|#...|#...",
    'G': ".###|#...|#.##|#..#|.###", 'H': "#..#|#..#|####|#..#|#..#", 'I': "###.|.#..|.#..|.#..|###.",
    'J': "..##|...#|...#|#..#|.##.", 'K': "#..#|#.#.|##..|#.#.|#..#", 'L': "#...|#...|#...|#...|####",
    'M': "#..#|####|####|#..#|#..#", 'N': "#..#|##.#|#.##|#..#|#..#", 'O': ".##.|#..#|#..#|#..#|.##.",
    'P': "###.|#..#|###.|#...|#...", 'Q': ".##.|#..#|#..#|#.#.|.#.#", 'R': "###.|#..#|###.|#.#.|#..#",
    'S': ".###|#...|.##.|...#|###.", 'T': "###.|.#..|.#..|.#..|.#..", 'U': "#..#|#..#|#..#|#..#|.##.",
    'V': "#..#|#..#|#..#|.##.|.##.", 'W': "#..#|#..#|####|####|#..#", 'X': "#..#|.##.|.##.|.##.|#..#",
    'Y': "#.#.|#.#.|.#..|.#..|.#..", 'Z': "####|..#.|.#..|#...|####", '"': "#.#.|#.#.|....|....|....",
    '@': ".##.|#.##|#.##|#...|.###", ' ': "....|....|....|....|....",
    '0': ".##.|#.##|##.#|#..#|.##.", '1': ".#..|##..|.#..|.#..|###.", '2': "###.|...#|.##.|#...|####",
    '3': "###.|...#|.##.|...#|###.", '4': "#..#|#..#|####|...#|...#", '5': "####|#...|###.|...#|###.",
    '6': ".##.|#...|###.|#..#|.##.", '7': "####|...#|..#.|.#..|.#..", '8': ".##.|#..#|.##.|#..#|.##.",
    '9': ".##.|#..#|.###|...#|.##.", '…': "....|....|....|....|#.#.", '.': "....|....|....|....|.#..",
    ':': "....|.#..|....|.#..|....", '(': "..#.|.#..|.#..|.#..|..#.", ')': ".#..|..#.|..#.|..#.|.#..",
    '-': "....|....|###.|....|....", "'": ".#..|.#..|....|....|....", '!': ".#..|.#..|.#..|....|.#..",
    '_': "....|....|....|....|####", '+': "....|.#..|###.|.#..|....", '\\': "#...|.#..|.#..|..#.|..#.",
    '/': "..#.|..#.|.#..|.#..|#...", '[': ".##.|.#..|.#..|.#..|.##.", ']': ".##.|..#.|..#.|..#.|.##.",
    '^': ".#..|#.#.|....|....|....", '&': ".#..|#.#.|.#..|#.#.|.#.#", '%': "#..#|..#.|.#..|#...|#..#",
    ',': "....|....|....|.#..|#...", '=': "....|###.|....|###.|....", '$': ".###|##..|.##.|..##|###.",
    '#': ".#.#|####|.#.#|####|.#.#", 'Å': ".##.|....|####|#..#|#..#", 'Ö': "#..#|.##.|#..#|#..#|.##.",
    'Ä': "#..#|.##.|#..#|####|#..#", '?': "###.|...#|.##.|....|.#..", '*': "....|#.#.|.#..|#.#.|....",
}
ROWS = ["ABCDEFGHIJKLMNOPQRSTUVWXYZ\"@   ", "0123456789….:()-'!_+\\/[]^&%,=$#", "ÅÖÄ?*"]


def img(w, h, color=BLACK):
    return Image.new('RGB', (w, h), color)


def rect(d, x, y, w, h, color):
    if w > 0 and h > 0:
        d.rectangle([x, y, x + w - 1, y + h - 1], fill=color)


def bevel(d, x, y, w, h, fill, hi, lo):
    rect(d, x, y, w, h, fill)
    rect(d, x, y, w, 1, hi)
    rect(d, x, y, 1, h, hi)
    rect(d, x, y + h - 1, w, 1, lo)
    rect(d, x + w - 1, y, 1, h, lo)


def inset(d, x, y, w, h, fill=LCD):
    bevel(d, x, y, w, h, fill, FACE_LO, FACE_HI)


def text(d, s, x, y, color):
    for ch in s.upper():
        rows = GLYPHS.get(ch, GLYPHS[' ']).split('|')
        for gy, row in enumerate(rows):
            for gx, c in enumerate(row):
                if c == '#':
                    d.point((x + gx, y + gy), fill=color)
        x += 5
    return x


def text_width(s):
    return 5 * len(s) - 1


def pattern(d, x, y, rows, color):
    for gy, row in enumerate(rows):
        for gx, c in enumerate(row):
            if c == '#':
                d.point((x + gx, y + gy), fill=color)


def face(d, w, h, x=0, y0=0):
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(a + (b - a) * t) for a, b in zip((60, 60, 88), (42, 42, 64)))
        rect(d, x, y0 + y, w, 1, c)
    rect(d, x, y0, w, 1, FACE_HI)
    rect(d, x, y0, 1, h, FACE_HI)
    rect(d, x, y0 + h - 1, w, 1, FACE_LO)


def title_bar(d, x, y, w, label, active):
    top, bottom = ((88, 88, 140), (44, 44, 84)) if active else ((64, 64, 84), (40, 40, 54))
    for i in range(14):
        t = i / 13
        rect(d, x, y + i, w, 1, tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    rect(d, x, y, w, 1, (150, 150, 200) if active else (100, 100, 120))
    rect(d, x, y + 13, w, 1, FACE_LO)
    tw = text_width(label)
    tx = x + (w - tw) // 2
    grip = (190, 170, 90) if active else (110, 110, 130)
    grip_lo = (90, 70, 30) if active else (50, 50, 60)
    for gx0, gx1 in ((x + 18, tx - 6), (tx + tw + 6, x + w - 36)):
        for gy in (y + 4, y + 7, y + 10):
            rect(d, gx0, gy - 1, gx1 - gx0, 1, grip)
            rect(d, gx0, gy, gx1 - gx0, 1, grip_lo)
    text(d, label, tx, y + 4, GOLD if active else (150, 150, 170))


def small_button(d, x, y, glyph, pressed):
    bevel(d, x, y, 9, 9, BTN_DOWN if pressed else BTN, BTN_LO if pressed else BTN_HI, BTN_HI if pressed else BTN_LO)
    pattern(d, x + 2 + pressed, y + 2 + pressed, glyph, ICON)


CLOSE = ["#...#", ".#.#.", "..#..", ".#.#.", "#...#"]
MINIMIZE = [".....", ".....", ".....", ".....", "#####"]
SHADE = ["#####", "#...#", "#####", ".....", "....."]
OPTIONS = ["#####", "#####", ".....", ".....", "....."]


def make_titlebar(out):
    im = img(344, 87, FACE)
    d = ImageDraw.Draw(im)
    title_bar(d, 27, 0, 275, 'WINAMP', True)
    title_bar(d, 27, 15, 275, 'WINAMP', False)
    for i, (glyph, px, py) in enumerate(((OPTIONS, 0, 0), (MINIMIZE, 9, 0), (CLOSE, 18, 0))):
        small_button(d, px, py, glyph, False)
        small_button(d, px, py + 9, glyph, True)
    small_button(d, 0, 18, SHADE, False)
    small_button(d, 9, 18, SHADE, True)
    # Shade mode strips (unused by this player, present for atlas compatibility).
    title_bar(d, 27, 29, 275, 'WINAMP', True)
    title_bar(d, 27, 42, 275, 'WINAMP', False)
    for x, enabled in ((304, True), (312, False)):
        bevel(d, x, 0, 8, 43, FACE_MID, FACE_LO, FACE_HI)
        for i, ch in enumerate('OAIDV'):
            text(d, ch, x + 2, 3 + i * 8, LABEL if enabled else (70, 70, 90))
    im.save(out / 'titlebar.bmp')


def transport_icon(d, x, y, w, h, kind, pressed):
    bevel(d, x, y, w, h, BTN_DOWN if pressed else BTN, BTN_LO if pressed else BTN_HI, BTN_HI if pressed else BTN_LO)
    rect(d, x + 1, y + 1, w - 2, 1, (110, 110, 146) if not pressed else BTN_DOWN)
    cx, cy = x + w // 2 + pressed, y + h // 2 + pressed
    icons = {
        'prev': ["#....#...#", "#...##..##", "#..###.###", "#.########", "#..###.###", "#...##..##", "#....#...#"],
        'play': ["#.....", "###...", "#####.", "######", "#####.", "###...", "#....."],
        'pause': ["##.##", "##.##", "##.##", "##.##", "##.##", "##.##", "##.##"],
        'stop': ["#######", "#######", "#######", "#######", "#######", "#######", "#######"],
        'next': ["#...#....#", "##..##...#", "###.###..#", "########.#", "###.###..#", "##..##...#", "#...#....#"],
        'eject': ["...#...", "..###..", ".#####.", "#######", ".......", "#######", "#######"],
    }
    glyph = icons[kind]
    pattern(d, cx - len(glyph[0]) // 2, cy - len(glyph) // 2, glyph, ICON)


def make_cbuttons(out):
    im = img(136, 36, FACE)
    d = ImageDraw.Draw(im)
    for kind, x, w in (('prev', 0, 23), ('play', 23, 23), ('pause', 46, 23), ('stop', 69, 23), ('next', 92, 22)):
        transport_icon(d, x, 0, w, 18, kind, 0)
        transport_icon(d, x, 18, w, 18, kind, 1)
    transport_icon(d, 114, 0, 22, 16, 'eject', 0)
    transport_icon(d, 114, 16, 22, 16, 'eject', 1)
    im.save(out / 'cbuttons.bmp')


def toggle(d, x, y, w, h, label, lit, pressed, led=True):
    bevel(d, x, y, w, h, BTN_DOWN if pressed else BTN, BTN_LO if pressed else BTN_HI, BTN_HI if pressed else BTN_LO)
    tx = x + (w - text_width(label)) // 2 + (2 if led else 0) + pressed
    ty = y + (h - 5) // 2 + pressed
    if led:
        rect(d, x + 3 + pressed, ty + 1, 3, 3, GREEN if lit else GREEN_DIM)
    text(d, label, tx, ty, ICON if not lit or led else GREEN)


def make_shufrep(out):
    im = img(92, 85, FACE)
    d = ImageDraw.Draw(im)
    for row, (lit, pressed) in enumerate(((0, 0), (0, 1), (1, 0), (1, 1))):
        toggle(d, 0, row * 15, 28, 15, 'REP', lit, pressed)
        toggle(d, 28, row * 15, 47, 15, 'SHUFFLE', lit, pressed)
    for x, label in ((0, 'EQ'), (23, 'PL')):
        toggle(d, x, 61, 23, 12, label, 0, 0)
        toggle(d, x, 73, 23, 12, label, 1, 0)
        toggle(d, x + 46, 61, 23, 12, label, 0, 1)
        toggle(d, x + 46, 73, 23, 12, label, 1, 1)
    im.save(out / 'shufrep.bmp')


def make_posbar(out):
    im = img(307, 10, FACE)
    d = ImageDraw.Draw(im)
    inset(d, 0, 0, 248, 10, (16, 16, 26))
    rect(d, 2, 4, 244, 2, (8, 8, 14))
    for x, pressed in ((248, 0), (278, 1)):
        bevel(d, x, 0, 29, 10, (110, 110, 150) if not pressed else (150, 150, 196), (190, 190, 230), BTN_LO)
        for gx in range(x + 10, x + 19, 3):
            rect(d, gx, 3, 1, 4, BTN_LO)
            rect(d, gx + 1, 3, 1, 4, (200, 200, 236))
    im.save(out / 'posbar.bmp')


def mix(a, b, t):
    return tuple(int(x + (y - x) * t) for x, y in zip(a, b))


def make_slider_strip(out, name, w, x0, color_at):
    im = img(w, 433, FACE)
    d = ImageDraw.Draw(im)
    fw = w - x0
    for i in range(28):
        y = i * 15
        inset(d, x0, y, fw, 13, (10, 10, 16))
        rect(d, x0 + 2, y + 5, fw - 4, 3, color_at(i / 27))
        rect(d, x0 + 2, y + 5, fw - 4, 1, mix(color_at(i / 27), (255, 255, 255), 0.35))
    for x, pressed in ((15, 0), (0, 1)):
        bevel(d, x, 422, 14, 11, (150, 150, 196) if pressed else (110, 110, 150), (200, 200, 236), BTN_LO)
        rect(d, x + 4, 425, 6, 1, BTN_LO)
        rect(d, x + 4, 427, 6, 1, BTN_LO)
        rect(d, x + 4, 429, 6, 1, BTN_LO)
    im.save(out / name)


def level_color(t):
    return mix((0, 180, 0), (220, 220, 0), t * 2) if t < 0.5 else mix((220, 220, 0), (230, 40, 0), t * 2 - 1)


def make_playpaus(out):
    im = img(42, 9, BLACK)
    d = ImageDraw.Draw(im)
    pattern(d, 2, 1, ["#....", "###..", "#####", "###..", "#....", "....."], GREEN)
    pattern(d, 11, 1, ["##.##", "##.##", "##.##", "##.##", "##.##"], (220, 160, 0))
    pattern(d, 20, 1, ["#####", "#####", "#####", "#####", "#####"], (200, 40, 0))
    rect(d, 36, 0, 3, 9, (60, 0, 0))
    rect(d, 39, 0, 3, 9, (0, 120, 0))
    im.save(out / 'playpaus.bmp')


def make_monoster(out):
    im = img(56, 24, BLACK)
    d = ImageDraw.Draw(im)
    for y, color in ((0, GREEN), (12, (36, 52, 36))):
        text(d, 'STEREO', 0, y + 3, color)
        text(d, 'MONO', 29 + 3, y + 3, color)
    im.save(out / 'monoster.bmp')


SEGMENTS = {'a': (2, 0, 5, 2), 'b': (6, 1, 2, 5), 'c': (6, 7, 2, 5), 'd': (2, 11, 5, 2),
            'e': (1, 7, 2, 5), 'f': (1, 1, 2, 5), 'g': (2, 5, 5, 2)}
DIGITS = ['abcdef', 'bc', 'abged', 'abgcd', 'fgbc', 'afgcd', 'afgedc', 'abc', 'abcdefg', 'abcdfg']


def make_numbers(out):
    im = img(99, 13, BLACK)
    d = ImageDraw.Draw(im)
    for i, segs in enumerate(DIGITS):
        for s in segs:
            x, y, w, h = SEGMENTS[s]
            rect(d, i * 9 + x, y, w, h, GREEN)
    im.save(out / 'numbers.bmp')


def make_text(out):
    im = img(155, 18, BLACK)
    d = ImageDraw.Draw(im)
    for r, row in enumerate(ROWS):
        for c, ch in enumerate(row):
            text(d, ch, c * 5, r * 6, TEXT_FG)
    im.save(out / 'text.bmp')


def make_main(out):
    im = img(275, 116)
    d = ImageDraw.Draw(im)
    face(d, 275, 116)
    rect(d, 274, 0, 1, 116, FACE_LO)
    inset(d, 8, 19, 98, 47)                       # LCD: clutter bar, time, visualizer
    rect(d, 9, 20, 96, 45, LCD)
    inset(d, 109, 23, 158, 13)                    # song title
    inset(d, 109, 40, 19, 11)                     # kbps
    text(d, 'KBPS', 130, 43, LABEL)
    inset(d, 154, 40, 14, 11)                     # khz
    text(d, 'KHZ', 170, 43, LABEL)
    inset(d, 210, 39, 58, 15, (10, 10, 16))       # mono / stereo
    inset(d, 14, 70, 252, 14, (16, 16, 26))       # position bar well
    inset(d, 14, 86, 117, 22, FACE_MID)           # transport well
    # Logo: a small bolt badge in the corner.
    bevel(d, 244, 89, 24, 18, (36, 36, 56), FACE_HI, FACE_LO)
    pattern(d, 251, 91, ["....##", "...##.", "..##..", ".#####", "...##.", "..##..", ".##...", "##....",
                         "#.....", ".....", ".....", "....."], GOLD)
    for y in range(68, 108, 4):
        rect(d, 270, y, 2, 1, FACE_HI)
    im.save(out / 'main.bmp')


def eq_slider_frame(d, x, y, t):
    inset(d, x, y, 14, 63, (10, 10, 16))
    rect(d, x + 6, y + 3, 2, 57, (4, 4, 8))
    level = int(57 * (1 - t))
    rect(d, x + 6, y + 3 + level, 2, 57 - level, level_color(1 - t))


def make_eqmain(out):
    im = img(275, 315, FACE)
    d = ImageDraw.Draw(im)
    face(d, 275, 116)
    rect(d, 0, 0, 275, 14, FACE_MID)
    inset(d, 84, 15, 117, 23)                      # graph well
    for i, label in enumerate(['PREAMP']):
        text(d, label, 13, 104, LABEL)
    for i, label in enumerate(['60', '170', '310', '600', '1K', '3K', '6K', '12K', '14K', '16K']):
        cx = 78 + 18 * i + 7
        text(d, label, cx - text_width(label) // 2, 104, LABEL)
    text(d, '+12DB', 44, 39, LABEL)
    text(d, '+0DB', 47, 67, LABEL)
    text(d, '-12DB', 44, 95, LABEL)
    for x in (21,) + tuple(78 + 18 * i for i in range(10)):
        eq_slider_frame(d, x, 38, 0.5)
    small_button(d, 0, 116, CLOSE, False)
    small_button(d, 0, 125, CLOSE, True)
    for x, lit, pressed in ((10, 0, 0), (128, 0, 1), (69, 1, 0), (187, 1, 1)):
        toggle(d, x, 119, 26, 12, 'ON', lit, pressed)
    for x, lit, pressed in ((36, 0, 0), (155, 0, 1), (95, 1, 0), (214, 1, 1)):
        toggle(d, x, 119, 32, 12, 'AUTO', lit, pressed)
    title_bar(d, 0, 134, 275, 'EQUALIZER', True)
    title_bar(d, 0, 149, 275, 'EQUALIZER', False)
    for i in range(28):
        eq_slider_frame(d, 13 + (i % 14) * 15, 164 + (i // 14) * 65, i / 27)
    for y, pressed in ((164, 0), (176, 1)):
        bevel(d, 0, y, 11, 11, (150, 150, 196) if pressed else (110, 110, 150), (200, 200, 236), BTN_LO)
        rect(d, 3, y + 5, 5, 1, BTN_LO)
    for y, pressed in ((164, 0), (176, 1)):
        toggle(d, 224, y, 44, 12, 'PRESETS', 0, pressed, led=False)
    rect(d, 0, 294, 113, 19, BLACK)
    for gx in range(0, 113, 4):
        d.point((gx, 294 + 9), fill=(40, 40, 60))
    for i in range(19):
        d.point((115, 294 + i), fill=level_color(1 - i / 18))
    rect(d, 0, 314, 113, 1, (110, 110, 140))
    im.save(out / 'eqmain.bmp')


def make_pledit(out):
    im = img(280, 186, FACE)
    d = ImageDraw.Draw(im)
    for y, active in ((0, True), (21, False)):
        top, bottom = ((88, 88, 140), (44, 44, 84)) if active else ((64, 64, 84), (40, 40, 54))
        for x0, w in ((0, 25), (26, 100), (127, 25), (153, 25)):
            for i in range(20):
                rect(d, x0, y + i, w, 1, mix(top, bottom, i / 19))
            rect(d, x0, y, w, 1, (150, 150, 200) if active else (100, 100, 120))
            rect(d, x0, y + 19, w, 1, FACE_LO)
        rect(d, 0, y, 1, 20, FACE_HI)
        grip = (190, 170, 90) if active else (110, 110, 130)
        for x0, w in ((6, 19), (127, 25), (153, 10)):
            for gy in (y + 6, y + 9, y + 12):
                rect(d, x0, gy, w, 1, grip)
        label = 'PLAYLIST'
        text(d, label, 26 + (100 - text_width(label)) // 2, y + 7, GOLD if active else (150, 150, 170))
        small_button(d, 153 + 13, y + 3, CLOSE, False)
    for i in range(29):
        rect(d, 0, 42 + i, 12, 1, FACE)
        rect(d, 31, 42 + i, 20, 1, FACE)
    rect(d, 0, 42, 1, 29, FACE_HI)
    rect(d, 11, 42, 1, 29, FACE_LO)
    rect(d, 31, 42, 1, 29, FACE_HI)
    rect(d, 36, 42, 8, 29, (16, 16, 26))           # scrollbar groove (x = width - 15)
    rect(d, 50, 42, 1, 29, FACE_LO)
    for x, pressed in ((52, 0), (61, 1)):
        bevel(d, x, 53, 8, 18, (150, 150, 196) if pressed else (110, 110, 150), (200, 200, 236), BTN_LO)
        for gy in (59, 61, 63):
            rect(d, x + 2, gy, 4, 1, BTN_LO)
    small_button(d, 52, 42, CLOSE, True)
    face(d, 125, 38, 0, 72)
    # Bottom-left corner: ADD REM SEL MISC buttons.
    for i, label in enumerate(['ADD', 'REM', 'SEL', 'MISC']):
        bx = 14 + 29 * i
        bevel(d, bx, 72 + 10, 25, 18, BTN, BTN_HI, BTN_LO)
        text(d, label, bx + (25 - text_width(label)) // 2 + 1, 72 + 17, ICON)
    # Bottom-right corner: running time, mini transport and LIST button.
    for i in range(38):
        rect(d, 126, 72 + i, 150, 1, mix((60, 60, 88), (42, 42, 64), i / 37))
    rect(d, 126, 109, 150, 1, FACE_LO)
    rect(d, 275, 72, 1, 38, FACE_LO)
    inset(d, 126 + 6, 72 + 8, 90, 10)
    for i, kind in enumerate(['prev', 'play', 'pause', 'stop', 'next', 'eject']):
        bx = 126 + 6 + i * 11
        bevel(d, bx, 72 + 22, 10, 9, BTN, BTN_HI, BTN_LO)
        glyph = {'prev': ["#.#", "###", "#.#"], 'play': ["#..", "##.", "#.."], 'pause': ["#.#", "#.#", "#.#"],
                 'stop': ["###", "###", "###"], 'next': ["#.#", "###", "#.#"], 'eject': [".#.", "###", "###"]}[kind]
        pattern(d, bx + 3, 72 + 25, glyph, ICON)
    bevel(d, 126 + 150 - 44, 72 + 10, 25, 18, BTN, BTN_HI, BTN_LO)
    text(d, 'LIST', 126 + 150 - 44 + 3, 72 + 17, ICON)
    # Bottom tile and visualizer background.
    for i in range(38):
        rect(d, 179, i, 25, 1, mix((60, 60, 88), (42, 42, 64), i / 37))
    rect(d, 179, 37, 25, 1, FACE_LO)
    rect(d, 205, 0, 75, 38, FACE_MID)
    inset(d, 205 + 4, 8, 67, 22)
    im.save(out / 'pledit.bmp')


def make_text_files(out):
    vis = [(0, 0, 0), (24, 33, 41)]
    for i in range(16):
        vis.append(mix((239, 49, 16), (41, 206, 16), i / 15))
    vis += [(255, 255, 255), (214, 214, 222), (181, 189, 189), (160, 170, 175), (148, 156, 165), (150, 150, 150)]
    (out / 'viscolor.txt').write_text(''.join(f'{r},{g},{b}\n' for r, g, b in vis))
    (out / 'pledit.txt').write_text('[Text]\nNormal=#00E000\nCurrent=#FFFFFF\nNormalBG=#000000\n'
                                     'SelectedBG=#0000C6\nFont=Arial\n')


def make_icon(path):
    """App icon: the corner bolt badge as 1024 px pixel art."""
    im = img(32, 32)
    d = ImageDraw.Draw(im)
    for y in range(32):
        rect(d, 0, y, 32, 1, mix((70, 70, 110), (24, 24, 40), y / 31))
    inset(d, 4, 4, 24, 24, (8, 8, 14))
    bolt = ["......####", ".....####.", "....####..", "...####...", "..#######.", "....####..",
            "...####...", "..####....", ".####.....", ".###......", ".##.......", ".#........"]
    pattern(d, 11, 10, bolt, GOLD)
    for x, y in ((8, 23), (11, 21), (14, 22), (17, 20), (20, 23), (23, 21)):
        rect(d, x, y, 2, 27 - y, GREEN)
    im.resize((1024, 1024), Image.NEAREST).save(path)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    make_main(out)
    make_titlebar(out)
    make_cbuttons(out)
    make_shufrep(out)
    make_posbar(out)
    make_slider_strip(out, 'volume.bmp', 68, 0, level_color)
    make_slider_strip(out, 'balance.bmp', 47, 9, lambda t: level_color(abs(t - 0.5) * 2))
    make_playpaus(out)
    make_monoster(out)
    make_numbers(out)
    make_text(out)
    make_eqmain(out)
    make_pledit(out)
    make_text_files(out)
    make_icon(out.parent / 'icon.png')
    print('Wrote skin to ' + str(out))


if __name__ == '__main__':
    main()
