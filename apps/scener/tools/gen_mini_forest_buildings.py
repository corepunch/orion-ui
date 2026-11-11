#!/usr/bin/env python3
"""Generate mini_forest_buildings.blks: ten chunky low-poly buildings on grass tiles."""
import math, sys

out = []
def e(s): out.append(s)
def f(v): return ("%.2f" % v).rstrip("0").rstrip(".") if abs(v) > 1e-6 else "0"
def v3(a, b, c): return "%s %s %s" % (f(a), f(b), f(c))

# Kit pieces have softly rounded edges: every box is a soft-box utility call (utilities.xml)
# and every cylinder a bevelled circle. Radii scale with the piece so thin
# trims stay crisp while walls and roofs read as chunky toys.
ROUND = 0.42      # corner radius as a fraction of the smaller cross-section side
ROUND_MAX = 22.0  # cm
BEVEL = 0.92      # rim bevel as a fraction of the corner radius (must stay below it)
NOISE_SCALE = 0.6 # <noise> scale as a fraction of the radius: neighbouring vertices of a low-poly sphere move apart

def box(p, s, m, rot=None, extra="", mods=""):
  r = ' rot="%s"' % v3(*rot) if rot else ""
  rad = min(ROUND_MAX, ROUND * min(s[0], s[1]))
  bev = min(BEVEL * rad, s[2] * 0.46)
  if rad < 0.6 or bev < 0.4:
    if mods: e('    <box pos="%s" size="%s"%s material="%s"%s>%s</box>' % (v3(*p), v3(*s), r, m, extra, mods))
    else:    e('    <box pos="%s" size="%s"%s material="%s"%s/>' % (v3(*p), v3(*s), r, m, extra))
    return
  e('    <soft-box width="%s" height="%s" depth="%s" radius="%s" bevel="%s" pos="%s"%s material="%s"%s>%s</soft-box>'
    % (f(s[0]), f(s[1]), f(s[2]), f(rad), f(bev), v3(*p), r, m, extra, mods))

def cyl(p, r, h, m, sides=8, rot=None, extra="", mods=""):
  bev = min(ROUND_MAX, ROUND * min(r * 2, h), h * 0.5) * BEVEL
  e('    <group pos="%s"%s>' % (v3(*p), ' rot="%s"' % v3(*rot) if rot else ""))
  e('    <circle radius="%s" segments="%d" rot="90 0 0" material="%s"%s><extrude amount="%s"/><bevel amount="%s" bevelSegments="4"/>%s</circle>'
    % (f(r), max(8, sides), m, extra, f(h), f(bev), mods))
  e('    </group>')

def cone(p, r, h, m, sides=8, top=0, rot=None, smooth=True):
  rr = ' rot="%s"' % v3(*rot) if rot else ""
  t = ' radiusTop="%s"' % f(top) if top else ""
  sm = "" if smooth else ' smooth="0"'
  e('    <cone pos="%s" radius="%s"%s height="%s" sides="%d"%s%s material="%s"/>' % (v3(*p), f(r), t, f(h), sides, rr, sm, m))

def gable_fill(cx, y0, cz, w, d, h, m, along="z"):
  """Triangular gable solid: base width w at y0, apex h above, length d along `along`."""
  # prism r=1: apex at local +x, base at -0.5, half-width 0.866; rot 90 0 90 puts apex up, length on z.
  # local x->world y, local y->world z, local z->world x.
  sc = v3(h / 1.5, 1, (w / 2) / 0.866)
  rot = "90 0 90" if along == "z" else "90 90 90"
  e('    <prism pos="%s" radius="1" height="%s" sides="3" rot="%s" scale="%s" material="%s"/>'
    % (v3(cx, y0 + 0.5 * h / 1.5, cz), f(d), rot, sc, m))

STUDS = ((0.32, -0.28), (0.55, 0.18), (0.78, -0.06), (0.4, 0.38))

