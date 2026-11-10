#!/usr/bin/env python3
"""Generate nile_buildings.blks: six Nile Archive buildings, ported from the Blender scripts.

    python3 apps/scener/tools/gen_nile_buildings.py [out.blks]

The builders are the ones in nile-archive/games/nile-archive/art/building-catalogue/build_scene.py
(small_pyramid, mud_house, pottery, bakery, watchtower, pylon_gate), unchanged apart from the
kit calls Scener cannot make. gen_nile_buildings.lua writes the same file.
"""
import math, os, sys
from building_kit import *

SLAB = C("#CDB283")
LIME, LIME_D, LIME_L = C("#E6D3A3"), C("#C7AC78"), C("#F3E6C2")
MUD, MUD_L, WASH = C("#B98B5C"), C("#D3A976"), C("#F1E7CF")
WOOD, WOOD_L, WOOD_D = C("#8E5427"), C("#B47436"), C("#5A3216")
CLAY, CLAY_L, POT = C("#C94E2C"), C("#E06A3A"), C("#B5613A")
TEAL, LAPIS, OCHRE = C("#16A296"), C("#2B5CAE"), C("#B9552F")
GOLD, FLAME, DOOR = C("#E8AE2C"), C("#FF7A0A"), C("#3A2614")
LINEN, INK, DATE, BASKET = C("#F0E4C6"), C("#2B2420"), C("#C8641E"), C("#C39146")
LEAVES = [C(h) for h in ("#3FB23C", "#55C43F", "#6FD043")]
FRUIT = [C(h) for h in ("#D9402E", "#EE8A1E", "#7DBB33", "#E9C32E", "#C2352E")]


def puff(k, x, y, z, r=.25): k.puff(x, y, z, LEAVES, r)
def crate(k, x, y, z, s, h=None, rz=0.): k.crate(x, y, z, s, WOOD, WOOD_L, h, rz)
def basket(k, x, y, z, r=.2): k.basket(x, y, z, BASKET, FRUIT, r)


def cornice(k, x, y, z, sx, sy, band=TEAL):
  """The flared top of an Egyptian wall: a painted band and a pale lip."""
  k.box("glaze", x, y, z, sx + .06, sy + .06, .08, band)
  k.box("stone", x, y, z + .08, sx + .14, sy + .14, .1, LIME_L)


def doorway(k, a, b, s, w, h, z=0.):
  """A dark door on the outside of wall a-b with a pale frame and a lintel."""
  with k.at(*face(a, b, 0)):
    k.box("matte", s, -.012, z, w, .03, h, DOOR)
    for side in (-1, 1): k.box("stone", s + side * (w / 2 + .05), -.02, z, .1, .05, h, LIME_L)
    k.box("stone", s, -.03, z + h, w + .26, .07, .12, LIME_L)
    k.box("glaze", s, -.05, z + h + .02, w * .6, .02, .07, LAPIS)


def slot(k, a, b, s, z, w=.12, h=.22, col=DOOR):
  with k.at(*face(a, b, 0)): k.box("matte", s, -.012, z, w, .03, h, col)


def relief(k, a, b, s, z, w, h, cols=(OCHRE, LAPIS, GOLD)):
  """A painted panel on a wall: three figures as coloured blocks."""
  with k.at(*face(a, b, 0)):
    k.box("matte", s, -.01, z, w, .02, h, J(LIME_L, .02))
    for i, c in enumerate(cols):
      px = s + (i - (len(cols) - 1) / 2) * w / (len(cols) + .4)
      k.box("matte", px, -.022, z + h * .14, w * .13, .02, h * .52, c)
      k.ball("matte", px, -.026, z + h * .76, w * .07, INK, 8, sy=.3)


def flag(k, x, y, z, h, col, d=(0, -1)):
  k.tube("wood", (x, y, z), (x, y, z + h), .035, .03, WOOD_D, 8)
  k.ball("gold", x, y, z + h + .02, .05, GOLD, 10)
  dx, dy = d
  k.poly("cloth", [(x, y, z + h - .04), (x + dx * .42, y + dy * .42, z + h - .14), (x, y, z + h - .36)], col)


def jar(k, x, y, z, r=.12, col=None, band=None):
  k.lathe("clay", x, y, z, [(r * .45, 0), (r, r * .8), (r * .95, r * 1.5), (r * .45, r * 2.1), (r * .5, r * 2.35), (r * .38, r * 2.4)], col or J(POT, .06), 14)
  if band: k.lathe("glaze", x, y, z + r * 1.15, [(r * 1.01, 0), (r * 1.01, r * .22)], band, 14)


def date_palm(k, x, y, z, h=1., L=.5, fronds=7, dates=True):
  k.palm(x, y, z, WOOD, LEAVES, h, L, fronds)
  x, y = k.fit(x, y, L * 1.02)
  if dates:
    for a in (.6, 2.6, 4.5): k.ball("matte", x + .08 * math.cos(a), y + .08 * math.sin(a), z + h - .1, .06, DATE, 8, sz=1.4)


