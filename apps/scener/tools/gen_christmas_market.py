#!/usr/bin/env python3
"""Convert the Christmas Market houses (nile-archive games/christmas-market/art/building-catalogue/market_wishes.py)
to a Scener scene, running that script unchanged.

    python3 gen_christmas_market.py [--nile ~/Developer/nile-archive] [out.blks]

The Blender scripts build meshes through nile-archive/tools/building_kit.py. This converter imports that
kit with stand-ins for bpy, bmesh and mathutils (a 4x4 Matrix and a Vector), then subclasses its Kit so the
primitives write Scener elements instead of vertices: boxes become soft-box/slab utility calls, tubes and
lathes their modifier stacks, frames (k.at) groups with pos/rot/scale, and free-form geometry (surface,
add, poly) a <mesh>. Masonry, windows, awnings and plants are the kit's own code on top of these.

Not carried over: per-vertex colour (a piece gets the mean of its colours) and the footprint clamp.
The nile kit draws with Python's random, so there is no Lua twin of this converter.
"""
import argparse, importlib.util, math, os, sys, types

HERE = os.path.dirname(os.path.abspath(__file__))


# ---------------------------------------------------------------- mathutils stand-in

class Vector:
  def __init__(self, v=(0, 0, 0)): self.v = [float(c) for c in v]
  x = property(lambda s: s.v[0]); y = property(lambda s: s.v[1]); z = property(lambda s: s.v[2])
  def __iter__(self): return iter(self.v)
  def __getitem__(self, i): return self.v[i]
  def __len__(self): return len(self.v)
  def __add__(self, o): return Vector(a + b for a, b in zip(self.v, o))
  def __sub__(self, o): return Vector(a - b for a, b in zip(self.v, o))
  def __neg__(self): return Vector(-a for a in self.v)
  def __mul__(self, k): return Vector(a * k for a in self.v)
  __rmul__ = __mul__
  def __truediv__(self, k): return Vector(a / k for a in self.v)
  @property
  def length(self): return math.sqrt(sum(a * a for a in self.v))
  def normalized(self): n = self.length or 1; return Vector(a / n for a in self.v)
  def dot(self, o): return sum(a * b for a, b in zip(self.v, o))
  def cross(self, o):
    a, b = self.v, list(o)
    return Vector((a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]))
  def lerp(self, o, t): return self * (1 - t) + Vector(o) * t


class Matrix:
  def __init__(self, rows): self.m = [list(map(float, r)) for r in rows]
  @staticmethod
  def Identity(n=4): return Matrix([[1. if i == j else 0. for j in range(4)] for i in range(4)])
  @staticmethod
  def Translation(v):
    m = Matrix.Identity(); m.m[0][3], m.m[1][3], m.m[2][3] = v[0], v[1], v[2]; return m
  @staticmethod
  def Scale(s, n=4, axis=None):
    m = Matrix.Identity()
    for i in range(3): m.m[i][i] = s
    return m
  @staticmethod
  def Rotation(a, n, axis):
    c, s, m = math.cos(a), math.sin(a), Matrix.Identity()
    i, j = {"X": (1, 2), "Y": (2, 0), "Z": (0, 1)}[axis.upper()]
    m.m[i][i], m.m[i][j], m.m[j][i], m.m[j][j] = c, -s, s, c
    return m
  def __matmul__(self, o):
    if isinstance(o, Matrix): return Matrix([[sum(self.m[i][k] * o.m[k][j] for k in range(4)) for j in range(4)] for i in range(4)])
    v = list(o) + [1.]
    return Vector(sum(self.m[i][k] * v[k] for k in range(4)) for i in range(3))
  def inverted(self):
    s = (self.m[0][0] ** 2 + self.m[1][0] ** 2 + self.m[2][0] ** 2) ** .5 or 1   # rotation, uniform scale and translation only
    r = [[self.m[j][i] / (s * s) for j in range(3)] for i in range(3)]
    t = [self.m[i][3] for i in range(3)]
    out = Matrix.Identity()
    for i in range(3):
      for j in range(3): out.m[i][j] = r[i][j]
      out.m[i][3] = -sum(r[i][k] * t[k] for k in range(3))
    return out


def load_nile(nile):
  """The Blender kit, imported with stand-in bpy/bmesh/mathutils modules."""
  for name, attrs in (("bpy", {}), ("bmesh", {}), ("mathutils", {"Matrix": Matrix, "Vector": Vector})):
    mod = types.ModuleType(name); mod.__dict__.update(attrs); sys.modules[name] = mod
  tools = os.path.join(nile, "tools")
  spec = importlib.util.spec_from_file_location("building_kit", os.path.join(tools, "building_kit.py"))
  kit = importlib.util.module_from_spec(spec); sys.modules["building_kit"] = kit; spec.loader.exec_module(kit)
  sys.path.insert(0, os.path.join(nile, "games", "christmas-market", "art", "building-catalogue"))
  return kit