def gable_roof(cx, y0, cz, w, d, h, m, t=16, ov=18, along="z", fill=None):
  """One solid bent roof, as a shape modifier stack: the gable underside drawn as a line, extruded
  across the building into a face, shelled upward to thickness t, every corner rounded and both
  rims bevelled. The underside rests on the slopes from (cx +- w/2, y0) to the ridge at
  (cx, y0 + h); ov is the overhang along the slope and at the gable ends."""
  if along == "x":
    e('    <group pos="%s" rot="0 90 0">' % v3(cx, 0, cz))
    gable_roof(0, y0, 0, w, d, h, m, t, ov, "z", fill)
    e('    </group>')
    return
  if fill: gable_fill(cx, y0, cz, w, d, h, fill)
  a = math.atan2(h, w / 2)
  ex, ey = w / 2 + ov * math.cos(a), y0 - ov * math.sin(a)
  D = d + 2 * ov
  e('    <shape closed="0" pos="%s" material="%s" v="%s %s 0 %s %s %s">'
    '<extrude amount="%s"/><shell amount="%s"/><fillet radius="%s"/><bevel amount="%s" bevelSegments="4"/></shape>'
    % (v3(cx, 0, cz), m, f(-ex), f(ey), f(y0 + h), f(ex), f(ey), f(D), f(t), f(t * 0.45), f(t * 0.38)))
  # a few soft shingle studs on each slope, like the kit's roofs
  L = math.hypot(w / 2, h) + ov
  for side in (1, -1):
    n = (side * math.sin(a), math.cos(a))
    for (fr, fz) in STUDS:
      sx, sy = side * fr * L * math.cos(a), y0 + h - fr * L * math.sin(a)
      px, py = sx + n[0] * (t + 1.5), sy + n[1] * (t + 1.5)
      e('    <circle radius="%s" segments="10" pos="%s" rot="-90 0 %s" material="%s"><extrude amount="5"/><bevel amount="2" bevelSegments="3"/></circle>'
        % (f(max(6, t * 0.5)), v3(cx + px, py, cz + side * fz * D), f(-side * math.degrees(a)), m))

def window(x, y, z, face, w=36, h=40, glass="glass"):
  """face: +z, -z, +x, -x. (x,y,z) is the wall surface point at window centre."""
  sgn = 1 if face[0] == "+" else -1
  if face[1] == "z":
    box((x, y, z + sgn * 1.5), (w + 10, h + 10, 3), "trim")
    box((x, y, z + sgn * 2.5), (w, h, 3), glass)
    box((x, y - h / 2 - 6, z + sgn * 4), (w + 16, 5, 8), "trim")
  else:
    box((x + sgn * 1.5, y, z), (3, h + 10, w + 10), "trim")
    box((x + sgn * 2.5, y, z), (3, h, w), glass)
    box((x + sgn * 4, y - h / 2 - 6, z), (8, 5, w + 16), "trim")

def door(x, y0, z, face, w=48, h=86, m="door"):
  sgn = 1 if face[0] == "+" else -1
  if face[1] == "z":
    box((x, y0 + h / 2 + 3, z + sgn * 1.5), (w + 10, h + 6, 3), "trim")
    box((x, y0 + h / 2, z + sgn * 3), (w, h, 3), m)
    box((x + w * 0.3, y0 + h * 0.48, z + sgn * 6), (6, 6, 4), "metal_knob")
    box((x, y0 + 3, z + sgn * 18), (w + 24, 6, 30), "stone_step")
  else:
    box((x + sgn * 1.5, y0 + h / 2 + 3, z), (3, h + 6, w + 10), "trim")
    box((x + sgn * 3, y0 + h / 2, z), (3, h, w), m)
    box((x + sgn * 6, y0 + h * 0.48, z - sgn * w * 0.3), (4, 6, 6), "metal_knob")
    box((x + sgn * 18, y0 + 3, z), (30, 6, w + 24), "stone_step")

def pine(x, z, s=1.0, seed=0):
  """Slim faceted pine like the kit's: short trunk, four flat-shaded six-sided tiers, each turned
  so its facets fall between the ones below."""
  cyl((x, 14 * s, z), 8 * s, 28 * s, "trunk", sides=8)
  m = "pine" if int(abs(x + z)) % 3 else "pine_light"
  for i, (r, top, h, y) in enumerate(((46, 10, 62, 24), (40, 8, 58, 66), (32, 6, 52, 104), (22, 0, 46, 138))):
    cone((x, (y + h / 2) * s, z), r * s, h * s, m, sides=6, top=top * s, rot=(0, i * 30, 0), smooth=False)