def small_pyramid(k):
  for i in range(4):
    s = 1.66 - .36 * i
    k.box("stone", 0, 0, G + i * .3, s, s, .3, J(LIME, .02))
  k.pyramid("gold", 0, 0, G + 1.2, .3, .3, GOLD)
  doorway(k, (.83, -.83), (.83, .83), .83, .3, .26, G)
  puff(k, .8, -.8, G, .14)
  puff(k, -.8, -.82, G, .13)
  date_palm(k, -.72, .72, G, .8, .38, 6)


def mud_house(k):
  k.box("stone", 0, .1, G, 1.3, 1.3, 1.0, J(WASH, .02))
  k.box("stone", 0, .1, G + 1.0, 1.38, 1.38, .07, LIME_D)
  k.box("stone", -.36, .44, G + 1.07, .5, .58, .5, J(WASH, .02))
  k.box("cloth", .25, -.1, G + 1.45, .8, .8, .04, TEAL)
  for x, y in ((-.1, -.45), (.6, -.45), (.6, .35)): k.tube("wood", (x, y, G + 1.07), (x, y, G + 1.45), .025, .025, WOOD_D, 6)
  doorway(k, (.65, -.55), (.65, .75), .55, .28, .58, G)
  slot(k, (-.65, -.55), (.65, -.55), .35, G + .55, .18, .2)
  slot(k, (-.65, -.55), (.65, -.55), .95, G + .55, .18, .2)
  for i in range(5): k.box("stone", -.55 + i * .2, -.72, G, .2, .22, .2 + i * .2, J(LIME_L, .02))
  jar(k, .8, -.75, G, .1, band=LAPIS)
  date_palm(k, .75, .78, G, .9, .35, 6)


def pottery(k):
  k.box("stone", -.3, .35, G, 1.0, .9, .75, J(MUD_L, .03))
  k.box("stone", -.3, .35, G + .75, 1.06, .96, .06, MUD)
  k.box("cloth", -.3, -.3, G + .6, 1.0, .5, .04, OCHRE)
  for x in (-.75, .15): k.tube("wood", (x, -.5, G), (x, -.5, G + .6), .03, .03, WOOD_D, 6)
  doorway(k, (-.8, -.1), (.2, -.1), .7, .26, .5, G)
  k.lathe("clay", .5, .45, G, [(.38, 0), (.36, .3), (.22, .62), (.08, .72), (.001, .74)], J(CLAY, .03), 16)
  k.box("glow", .5, .07, G + .1, .2, .04, .18, FLAME)
  k.light(.5, -.05, G + .2, .8)
  k.lathe("wood", .3, -.45, G, [(.06, 0), (.06, .26), (.2, .26), (.2, .3)], WOOD_L, 14)
  k.lathe("clay", .3, -.45, G + .3, [(.08, 0), (.1, .1), (.05, .16)], CLAY_L, 12)
  for x, y, r, b in ((-.8, -.8, .1, TEAL), (.75, -.75, .09, None), (.8, -.4, .08, LAPIS), (-.55, -.78, .08, None)): jar(k, x, y, G, r, band=b)


def bakery(k):
  k.box("stone", -.35, .2, G, 1.7, 1.1, .85, J(MUD_L, .03))
  cornice(k, -.35, .2, G + .85, 1.7, 1.1, OCHRE)
  doorway(k, (-1.2, -.35), (.5, -.35), .85, .3, .55, G)
  slot(k, (-1.2, -.35), (.5, -.35), .35, G + .45, .2, .18)
  slot(k, (-1.2, -.35), (.5, -.35), 1.35, G + .45, .2, .18)
  for y in (-.4, .35):
    k.lathe("clay", 1.0, y, G, [(.3, 0), (.29, .2), (.18, .45), (.001, .52)], J(CLAY, .03), 16)
    k.box("glow", 1.29, y, G + .06, .04, .16, .14, FLAME)
    k.light(1.36, y, G + .14, .6)
  k.box("wood", .3, -.72, G, .6, .3, .3, WOOD_L)
  for i in range(4): k.ball("matte", .12 + i * .12, -.72, G + .33, .06, C("#D39446"), 10, sz=.6)
  basket(k, -.9, -.7, G, .14)
  k.box("cloth", -.35, -.55, G + .62, 1.6, .4, .04, LINEN)


def watchtower(k):
  k.box("stone", 0, 0, G, 1.5, 1.5, .12, LIME_L)
  k.frustum("stone", 0, 0, G + .12, 1.15, 1.15, 1.9, .1, .1, J(LIME_D, .02))
  k.box("stone", 0, 0, G + 2.02, 1.05, 1.05, .1, LIME)
  for s in (-.36, 0, .36):
    for x, y in ((s, -.45), (s, .45), (-.45, s), (.45, s)):
      if (x, y) != (0, 0): k.box("stone", x, y, G + 2.12, .18, .18, .18, LIME_L)
  flag(k, .2, .2, G + 2.12, .7, LAPIS, (1, 0))
  doorway(k, (-.57, -.57), (.57, -.57), .57, .3, .5, G + .12)
  slot(k, (.55, -.55), (.55, .55), .55, G + 1.3, .1, .3)
  slot(k, (-.55, -.55), (.55, -.55), .55, G + 1.3, .1, .3)
  crate(k, -.6, -.62, G, .22)