# ---------------------------------------------------------------- Scener writer

sys.path.insert(0, HERE)
import importlib
_sk = importlib.util.spec_from_file_location("scener_kit", os.path.join(HERE, "building_kit.py"))
scener = importlib.util.module_from_spec(_sk); sys.modules["scener_kit"] = scener; _sk.loader.exec_module(scener)
TILE = scener.TILE


def srgb(c):
  """The Blender palettes are linear; scene files take sRGB."""
  return tuple(min(1., (12.92 * v if v <= .0031308 else 1.055 * v ** (1 / 2.4) - .055)) for v in c)


def colour(col):
  if callable(col): return lambda *a: srgb(col(*a))
  if isinstance(col, list): return srgb(tuple(sum(c[i] for c in col) / len(col) for i in range(3)))
  return srgb(col)


def to_scener(p): return (p[0] * TILE, p[2] * TILE, -p[1] * TILE)


def euler(m):
  """Scener rot (degrees, Rz*Ry*Rx) of the rotation part of a Blender matrix, with uniform scale s."""
  s = (m.m[0][0] ** 2 + m.m[1][0] ** 2 + m.m[2][0] ** 2) ** .5
  r = [[m.m[i][j] / s for j in range(3)] for i in range(3)]
  b = [[1, 0, 0], [0, 0, 1], [0, -1, 0]]   # Blender (x, y, z) -> Scener (x, z, -y)
  mul = lambda a, c: [[sum(a[i][k] * c[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
  r = mul(mul(b, r), [list(c) for c in zip(*b)])
  ry = -math.asin(max(-1, min(1, r[2][0])))
  if abs(math.cos(ry)) > 1e-6: rx, rz = math.atan2(r[2][1], r[2][2]), math.atan2(r[1][0], r[0][0])
  else: rx, rz = math.atan2(-r[1][2], r[1][1]), 0.
  return tuple(math.degrees(a) for a in (rx, ry, rz)), s


def make_kit(nile_kit):
  f, v3, attrs = scener.f, scener.v3, scener.attrs

  class SKit(nile_kit.Kit):
    def __init__(self, w, h, shapes=None):
      super().__init__(w, h)
      self.sk = scener.Kit(w, h, shapes if shapes is not None else {})

    class _Frame:
      def __init__(self, kit, m): self.kit, self.m = kit, m
      def __enter__(self):
        k = self.kit
        rot, s = euler(self.m)
        p = to_scener((self.m.m[0][3], self.m.m[1][3], self.m.m[2][3]))
        pairs = [("pos", v3(*p)), ("rot", v3(*rot)) if any(abs(a) > 1e-6 for a in rot) else ("rot", None),
                 ("scale", "%s %s %s" % ((f(s),) * 3)) if abs(s - 1) > 1e-6 else ("scale", None)]
        k.sk.open("group", pairs)
        self.prev, k.xf = k.xf, k.xf @ self.m
      def __exit__(self, *a):
        self.kit.xf = self.prev; self.kit.sk.close("group")

    def at(self, m): return SKit._Frame(self, m)

    def box(self, mat, x, y, z, sx, sy, sz, col, rz=0.): self.sk.box(mat, x, y, z, sx, sy, sz, colour(col), rz)
    def beam(self, mat, a, b, w, d, col): self.sk.beam(mat, tuple(a), tuple(b), w, d, colour(col))
    def tube(self, mat, a, b, r0, r1, col, n=10, smooth=True): self.sk.tube(mat, tuple(a), tuple(b), r0, r1, colour(col), n)
    def ball(self, mat, x, y, z, r, col, n=12, sx=1., sy=1., sz=1.): self.sk.ball(mat, x, y, z, r, colour(col), n, sx, sy, sz)

    def lathe(self, mat, x, y, z, prof, col, n=18, smooth=True, sx=1., sy=1., m=None):
      if m is None: return self.sk.lathe(mat, x, y, z, prof, colour(col), n, sx, sy)
      rot, _ = euler(m)   # a turned lathe: the group sits at the lathe's origin
      self.sk.open("group", [("pos", v3(*to_scener((x, y, z)))), ("rot", v3(*rot))])
      self.sk.lathe(mat, 0, 0, 0, prof, colour(col), n, sx, sy)
      self.sk.close("group")

    def surface(self, mat, fn, nu, nv, col):
      pts = [fn(i / nu, j / nv) for j in range(nv + 1) for i in range(nu + 1)]
      faces = [(j * (nu + 1) + i, j * (nu + 1) + i + 1, j * (nu + 1) + i + nu + 2, j * (nu + 1) + i + nu + 1) for j in range(nv) for i in range(nu)]
      mean = (lambda: colour(col(.5, .5))) if callable(col) else (lambda: colour(col))
      self._mesh(mat, pts, faces, mean(), smooth=True, both=True)

    def add(self, mat, verts, faces, col, smooth=False, m=None):
      if m is not None: verts = [tuple(m @ Vector(v)) for v in verts]
      self._mesh(mat, verts, faces, colour(col), smooth, both=False)

    def poly(self, mat, pts, col, smooth=False): self._mesh(mat, pts, [tuple(range(len(pts)))], colour(col), False, both=True)

    def _mesh(self, mat, pts, faces, col, smooth, both):
      s = self.sk
      s.open("mesh", [("smooth", 1 if smooth else None), ("twoSided", 1 if both else None)] + s.look(mat, col))
      for p in pts: s.emit('<v x="%s" y="%s" z="%s"/>' % tuple(f(c) for c in to_scener(p)))
      for fc in faces: s.emit('<f v="%s"/>' % " ".join(str(i) for i in fc))
      s.close("mesh")

    def finish(self):
      for p, watts in self.lights: self.sk.light(p[0], p[1], p[2], watts)
      return self.sk.finish()

  return SKit


HEAD = """<?xml version="1.0" encoding="UTF-8"?>
<!-- The twelve Christmas Market objects, converted from market_wishes.py (nile-archive games/christmas-market)
     by tools/gen_christmas_market.py. Each is a named group; boxes are utilities.xml soft-box/slab calls. -->
<scene ambient="0.50 0.55 0.68" background="0.30 0.38 0.58">
  <include src="utilities.xml"/>
  <sun dir="-0.4 -1 -0.9" color="0.95 0.92 1.0" intensity="0.8" castShadows="1"/>
  <sun dir="1.0 -0.5 0.6" color="0.7 0.8 1.0" intensity="0.2" castShadows="0"/>
  <material id="snowfield" color="0.86 0.9 0.96" shininess="4"/>"""


def main():
  ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  ap.add_argument("out", nargs="?", default=os.path.join(HERE, "..", "scenes", "christmas_market_buildings.blks"))
  ap.add_argument("--nile", default=os.path.expanduser("~/Developer/nile-archive"))
  a = ap.parse_args()
  nile = load_nile(a.nile)
  nile.Kit = make_kit(nile)
  import market_wishes as mw
  mw.Kit = nile.Kit
  shapes, body = {}, []
  for i, it in enumerate(mw.ITEMS):
    mw.R.seed(7 + i)
    k = mw.Kit(it["w"], it["h"], shapes)
    mw.snow_base(k)
    getattr(mw, it["key"])(k)
    body.append((it, k.finish()))
  cols, gap = 4, 1.5
  rows = (len(body) + cols - 1) // cols
  cell = 3 + gap
  W, D = cols * cell + gap, rows * cell + gap
  out = [HEAD]
  out.append('  <camera name="Overview" comment="The whole market, three-quarter view" pos="%s" look="0 30 0" fov="30"/>' % scener.v3(W * TILE * .15, W * TILE * .55, D * TILE * .5 + W * TILE * .45))
  placed = []
  for n, (it, lines) in enumerate(body):
    cx, cz = ((n % cols) - (cols - 1) / 2) * cell * TILE, ((n // cols) - (rows - 1) / 2) * cell * TILE
    placed.append((it, lines, cx, cz))
    out.append('  <camera name="%s" comment="%s" pos="%s" look="%s" fov="32"/>' % (it["title"], it["title"], scener.v3(cx + 360, 360, cz + 520), scener.v3(cx, 90, cz)))
  for key, sid in shapes.items():
    out.append('  <shape id="%s" closed="0">%s</shape>' % (sid, "".join('<v x="%s" y="%s"/>' % tuple(p.split(",")) for p in key.split(" "))))
  out.append('  <box pos="0 -8 0" size="%s 16 %s" material="snowfield"/>' % (scener.f(W * TILE), scener.f(D * TILE)))
  for it, lines, cx, cz in placed:
    out.append('  <group name="%s" pos="%s">' % (it["key"], scener.v3(cx, 0, cz)))
    out.extend(lines)
    out.append("  </group>")
  out.append("</scene>\n")
  with open(a.out, "w") as fh: fh.write("\n".join(out))
  print("wrote %s: %d buildings" % (os.path.abspath(a.out), len(body)))


main()