def bush(x, z, r=26, m="leaf"):
  e('    <sphere pos="%s" radius="%s" rings="5" slices="7" material="%s"><noise strength="2" scale="%s" seed="%d"/></sphere>'
    % (v3(x, r * 0.7, z), f(r), m, f(r * NOISE_SCALE), int(abs(x * 7 + z * 13)) % 97 + 1))

def rock(x, z, r=16):
  e('    <sphere pos="%s" radius="%s" rings="3" slices="6" scale="1.2 0.7 1" material="rock"/>' % (v3(x, r * 0.5, z), f(r)))

TILE = 560
def tile():
  # grass tile with a dirt edge, like the kit's base tiles; top surface at y=0
  box((0, -16, 0), (TILE, 28, TILE), "dirt")
  box((0, -4, 0), (TILE + 2, 8, TILE + 2), "grass")

# ---------------------------------------------------------------- buildings
def cabin():
  """Log cabin: stacked logs, dark shingle roof, stone chimney."""
  W, D, H = 260, 200, 150
  box((0, 6, 0), (W + 20, 12, D + 20), "stone")
  for i in range(6):
    y = 12 + 13 + i * 24
    cyl((0, y, D / 2 - 12), 13, W + 24, "log" if i % 2 == 0 else "log_light", sides=8, rot=(0, 0, 90))
    cyl((0, y, -D / 2 + 12), 13, W + 24, "log" if i % 2 == 0 else "log_light", sides=8, rot=(0, 0, 90))
    cyl((W / 2 - 12, y + 12, 0), 13, D + 24, "log_light" if i % 2 == 0 else "log", sides=8, rot=(90, 0, 0))
    cyl((-W / 2 + 12, y + 12, 0), 13, D + 24, "log_light" if i % 2 == 0 else "log", sides=8, rot=(90, 0, 0))
  box((0, 12 + H / 2, 0), (W - 30, H, D - 30), "log_inner")
  door(-40, 12, D / 2, "+z")
  window(60, 95, D / 2, "+z")
  window(W / 2, 95, 0, "+x")
  gable_roof(0, 12 + H + 6, 0, W + 30, D + 20, 95, "roof_brown", along="x", fill="log_light")
  # chimney
  box((W / 2 - 50, 12 + H + 70, -40), (44, 200, 44), "stone")
  box((W / 2 - 50, 12 + H + 175, -40), (54, 14, 54), "stone_dark")

def cottage():
  """Cream cottage with red roof, flower boxes and a chimney."""
  W, D, H = 240, 190, 140
  box((0, H / 2, 0), (W, H, D), "plaster")
  box((0, 8, 0), (W + 8, 16, D + 8), "stone")
  for x in (-W / 2 + 6, W / 2 - 6):
    for z in (-D / 2 + 6, D / 2 - 6):
      box((x, H / 2, z), (14, H, 14), "timber")
  box((0, H - 6, D / 2 + 1), (W, 12, 6), "timber")
  door(0, 16, D / 2, "+z", w=50)
  for x in (-75, 75):
    window(x, 82, D / 2, "+z")
    box((x, 55, D / 2 + 12), (46, 12, 14), "timber")
    for k in (-14, 0, 14):
      e('    <sphere pos="%s" radius="7" rings="4" slices="6" material="%s"/>' % (v3(x + k, 66, D / 2 + 12), "flower_pink" if k else "flower_yellow"))
  window(W / 2, 82, 0, "+x")
  gable_roof(0, H, 0, W + 10, D, 110, "roof_red", along="z", fill="plaster")
  box((-60, H + 90, -30), (36, 140, 36), "brick")
  box((-60, H + 162, -30), (44, 10, 44), "brick_dark")

def aframe():
  """A-frame hut: the roof reaches the ground; glazed front gable."""
  W, D, H = 280, 240, 230
  gable_fill(0, 0, 0, W - 30, D - 20, H - 26, "wood_light", along="z")
  gable_roof(0, 0, 0, W, D, H, "roof_green", t=14, ov=10, along="z")
  door(0, 0, (D - 20) / 2, "+z", w=56, h=96)
  window(0, 150, (D - 20) / 2, "+z", w=34, h=34)
  box((0, 3, (D - 20) / 2 + 60), (W - 40, 6, 80), "deck")

