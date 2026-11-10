"""Building kit for Scener: the toolbox of the Blender building scripts, writing .blks instead of meshes.

Ported from nile-archive/tools/building_kit.py. A building is a function that draws into a Kit
in pivot-local, whole-tile coordinates, exactly as it does in Blender:

    X = tile u (front-left side), Y = -v (front-right side), Z = up, 1 unit = 1 tile.

Scener is Y-up and measures in centimetres, so the kit maps (x, y, z) tiles to (x, z, -y) * TILE cm.
That keeps handedness: Blender's front (-Y) is Scener's +Z, the side a camera at +X +Z sees first.
Rotations about Blender's Z become Scener rotations about Y with the same sign.

Everything is a profile put through modifiers, as Scener prefers, not a hard-coded primitive:
  box / beam / frustum  rect or rounded-rect -> extrude -> bevel (-> taper)
  tube                  circle -> extrude -> bevel -> taper along the axis
  ball / lathe / pyramid  a profile curve revolved about the vertical axis (a sphere is a half circle)
  poly / roof           a line -> extrude (-> shell -> fillet)

Differences from the Blender kit, all forced by what a scene file can say:
  - no per-vertex colour: every piece carries one jittered sRGB colour, and leaves and clay a vertical
    gradient (gradientFrom/gradientTo, a multiplier over the piece's bounding box)
  - no free-form surfaces: palm fronds are flattened revolved spheres, polygons must stand in a vertical plane
  - a taper scales both axes alike, so a frustum's two insets cannot differ
  - no footprint clamp (Blender clamps vertices to the footprint after the fact)

Randomness is a small LCG shared with building_kit.lua so both ports write identical files.
"""
import math

TILE = 160.0    # cm per tile
G = 0.1         # top of the base slab, in tiles
SOLID_ROUND = 0.45   # a bevel never exceeds this fraction of a piece's thinnest side
THIN = 1.5      # cm thickness of a flat polygon
HALF = 50.0     # cm radius of the shared unit-sphere profile

# material -> (shininess, bevel width in tiles or 0, unlit); GRADIENTS: material -> darkest..lightest multiplier
# along the shape's own up axis, the Blender kit's darker-at-the-bottom vertex colours (<gradient> attributes)
GRADIENTS = {"leaf": (.6, 1.2), "clay": (.8, 1.05)}
MATERIALS = {
  "stone": (6, .04, False), "slab": (6, .04, False), "wood": (8, .03, False), "clay": (14, .03, False),
  "glaze": (40, .028, False), "gold": (60, .022, False), "cloth": (3, .025, False), "hedge": (6, .07, False),
  "leaf": (10, 0, False), "matte": (6, 0, False), "glass": (80, 0, False), "water": (70, 0, False),
  "flame": (4, 0, True), "glow": (4, 0, True),
}


def C(h):
  h = h.lstrip("#")
  return tuple(int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))


class Rng:
  """Park-Miller style LCG; the Lua kit uses the same constants."""
  def __init__(self, seed=7): self.s = seed % 2147483647 or 1
  def seed(self, n): self.s = n % 2147483647 or 1
  def next(self):
    self.s = (self.s * 48271) % 2147483647
    return self.s / 2147483647
  def uniform(self, a, b): return a + (b - a) * self.next()
  def choice(self, seq): return seq[int(self.next() * len(seq)) % len(seq)]


R = Rng()


def J(col, j=.08):
  m = 1 + R.uniform(-j, j)
  return tuple(min(1, max(0, c * m * (1 + R.uniform(-j, j) * .25))) for c in col)


def f(v):
  s = "%.2f" % v
  if "." in s: s = s.rstrip("0").rstrip(".")
  return "0" if s in ("-0", "") else s


def f4(v):
  s = ("%.4f" % v).rstrip("0").rstrip(".")
  return "0" if s in ("-0", "") else s


def v3(a, b, c): return "%s %s %s" % (f(a), f(b), f(c))
def rgb(c): return "%.3f %.3f %.3f" % tuple(c)


def attrs(pairs): return "".join(' %s="%s"' % (k, v) for k, v in pairs if v is not None)