def pylon_gate(k):
  for x in (-.85, .85):
    k.frustum("stone", x, 0, G, 1.1, 1.1, 1.85, .16, .1, J(LIME, .02))
    cornice(k, x, 0, G + 1.85, .78, .9, TEAL)
    relief(k, (x - .5, -.5), (x + .5, -.5), .5, G + .5, .58, .9)
    flag(k, x + (.32 if x > 0 else -.32), -.62, G, 2.0, (OCHRE if x < 0 else LAPIS), (-1 if x < 0 else 1, 0))
  k.box("stone", 0, 0, G, .64, .8, 1.25, LIME_D)
  cornice(k, 0, 0, G + 1.25, .6, .8, LAPIS)
  doorway(k, (-.32, -.4), (.32, -.4), .32, .36, .85, G)
  k.ball("gold", 0, -.44, G + 1.36, .1, GOLD, 12, sy=.3)
  for s in (-1, 1): k.box("glaze", s * .2, -.44, G + 1.38, .26, .03, .06, LAPIS)


ITEMS = [dict(key=k, title=t, w=w, h=h) for k, t, w, h in (
  ("small_pyramid", "Small pyramid", 2, 2), ("mud_house", "Mud-brick house", 2, 2), ("pottery", "Pottery", 2, 2),
  ("bakery", "Bakery", 3, 2), ("watchtower", "Watchtower", 2, 2), ("pylon_gate", "Pylon gate", 3, 2))]
BUILDERS = dict(small_pyramid=small_pyramid, mud_house=mud_house, pottery=pottery, bakery=bakery,
                watchtower=watchtower, pylon_gate=pylon_gate)

HEAD = """<?xml version="1.0" encoding="UTF-8"?>
<!-- Six Nile Archive buildings on their paved bases, ported from the Blender building scripts
     (nile-archive tools/building_kit.py). Generated by tools/gen_nile_buildings.py or .lua; each building
     is a named group you can copy into another scene. -->
<scene ambient="0.55 0.54 0.58" background="0.62 0.72 0.95">
  <include src="utilities.xml"/>
  <sun dir="-0.4 -1 -0.9" color="1.0 0.93 0.80" intensity="0.95" castShadows="1"/>
  <sun dir="1.0 -0.5 0.6" color="1.0 0.88 0.72" intensity="0.22" castShadows="0"/>
  <material id="grass" color="0.36 0.54 0.24" shininess="3"/>"""

GROUND_MARGIN = 3   # tiles of grass around the shelves


def main(path):
  shapes, body = assemble("nile", ITEMS, BUILDERS, SLAB, None)
  x0 = min(it["at"][0] - it["w"] / 2 for it, _ in body) - GROUND_MARGIN
  x1 = max(it["at"][0] + it["w"] / 2 for it, _ in body) + GROUND_MARGIN
  v0 = min(it["at"][1] - it["h"] / 2 for it, _ in body) - GROUND_MARGIN
  v1 = max(it["at"][1] + it["h"] / 2 for it, _ in body) + GROUND_MARGIN
  cx, cv = (x0 + x1) / 2, (v0 + v1) / 2
  out = [HEAD]
  out.append('  <camera name="Overview" comment="All buildings, three-quarter view from the front corner" pos="%s" look="%s" fov="30"/>'
             % (v3((x1 - x0) * TILE * .2, (x1 - x0) * TILE * .42, (v1 - v0) * TILE * .5 + (x1 - x0) * TILE * .42), v3(0, 40, 0)))
  for it, _ in body:
    bx, bz = (it["at"][0] - cx) * TILE, (it["at"][1] - cv) * TILE
    out.append('  <camera name="%s" comment="%s" pos="%s" look="%s" fov="32"/>' % (it["title"], it["title"], v3(bx + 420, 420, bz + 700), v3(bx, 90, bz)))
  for key, sid in shapes.items():
    out.append('  <shape id="%s" closed="0">%s</shape>' % (sid, "".join('<v x="%s" y="%s"/>' % tuple(p.split(",")) for p in key.split(" "))))
  out.append('  <box pos="0 -8 0" size="%s 16 %s" material="grass"/>' % (f((x1 - x0) * TILE), f((v1 - v0) * TILE)))
  for it, lines in body:
    out.append('  <group name="%s" pos="%s">' % (it["key"], v3((it["at"][0] - cx) * TILE, 0, (it["at"][1] - cv) * TILE)))
    out.extend(lines)
    out.append("  </group>")
  out.append("</scene>\n")
  with open(path, "w") as fh: fh.write("\n".join(out))
  print("wrote %s: %d buildings" % (path, len(body)))


if __name__ == "__main__":
  main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scenes", "nile_buildings.blks"))