def watchtower():
  """Ranger lookout: four log legs, cross braces, platform and a pyramid-roofed cabin."""
  S, P = 150, 260
  for x in (-S / 2, S / 2):
    for z in (-S / 2, S / 2):
      cyl((x, P / 2, z), 10, P, "log", sides=8)
  for z in (-S / 2, S / 2):
    d = math.degrees(math.atan2(P * 0.45, S))
    box((0, P * 0.35, z), (math.hypot(S, P * 0.45), 8, 8), "log_light", rot=(0, 0, d))
    box((0, P * 0.35, z), (math.hypot(S, P * 0.45), 8, 8), "log_light", rot=(0, 0, -d))
  box((0, P + 6, 0), (S + 70, 12, S + 70), "deck")
  for x in (-(S + 60) / 2, (S + 60) / 2):
    for z in (-(S + 60) / 2, (S + 60) / 2):
      box((x, P + 12 + 25, z), (8, 50, 8), "log")
  for s in (1, -1):
    box((0, P + 12 + 46, s * (S + 60) / 2), (S + 68, 6, 6), "log_light")
    box((s * (S + 60) / 2, P + 12 + 46, 0), (6, 6, S + 68), "log_light")
  box((0, P + 12 + 50, 0), (110, 100, 110), "wood_light")
  window(0, P + 12 + 60, 55, "+z", w=50, h=34)
  window(55, P + 12 + 60, 0, "+x", w=50, h=34)
  e('    <pyramid pos="%s" radius="105" height="70" rot="0 45 0" material="roof_brown"/>' % v3(0, P + 12 + 100 + 35, 0))
  # ladder
  for x in (-18, 18):
    box((x, P / 2 + 10, S / 2 + 26), (6, P + 20, 6), "log_light", rot=(-8, 0, 0))
  for i in range(9):
    y = 25 + i * 28
    box((0, y, S / 2 + 26 + (P / 2 - y) * math.tan(math.radians(8)) - 0), (40, 5, 5), "log", rot=(-8, 0, 0))

def watermill():
  """Stone mill with a timber upper storey and a paddle wheel in a stream."""
  W, D = 220, 180
  box((0, 50, 0), (W, 100, D), "stone")
  box((0, 100 + 55, 0), (W, 110, D), "wood")
  box((0, 103, 0), (W + 10, 8, D + 10), "timber")
  door(-40, 0, D / 2, "+z", w=50)
  window(50, 60, D / 2, "+z", w=30, h=30)
  window(-40, 160, D / 2, "+z")
  window(50, 160, D / 2, "+z")
  gable_roof(0, 210, 0, W + 10, D, 90, "roof_blue", along="z", fill="wood")
  # stream along the right side
  box((W / 2 + 70, 1, 0), (90, 2, TILE - 20), "water")
  # paddle wheel on the +x wall
  wx, wy = W / 2 + 34, 92
  cyl((wx, wy, 0), 14, 30, "metal", sides=8, rot=(0, 0, 90))
  e('    <torus pos="%s" majorRadius="80" minorRadius="5" majorSegments="16" minorSegments="5" rot="0 0 90" material="wood_dark"/>' % v3(wx + 8, wy, 0))
  e('    <torus pos="%s" majorRadius="80" minorRadius="5" majorSegments="16" minorSegments="5" rot="0 0 90" material="wood_dark"/>' % v3(wx - 8, wy, 0))
  box((wx, wy, 0), (24, 8, 160), "wood_light", mods='<array count="6" rotation="30 0 0"/>')

def treehouse():
  """Treehouse: fat trunk, round leafy crown, small hut on a platform."""
  cyl((0, 140, 0), 34, 280, "trunk", sides=9, mods='<taper center="0 0 0" amount="0.5" axis="z"/>')
  for a in (0, 120, 240):
    r = math.radians(a)
    box((math.cos(r) * 40, 10, math.sin(r) * 40), (60, 20, 22), "trunk", rot=(0, -a, -20))
  for (x, y, z, r, m) in ((0, 360, 0, 120, "leaf"), (-90, 320, 50, 80, "leaf_light"), (80, 330, -60, 85, "leaf_light"), (40, 410, 70, 70, "leaf")):
    e('    <sphere pos="%s" radius="%d" rings="6" slices="9" material="%s"><noise strength="5" scale="%s" seed="%d"/></sphere>' % (v3(x, y, z), r, m, f(r * NOISE_SCALE), r))
  box((60, 175, 50), (210, 10, 180), "deck")
  box((70, 225, 50), (120, 90, 110), "wood_light")
  gable_roof(70, 270, 50, 130, 110, 50, "roof_red", along="x", fill="wood_light", ov=12, t=8)
  door(70, 180, 105, "+z", w=36, h=70)
  window(130, 230, 50, "+x", w=28, h=28)
  # railing posts and rope ladder
  for x in (-40, 0, 160):
    box((x, 200, 138), (6, 40, 6), "log")
  box((60, 218, 138), (210, 5, 5), "log_light")
  for x in (-30, 0):
    box((x - 15, 85, 135), (4, 170, 4), "rope")
    box((x + 15 - 30, 85, 135), (4, 170, 4), "rope")