class Kit:
  """Collects one building's scene XML in pivot-local coordinates."""

  def __init__(self, w, h, shapes):
    self.w, self.h = w, h
    self.lines, self.lights, self.shapes, self.depth = [], [], shapes, 2

  # ---- emitting

  def emit(self, s): self.lines.append("  " * self.depth + s)

  def open(self, tag, pairs): self.emit("<%s%s>" % (tag, attrs(pairs))); self.depth += 1
  def close(self, tag): self.depth -= 1; self.emit("</%s>" % tag)

  def at(self, x, y, ang=0.):
    """Opens a frame (a group moved to x, y and turned ang about vertical); use with `with`."""
    return _At(self, x, y, ang)

  def look(self, mat, col):
    shin, _, unlit = MATERIALS[mat]
    p = [("color", rgb(col)), ("shininess", shin)]
    if mat in GRADIENTS: p += [("gradientFrom", v3(*[GRADIENTS[mat][0]] * 3)), ("gradientTo", v3(*[GRADIENTS[mat][1]] * 3))]
    return p + ([("unlit", 1), ("castShadow", 0)] if unlit else [])

  # ---- primitives

  # ---- solids: every shape is a profile (line, rect, circle, revolved curve) put through modifiers

  def solid(self, profile, depth, pos, mat, col, rot=(), mods=""):
    """A 2D profile extruded `depth` cm along local Z, bevelled, then deformed by `mods`."""
    ext = "<extrude amount=\"%s\"/>" % f(depth)
    return self.emit("<%s%s%s%s>%s</%s>" % (profile[0], attrs(profile[1]), attrs([("pos", pos)]) + attrs(list(rot)), attrs(self.look(mat, col)), ext + mods, profile[0]))

  def rounded(self, mat, a, b, depth):
    """Corner radius and rim bevel (cm) for a piece with cross-section a x b tiles and extruded depth cm."""
    rad = min(MATERIALS[mat][1] * TILE, SOLID_ROUND * min(a, b) * TILE)
    return (rad, min(.85 * rad, SOLID_ROUND * depth)) if rad >= .8 and depth >= 2.4 else (0, 0)

  def prism(self, mat, pos, a, b, depth, col, rot=(), mods=""):
    """A box as a rect (or rounded-rect) of a x b tiles, extruded `depth` cm, with the kit's bevels."""
    rad, bev = self.rounded(mat, a, b, depth)
    size = ("size", "%s %s" % (f(a * TILE), f(b * TILE)))
    if not rad:
      return self.solid(("rect", [size]), depth, pos, mat, col, rot, mods)
    bevel = "<bevel amount=\"%s\" bevelSegments=\"3\"/>" % f(bev)
    return self.solid(("rounded-rect", [size, ("radius", f(rad)), ("segments", 4)]), depth, pos, mat, col, rot, bevel + mods)

  def box(self, mat, x, y, z, sx, sy, sz, col, rz=0.):
    self.prism(mat, v3(x * TILE, (z + sz / 2) * TILE, -y * TILE), sx, sz, sy * TILE, col, [("rot", v3(0, math.degrees(rz), 0))] if rz else [])

  def beam(self, mat, a, b, w, d, col):
    """A bar of section w (horizontal) by d (vertical-ish) from a to b."""
    dx, dy, dz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    L = math.sqrt(dx * dx + dy * dy + dz * dz)
    yaw, pitch = math.atan2(dy, dx), math.atan2(dz, math.hypot(dx, dy))
    mid = tuple((a[i] + b[i]) / 2 for i in range(3))
    self.open("group", [("pos", v3(mid[0] * TILE, mid[2] * TILE, -mid[1] * TILE)), ("rot", v3(0, math.degrees(yaw), 0))])
    self.prism(mat, "0 0 0", L, d, w * TILE, col, [("rot", v3(0, 0, math.degrees(pitch)))])
    self.close("group")

  def tube(self, mat, a, b, r0, r1, col, n=10, smooth=True):
    """A circle extruded along the tube and tapered from radius r0 at a to r1 at b."""
    dx, dy, dz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    L = math.sqrt(dx * dx + dy * dy + dz * dz)
    mid = tuple((a[i] + b[i]) / 2 for i in range(3))
    ratio = r1 / r0
    t = (ratio - 1) / (ratio + 1)
    bev = min(.35 * min(r0, r1) * TILE, SOLID_ROUND * L * TILE)
    mods = ("<bevel amount=\"%s\" bevelSegments=\"2\"/>" % f(bev) if bev >= .6 else "") + ("<taper amount=\"%s\" axis=\"z\"/>" % f4(t) if abs(t) > .001 else "")
    tilt = math.degrees(math.acos(max(-1, min(1, dz / L))))
    p = v3(mid[0] * TILE, mid[2] * TILE, -mid[1] * TILE)
    if tilt < .01:
      self.solid(("circle", [("radius", f(r0 * TILE / (1 - t))), ("segments", max(8, n))]), L * TILE, p, mat, col, [("rot", "-90 0 0")], mods)
      return
    self.open("group", [("pos", p), ("rot", v3(0, math.degrees(math.atan2(dy, dx)), 0))])
    self.open("group", [("rot", v3(0, 0, -tilt))])
    self.solid(("circle", [("radius", f(r0 * TILE / (1 - t))), ("segments", max(8, n))]), L * TILE, "0 0 0", mat, col, [("rot", "-90 0 0")], mods)
    self.close("group")
    self.close("group")

  def revolve(self, prof, pos, scale, segments, look, rot=()):
    """Revolves a profile of (radius, height) cm pairs about the vertical axis; identical profiles share one shape."""
    key = " ".join("%s,%s" % (f(0 if r < .3 else r), f(h)) for r, h in prof)
    sid = self.shapes.setdefault(key, "lathe%d" % len(self.shapes))
    self.emit("<lathe shape=\"%s\" segments=\"%d\"%s%s%s%s/>" % (sid, segments, attrs([("pos", pos)] + ([("scale", scale)] if scale else [])), attrs(list(rot)), "", attrs(look)))

  def lathe(self, mat, x, y, z, prof, col, n=18, sx=1., sy=1.):
    if callable(col): col = col(prof[len(prof) // 2][1])
    self.revolve([(r * TILE, h * TILE) for r, h in prof], v3(x * TILE, z * TILE, -y * TILE), v3(sx, 1, sy) if (sx, sy) != (1., 1.) else None, n, self.look(mat, col))

  def ball(self, mat, x, y, z, r, col, n=12, sx=1., sy=1., sz=1., rot=None):
    """A sphere is a revolved half-circle: one 100 cm profile shared by all, scaled to the radii."""
    if callable(col): col = col(r)
    rings = max(4, n // 2)
    prof = [(HALF * math.sin(math.pi * i / rings), HALF * (1 - math.cos(math.pi * i / rings))) for i in range(rings + 1)]
    k = TILE / HALF
    self.revolve(prof, v3(x * TILE, (z - r * sz) * TILE, -y * TILE), v3(r * sx * k, r * sz * k, r * sy * k), n, self.look(mat, col), [("rot", rot)] if rot else [])

  def poly(self, mat, pts, col):
    """A flat polygon standing in a vertical plane; pts[1] fixes the plane's direction."""
    (x0, y0, z0), (x1, y1, _) = pts[0], pts[1]
    ang = math.atan2(y1 - y0, x1 - x0)
    c, s = math.cos(ang), math.sin(ang)
    flat = [((x - x0) * c + (y - y0) * s, z - z0) for x, y, z in pts]
    assert all(abs(-(x - x0) * s + (y - y0) * c) < 1e-6 for x, y, _ in pts), "poly must stand in a vertical plane"
    self.open("group", [("pos", v3(x0 * TILE, z0 * TILE, -y0 * TILE)), ("rot", v3(0, math.degrees(ang), 0))])
    self.open("shape", [("closed", 1)] + self.look(mat, col))
    for u, v in flat: self.emit("<v x=\"%s\" y=\"%s\"/>" % (f(u * TILE), f(v * TILE)))
    self.emit("<extrude amount=\"%s\"/>" % f(THIN))
    self.close("shape")
    self.close("group")

  def frustum(self, mat, x, y, z, sx, sy, h, tx, ty, col, rz=0.):
    """A battered block: a box tapered towards its top. Scener tapers both axes alike, so the
    taper follows tx and the depth is scaled with it; ty is not honoured."""
    ratio = (sx - 2 * tx) / sx
    a = (ratio - 1) / (ratio + 1)
    k = 1 / (1 - a)
    self.prism(mat, v3(x * TILE, (z + h / 2) * TILE, -y * TILE), sx * k, h, sy * k * TILE, col,
               [("rot", v3(0, math.degrees(rz), 0))] if rz else [], "<taper amount=\"%s\"/>" % f4(a))

  def pyramid(self, mat, x, y, z, s, h, col):
    """Four revolved segments turned a quarter step, so the base is square."""
    self.revolve([(s * TILE / math.sqrt(2), 0), (0, h * TILE)], v3(x * TILE, z * TILE, -y * TILE), None, 4, self.look(mat, col), [("rot", "0 45 0")])

  # ---- plants

  def fit(self, x, y, reach):
    """Moves a plant inward so its reach stays on the footprint."""
    lx, ly = max(self.w / 2 - reach, 0), max(self.h / 2 - reach, 0)
    return max(-lx, min(lx, x)), max(-ly, min(ly, y))

  def blob(self, x, y, z, r, leaves):
    base = J(R.choice(leaves), .04)
    self.ball("leaf", x, y, z, r, base, 10)

  def puff(self, x, y, z, leaves, r=.25):
    """A rounded shrub: a few big soft balls."""
    r *= 1.25
    x, y = self.fit(x, y, r * 1.17)
    for i in range(3):
      a = 2 * math.pi * i / 3 + R.uniform(0, 1)
      self.blob(x + r * .45 * math.cos(a), y + r * .45 * math.sin(a), z + r * .55, r * .72, leaves)
    self.blob(x, y, z + r * 1.05, r * .75, leaves)

  def palm(self, x, y, z, trunk, leaves, h=1., L=.5, fronds=6):
    """Trunk and drooping fronds, each a flattened sphere; Blender sweeps a curved surface instead."""
    x, y = self.fit(x, y, L * 1.02)
    self.tube("wood", (x, y, z), (x, y, z + h), .09, .06, trunk, 10)
    for i in range(fronds):
      a = 2 * math.pi * i / fronds + R.uniform(-.15, .15)
      base = J(R.choice(leaves), .04)
      self.open("group", [("pos", v3(x * TILE, (z + h) * TILE, -y * TILE)), ("rot", v3(0, math.degrees(a), 0))])
      self.ball("leaf", L * .5, 0, -L * .1, 1, base, 8, L * .52, L * .17, L * .04, "0 0 -22")
      self.close("group")
    self.blob(x, y, z + h, .08, leaves)

  # ---- props

  def light(self, x, y, z, watts, color=(1, .62, .28)):
    self.lights.append((x, y, z, watts, color))

  def crate(self, x, y, z, s, wood, wood_l, h=None, rz=0.):
    h = h or s
    self.box("wood", x, y, z, s, s, h, J(wood_l, .05), rz)
    self.box("wood", x, y, z + h, s + .05, s + .05, .06, wood, rz)

  def basket(self, x, y, z, base, fruit, r=.2):
    self.lathe("matte", x, y, z, [(r * .62, 0), (r, r * .55), (r * 1.05, r * .85), (r * .85, r * .88)], J(base, .05), 16)
    col = R.choice(fruit)
    for i in range(3):
      a = 2 * math.pi * i / 3
      self.ball("matte", x + r * .42 * math.cos(a), y + r * .42 * math.sin(a), z + r * .85, r * .4, J(col, .05), 10)

  # ---- output

  def finish(self):
    for x, y, z, watts, color in self.lights:
      self.emit("<light pos=\"%s\" color=\"%s\" intensity=\"%s\" radius=\"%s\" castShadows=\"0\"/>" % (
        v3(x * TILE, z * TILE, -y * TILE), rgb(color), f(.35 * watts), f(TILE * 1.4)))
    return self.lines


class _At:
  def __init__(self, kit, x, y, ang): self.kit, self.p = kit, (x, y, ang)
  def __enter__(self):
    x, y, ang = self.p
    self.kit.open("group", [("pos", v3(x * TILE, 0, -y * TILE)), ("rot", v3(0, math.degrees(ang), 0))])
  def __exit__(self, *a): self.kit.close("group")


def wall_frame(a, b):
  """x along a->b, y to the inside when walking counter-clockwise, z up."""
  return a[0], a[1], math.atan2(b[1] - a[1], b[0] - a[0])


def face(a, b, t=.24):
  """A frame on a wall's outer surface: x along it, -y out."""
  x, y, ang = wall_frame(a, b)
  return x + t / 2 * math.sin(ang), y - t / 2 * math.cos(ang), ang


def assemble(name, items, builders, slab_color, out, title=""):
  """Writes every building as a named group on a shelf of whole-tile cells to `out`."""
  shapes, body, u, v, row_h = {}, [], 0, 0, 0
  for i, it in enumerate(items):
    R.seed(7 + i)
    k = Kit(it["w"], it["h"], shapes)
    k.box("slab", 0, 0, 0, it["w"], it["h"], G, slab_color)
    builders[it["key"]](k)
    if u + it["w"] > 12: u, v, row_h = 0, v + row_h + 2, 0
    it["at"] = (u + it["w"] / 2, v + it["h"] / 2)
    body.append((it, k.finish()))
    u, row_h = u + it["w"] + 2, max(row_h, it["h"])
  return shapes, body