def roundhut():
  """Round hut: cylinder wall, conical thatch with a little cap."""
  cyl((0, 65, 0), 115, 130, "plaster_warm", sides=24)
  cyl((0, 8, 0), 122, 16, "stone", sides=24)
  e('    <cone pos="0 190 0" radius="160" radiusTop="18" height="130" sides="24" material="thatch"/>')
  e('    <cone pos="0 270 0" radius="26" height="30" sides="8" material="thatch_dark"/>')
  cyl((0, 130, 0), 160, 10, "thatch_dark", sides=24)
  # door + windows on the front, set in flat frame boxes against the round wall
  box((0, 58, 108), (62, 106, 20), "plaster_warm")
  door(0, 16, 118, "+z", w=46, h=84)
  for a in (55, -55):
    r = math.radians(a)
    e('    <group pos="%s" rot="0 %s 0">' % (v3(math.sin(r) * 108, 0, math.cos(r) * 108), f(a)))
    box((0, 80, 0), (50, 56, 20), "plaster_warm")
    window(0, 82, 10, "+z", w=30, h=30)
    e('    </group>')

def woodshed():
  """Open lean-to woodshed with a stack of split logs and a chopping block."""
  W, D = 250, 150
  for x in (-W / 2 + 10, W / 2 - 10):
    box((x, 90, -D / 2 + 10), (14, 180, 14), "timber")
    box((x, 65, D / 2 - 10), (14, 130, 14), "timber")
  box((0, 90, -D / 2 + 6), (W - 20, 170, 8), "plank")
  a = math.degrees(math.atan2(50, D))
  box((0, 162, 0), (W + 30, 10, math.hypot(D, 50) + 40), "roof_green", rot=(-a, 0, 0))
  box((0, 6, 0), (W, 12, D), "deck")
  for row in range(4):
    for i in range(7 - (row % 2)):
      x = -W / 2 + 40 + i * 28 + (row % 2) * 14
      cyl((x, 12 + 13 + row * 24, -10), 12, 90, "log" if (i + row) % 3 else "log_light", sides=7, rot=(90, 0, 0))
  cyl((W / 2 + 60, 22, 70), 28, 44, "log_light", sides=9)
  box((W / 2 + 60, 64, 70), (6, 40, 6), "trunk", rot=(0, 0, 25))
  box((W / 2 + 70, 82, 70), (24, 16, 6), "metal")

def ranger_station():
  """Two-storey ranger station: stone ground floor, shingled upper floor, porch."""
  W, D = 230, 190
  box((0, 60, 0), (W, 120, D), "stone_light")
  box((0, 120 + 55, 0), (W + 16, 110, D + 16), "wood_teal")
  box((0, 124, 0), (W + 20, 8, D + 20), "trim")
  door(-50, 0, D / 2, "+z", w=50)
  window(50, 70, D / 2, "+z")
  for x in (-60, 60):
    window(x, 180, D / 2 + 8, "+z", w=40, h=44)
  window(W / 2 + 8, 180, 0, "+x")
  window(W / 2, 70, 0, "+x")
  gable_roof(0, 230, 0, W + 30, D + 16, 85, "roof_blue", along="z", fill="wood_teal")
  # porch
  box((0, 6, D / 2 + 50), (W + 20, 12, 100), "deck")
  for x in (-W / 2, W / 2):
    box((x, 70, D / 2 + 92), (12, 116, 12), "timber")
  box((0, 133, D / 2 + 50), (W + 40, 10, 116), "roof_blue", rot=(12, 0, 0))
  # flag pole
  cyl((W / 2 + 50, 150, D / 2 + 70), 4, 300, "metal", sides=6)
  box((W / 2 + 80, 275, D / 2 + 70), (56, 36, 3), "flag")

def stone_tower():
  """Round stone tower with a tall pointed roof and a pennant."""
  cyl((0, 140, 0), 80, 280, "stone_light", sides=24, mods='<taper center="0 0 0" amount="0.16" axis="z"/>')
  cyl((0, 10, 0), 92, 20, "stone", sides=24)
  cyl((0, 286, 0), 94, 16, "stone", sides=24)
  e('    <cone pos="0 359 0" radius="104" height="130" sides="24" material="roof_purple"/>')
  cyl((0, 444, 0), 3, 40, "metal")
  box((14, 456, 0), (26, 16, 3), "flag")
  # band of stones
  door(0, 20, 80, "+z", w=46, h=84)
  for (y, a) in ((150, 0), (215, 60), (215, -60), (150, 120)):
    r = math.radians(a)
    e('    <group pos="%s" rot="0 %s 0">' % (v3(math.sin(r) * 72, 0, math.cos(r) * 72), f(a)))
    box((0, y, 0), (40, 54, 20), "stone_light")
    window(0, y + 2, 10, "+z", w=22, h=36, glass="glass_warm")
    e('    </group>')

def decorate(i):
  """A few trees, bushes and rocks on each tile, away from the building."""
  h = TILE / 2
  spots = [
    [(-h + 70, -h + 70, 1.0), (h - 70, -h + 90, 0.8)],
    [(-h + 60, -h + 80, 0.9), (h - 60, h - 70, 0.7)],
    [(h - 70, -h + 70, 1.1), (-h + 70, -h + 110, 0.75)],
    [(-h + 60, h - 60, 0.8), (h - 70, -h + 70, 1.0)],
    [(-h + 60, -h + 70, 0.9)],
    [(h - 60, -h + 60, 0.8)],
    [(-h + 70, -h + 70, 1.0), (h - 60, -h + 100, 0.85), (h - 70, h - 70, 0.7)],
    [(-h + 60, -h + 60, 0.9), (h - 60, h - 60, 0.8)],
    [(-h + 60, -h + 70, 1.0)],
    [(h - 70, -h + 70, 1.0), (-h + 60, -h + 90, 0.8)],
  ][i]
  for (x, z, s) in spots: pine(x, z, s)
  bush(-h + 60 + (i * 37) % 80, h - 50, 24, "leaf_light" if i % 2 else "leaf")
  rock(h - 50 - (i * 23) % 60, h - 40 - (i * 11) % 40, 14)

BUILDINGS = [cabin, cottage, aframe, watchtower, watermill,
             treehouse, roundhut, woodshed, ranger_station, stone_tower]

MATERIALS = """
  <material id="grass"         color="0.55 0.80 0.38" shininess="4"/>
  <material id="dirt"          color="0.62 0.43 0.28" shininess="4"/>
  <material id="water"         color="0.40 0.72 0.92" shininess="60"/>
  <material id="stone"         color="0.62 0.64 0.68" shininess="6"/>
  <material id="stone_dark"    color="0.46 0.48 0.53" shininess="6"/>
  <material id="stone_light"   color="0.78 0.79 0.80" shininess="6"/>
  <material id="stone_step"    color="0.70 0.71 0.73" shininess="6"/>
  <material id="rock"          color="0.66 0.67 0.70" shininess="6"/>
  <material id="log"           color="0.62 0.38 0.22" shininess="8"/>
  <material id="log_light"     color="0.76 0.52 0.32" shininess="8"/>
  <material id="log_inner"     color="0.52 0.32 0.20" shininess="8"/>
  <material id="trunk"         color="0.55 0.36 0.24" shininess="6"/>
  <material id="wood"          color="0.80 0.58 0.38" shininess="8"/>
  <material id="wood_light"    color="0.90 0.70 0.48" shininess="8"/>
  <material id="wood_dark"     color="0.45 0.28 0.18" shininess="8"/>
  <material id="wood_teal"     color="0.40 0.70 0.68" shininess="8"/>
  <material id="plank"         color="0.70 0.48 0.30" shininess="8"/>
  <material id="deck"          color="0.84 0.62 0.40" shininess="8"/>
  <material id="timber"        color="0.48 0.30 0.20" shininess="8"/>
  <material id="trim"          color="0.97 0.95 0.90" shininess="10"/>
  <material id="door"          color="0.55 0.32 0.22" shininess="10"/>
  <material id="metal_knob"    color="0.95 0.78 0.30" shininess="60"/>
  <material id="metal"         color="0.55 0.57 0.62" shininess="50"/>
  <material id="rope"          color="0.85 0.74 0.52" shininess="4"/>
  <material id="glass"         color="0.42 0.68 0.90" shininess="80"/>
  <material id="glass_warm"    color="1.00 0.82 0.40" shininess="40"/>
  <material id="plaster"       color="0.98 0.93 0.80" shininess="4"/>
  <material id="plaster_warm"  color="0.96 0.84 0.64" shininess="4"/>
  <material id="brick"         color="0.80 0.42 0.34" shininess="4"/>
  <material id="brick_dark"    color="0.62 0.30 0.26" shininess="4"/>
  <material id="thatch"        color="0.95 0.76 0.40" shininess="3"/>
  <material id="thatch_dark"   color="0.80 0.60 0.30" shininess="3"/>
  <material id="roof_red"      color="0.90 0.36 0.32" shininess="10"/>
  <material id="roof_red_dark" color="0.70 0.26 0.24" shininess="10"/>
  <material id="roof_brown"    color="0.56 0.36 0.28" shininess="10"/>
  <material id="roof_brown_dark" color="0.42 0.26 0.20" shininess="10"/>
  <material id="roof_green"    color="0.36 0.62 0.42" shininess="10"/>
  <material id="roof_green_dark" color="0.26 0.48 0.32" shininess="10"/>
  <material id="roof_blue"     color="0.34 0.52 0.80" shininess="10"/>
  <material id="roof_blue_dark" color="0.24 0.38 0.62" shininess="10"/>
  <material id="roof_purple"   color="0.56 0.42 0.76" shininess="10"/>
  <material id="flag"          color="0.96 0.40 0.36" shininess="4"/>
  <material id="flower_pink"   color="0.98 0.52 0.66" shininess="6"/>
  <material id="flower_yellow" color="1.00 0.86 0.30" shininess="6"/>
  <material id="leaf"          color="0.36 0.70 0.34" shininess="4"/>
  <material id="leaf_light"    color="0.52 0.80 0.36" shininess="4"/>
  <material id="pine"          color="0.24 0.58 0.40" shininess="4"/>
  <material id="pine_light"    color="0.32 0.68 0.44" shininess="4"/>"""

COLS, GAP = 5, 660
e('<?xml version="1.0" encoding="UTF-8"?>')
e('<!-- Ten casual low-poly forest buildings in a toy-like kit style, laid out on a 5x2 grid of')
e('     grass tiles. Generated; each building is a named group you can copy into another scene. -->')
e('<scene ambient="0.52 0.55 0.62" background="0.62 0.82 0.96">')
e('  <include src="utilities.xml"/>')
e('  <camera name="Grid" comment="All ten buildings, three-quarter overview" pos="500 2500 3300" look="0 20 40" fov="28.5"/>')
e('  <camera name="Front" comment="Lower front row view" pos="900 900 2400" look="0 150 300" fov="40"/>')
e('  <camera name="Closeup" comment="Cottage and log cabin, showing the rounded kit edges" pos="-300 420 300" look="-860 110 -330" fov="38"/>')
e('  <sun dir="-0.55 -1 -0.75" color="1.0 0.97 0.90" intensity="0.95" castShadows="1"/>')
e('  <sun dir="0.6 -0.5 0.4" color="0.70 0.80 1.0" intensity="0.18" castShadows="0"/>')
e(MATERIALS)
names = ["log_cabin", "cottage", "a_frame", "watchtower", "watermill",
         "treehouse", "round_hut", "woodshed", "ranger_station", "stone_tower"]
for i, fn in enumerate(BUILDINGS):
  r, c = divmod(i, COLS)
  x = (c - (COLS - 1) / 2) * GAP
  z = (r - 0.5) * GAP
  e('')
  e('  <!-- %s -->' % fn.__doc__.strip())
  e('  <group name="%s" pos="%s">' % (names[i], v3(x, 0, z)))
  tile()
  fn()
  decorate(i)
  e('  </group>')
e('</scene>')
sys.stdout.write("\n".join(l for l in out if l) + "\n")
