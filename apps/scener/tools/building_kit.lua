-- Building kit for Scener, Lua edition (profiles through modifiers: see building_kit.py). Same API and same output as building_kit.py: a building is a
-- function drawing into a Kit in pivot-local, whole-tile coordinates (X = u, Y = -v, Z up, 1 unit =
-- 1 tile) and the kit writes Scener XML (Y up, centimetres): (x, y, z) tiles -> (x, z, -y) * TILE.
-- Run under Lua 5.4: lua gen_nile_buildings.lua [out.blks]
local M = {}

local pi, sin, cos, sqrt, atan, acos, floor, abs, min, max =
  math.pi, math.sin, math.cos, math.sqrt, math.atan, math.acos, math.floor, math.abs, math.min, math.max
local fmt = string.format

local TILE, G = 160.0, 0.1
local SOLID_ROUND, THIN, HALF = 0.45, 1.5, 50.0
M.TILE, M.G = TILE, G

-- material -> {shininess, bevel width in tiles, unlit}
local MATERIALS = {
  stone = {6, .04, false}, slab = {6, .04, false}, wood = {8, .03, false}, clay = {14, .03, false},
  glaze = {40, .028, false}, gold = {60, .022, false}, cloth = {3, .025, false}, hedge = {6, .07, false},
  leaf = {10, 0, false}, matte = {6, 0, false}, glass = {80, 0, false}, water = {70, 0, false},
  flame = {4, 0, true}, glow = {4, 0, true},
}

-- material -> darkest..lightest multiplier along the shape's own up axis (gradientFrom/gradientTo)
local GRADIENTS = {leaf = {.6, 1.2}, clay = {.8, 1.05}}

-- Kenney gradient swatches (see kenney_palette.py): name -> {bottom = {r,g,b}, top = {r,g,b}}. A swatch is a
-- colour for any kit call; the piece is drawn with gradientFrom/gradientTo over its height.
local here = debug.getinfo(1, "S").source:sub(2):match("^(.*)/[^/]*$") or "."
function M.palette(name) return dofile(here .. "/../palettes/kenney_" .. name .. ".lua") end

local function deg(r) return r * 180 / pi end

function M.C(h)
  h = h:gsub("^#", "")
  return {tonumber(h:sub(1, 2), 16) / 255, tonumber(h:sub(3, 4), 16) / 255, tonumber(h:sub(5, 6), 16) / 255}
end

-- Park-Miller LCG, identical to the Python kit.
local Rng = {s = 7}
function Rng.seed(n) Rng.s = n % 2147483647; if Rng.s == 0 then Rng.s = 1 end end
function Rng.next() Rng.s = (Rng.s * 48271) % 2147483647; return Rng.s / 2147483647 end
function Rng.uniform(a, b) return a + (b - a) * Rng.next() end
function Rng.choice(seq) return seq[floor(Rng.next() * #seq) % #seq + 1] end
M.R = Rng

function M.J(col, j)
  j = j or .08
  local m = 1 + Rng.uniform(-j, j)
  local out = {}
  for i = 1, 3 do out[i] = min(1, max(0, col[i] * m * (1 + Rng.uniform(-j, j) * .25))) end
  return out
end

local function f(v)
  local s = fmt("%.2f", v)
  if s:find("%.") then s = s:gsub("0+$", ""):gsub("%.$", "") end
  if s == "-0" or s == "" then return "0" end
  return s
end
local function f4(v)
  local s = fmt("%.4f", v):gsub("0+$", ""):gsub("%.$", "")
  if s == "-0" or s == "" then return "0" end
  return s
end
local function v3(a, b, c) return f(a) .. " " .. f(b) .. " " .. f(c) end
local function rgb(c) return fmt("%.3f %.3f %.3f", c[1], c[2], c[3]) end
local function attrs(pairs_)
  local out = {}
  for _, p in ipairs(pairs_) do if p[2] ~= nil then out[#out + 1] = fmt(' %s="%s"', p[1], p[2]) end end
  return table.concat(out)
end
local function cat(a, b) local o = {table.unpack(a)}; for _, v in ipairs(b) do o[#o + 1] = v end; return o end

local Kit = {}
Kit.__index = Kit

function M.new_kit(w, h, shapes)
  return setmetatable({w = w, h = h, lines = {}, lights = {}, shapes = shapes, depth = 2}, Kit)
end

function Kit:emit(s) self.lines[#self.lines + 1] = string.rep("  ", self.depth) .. s end
function Kit:open(tag, pairs_) self:emit("<" .. tag .. attrs(pairs_) .. ">"); self.depth = self.depth + 1 end
function Kit:close(tag) self.depth = self.depth - 1; self:emit("</" .. tag .. ">") end

-- Runs fn inside a frame: a group moved to x, y and turned ang about vertical.
function Kit:at(x, y, ang, fn)
  self:open("group", {{"pos", v3(x * TILE, 0, -y * TILE)}, {"rot", v3(0, deg(ang or 0), 0)}})
  fn()
  self:close("group")
end

function Kit:look(mat, col)
  local m = MATERIALS[mat]
  local swatch = col.bottom ~= nil
  local p = swatch and {{"color", "1 1 1"}, {"gradientFrom", rgb(col.bottom)}, {"gradientTo", rgb(col.top)}, {"shininess", m[1]}}
    or {{"color", rgb(col)}, {"shininess", m[1]}}
  local g = (not swatch) and GRADIENTS[mat]
  if g then p[#p + 1] = {"gradientFrom", v3(g[1], g[1], g[1])}; p[#p + 1] = {"gradientTo", v3(g[2], g[2], g[2])} end
  if m[3] then p[#p + 1] = {"unlit", 1}; p[#p + 1] = {"castShadow", 0} end
  return p
end

local function pos(x, y, z) return v3(x * TILE, z * TILE, -y * TILE) end

-- Solids: every shape is a profile (line, rect, circle, revolved curve) put through modifiers.

-- A 2D profile extruded `depth` cm along local Z, bevelled, then deformed by `mods`.
function Kit:solid(tag, pattrs, depth, p, mat, col, rot, mods, pre)
  self:emit(fmt('<%s%s%s%s%s>%s%s</%s>', tag, attrs(pattrs), attrs({{"pos", p}}), attrs(rot or {}), attrs(self:look(mat, col)),
    (pre or "") .. fmt('<extrude amount="%s"/>', f(depth)), mods or "", tag))
end

-- Corner radius and rim bevel (cm) for a piece with cross-section a x b tiles and extruded depth cm.
local function rounded(mat, a, b, depth)
  local rad = min(MATERIALS[mat][2] * TILE, SOLID_ROUND * min(a, b) * TILE)
  if rad >= .8 and depth >= 2.4 then return rad, min(.85 * rad, SOLID_ROUND * depth) end
  return 0, 0
end

-- A box of a x b tiles, `depth` cm deep, as a call to the scene's soft-box (rounded) or slab (square) utility; `mods` become its children.
function Kit:prism(mat, p, a, b, depth, col, rot, mods)
  local rad, bev = rounded(mat, a, b, depth)
  local size = {{"width", f(a * TILE)}, {"height", f(b * TILE)}, {"depth", f(depth)}}
  local tag, extra = "slab", {{"bevel", 0}}
  if rad ~= 0 then tag, extra = "soft-box", {{"radius", f(rad)}, {"bevel", f(bev)}} end
  local head = fmt('<%s%s%s%s%s%s', tag, attrs(size), attrs(extra), attrs({{"pos", p}}), attrs(rot or {}), attrs(self:look(mat, col)))
  self:emit((mods and mods ~= "") and fmt('%s>%s</%s>', head, mods, tag) or head .. "/>")
end

function Kit:box(mat, x, y, z, sx, sy, sz, col, rz)
  rz = rz or 0
  self:prism(mat, v3(x * TILE, (z + sz / 2) * TILE, -y * TILE), sx, sz, sy * TILE, col, rz ~= 0 and {{"rot", v3(0, deg(rz), 0)}} or {})
end

function Kit:beam(mat, a, b, w, d, col)
  local dx, dy, dz = b[1] - a[1], b[2] - a[2], b[3] - a[3]
  local L = sqrt(dx * dx + dy * dy + dz * dz)
  local yaw, pitch = atan(dy, dx), atan(dz, sqrt(dx * dx + dy * dy))
  local mx, my, mz = (a[1] + b[1]) / 2, (a[2] + b[2]) / 2, (a[3] + b[3]) / 2
  self:open("group", {{"pos", v3(mx * TILE, mz * TILE, -my * TILE)}, {"rot", v3(0, deg(yaw), 0)}})
  self:prism(mat, "0 0 0", L, d, w * TILE, col, {{"rot", v3(0, 0, deg(pitch))}})
  self:close("group")
end

-- A circle extruded along the tube and tapered from radius r0 at a to r1 at b.
function Kit:tube(mat, a, b, r0, r1, col, n)
  n = n or 10
  local dx, dy, dz = b[1] - a[1], b[2] - a[2], b[3] - a[3]
  local L = sqrt(dx * dx + dy * dy + dz * dz)
  local mx, my, mz = (a[1] + b[1]) / 2, (a[2] + b[2]) / 2, (a[3] + b[3]) / 2
  local ratio = r1 / r0
  local t = (ratio - 1) / (ratio + 1)
  local bev = min(.35 * min(r0, r1) * TILE, SOLID_ROUND * L * TILE)
  local mods = (bev >= .6 and fmt('<bevel amount="%s" bevelSegments="2"/>', f(bev)) or "") ..
    (abs(t) > .001 and fmt('<taper amount="%s" axis="z"/>', f4(t)) or "")
  local tilt = deg(acos(max(-1, min(1, dz / L))))
  local p = v3(mx * TILE, mz * TILE, -my * TILE)
  local circle = {{"radius", f(r0 * TILE / (1 - t))}, {"segments", max(8, n)}}
  if tilt < .01 then
    self:solid("circle", circle, L * TILE, p, mat, col, {{"rot", "-90 0 0"}}, mods)
    return
  end
  self:open("group", {{"pos", p}, {"rot", v3(0, deg(atan(dy, dx)), 0)}})
  self:open("group", {{"rot", v3(0, 0, -tilt)}})
  self:solid("circle", circle, L * TILE, "0 0 0", mat, col, {{"rot", "-90 0 0"}}, mods)
  self:close("group")
  self:close("group")
end

-- Revolves a profile of {radius, height} cm pairs about the vertical axis; identical profiles share one shape.
function Kit:revolve(prof, p, scale, segments, look, rot)
  local parts = {}
  for i, q in ipairs(prof) do parts[i] = f(q[1] < .3 and 0 or q[1]) .. "," .. f(q[2]) end
  local key = table.concat(parts, " ")
  local sid = self.shapes.ids[key]
  if not sid then
    sid = "lathe" .. #self.shapes.order
    self.shapes.ids[key] = sid
    self.shapes.order[#self.shapes.order + 1] = key
  end
  self:emit(fmt('<lathe shape="%s" segments="%d"%s%s%s/>', sid, segments,
    attrs(cat({{"pos", p}}, scale and {{"scale", scale}} or {})), attrs(rot or {}), attrs(look)))
end

function Kit:lathe(mat, x, y, z, prof, col, n, sx, sy)
  n, sx, sy = n or 18, sx or 1, sy or 1
  if type(col) == "function" then col = col(prof[#prof // 2 + 1][2]) end
  local cm = {}
  for i, q in ipairs(prof) do cm[i] = {q[1] * TILE, q[2] * TILE} end
  self:revolve(cm, v3(x * TILE, z * TILE, -y * TILE), (sx ~= 1 or sy ~= 1) and v3(sx, 1, sy) or nil, n, self:look(mat, col))
end

-- A sphere is a revolved half-circle: one 100 cm profile shared by all, scaled to the radii.
function Kit:ball(mat, x, y, z, r, col, n, sx, sy, sz, rot)
  n, sx, sy, sz = n or 12, sx or 1, sy or 1, sz or 1
  if type(col) == "function" then col = col(r) end
  local rings = max(4, n // 2)
  local prof = {}
  for i = 0, rings do prof[i + 1] = {HALF * sin(pi * i / rings), HALF * (1 - cos(pi * i / rings))} end
  local k = TILE / HALF
  self:revolve(prof, v3(x * TILE, (z - r * sz) * TILE, -y * TILE), v3(r * sx * k, r * sz * k, r * sy * k), n, self:look(mat, col), rot and {{"rot", rot}} or {})
end

-- A flat polygon standing in a vertical plane; pts[2] fixes the plane's direction.
function Kit:poly(mat, pts, col)
  local x0, y0, z0 = pts[1][1], pts[1][2], pts[1][3]
  local ang = atan(pts[2][2] - y0, pts[2][1] - x0)
  local c, s = cos(ang), sin(ang)
  local flat = {}
  for i, p in ipairs(pts) do
    assert(abs(-(p[1] - x0) * s + (p[2] - y0) * c) < 1e-6, "poly must stand in a vertical plane")
    flat[i] = {(p[1] - x0) * c + (p[2] - y0) * s, p[3] - z0}
  end
  self:open("group", {{"pos", v3(x0 * TILE, z0 * TILE, -y0 * TILE)}, {"rot", v3(0, deg(ang), 0)}})
  self:open("shape", cat({{"closed", 1}}, self:look(mat, col)))
  for _, p in ipairs(flat) do self:emit(fmt('<v x="%s" y="%s"/>', f(p[1] * TILE), f(p[2] * TILE))) end
  self:emit(fmt('<extrude amount="%s"/>', f(THIN)))
  self:close("shape")
  self:close("group")
end

-- A battered block: a box tapered towards its top. A taper scales both axes alike, so ty is not honoured.
function Kit:frustum(mat, x, y, z, sx, sy, h, tx, ty, col, rz)
  local ratio = (sx - 2 * tx) / sx
  local a = (ratio - 1) / (ratio + 1)
  local k = 1 / (1 - a)
  self:prism(mat, v3(x * TILE, (z + h / 2) * TILE, -y * TILE), sx * k, h, sy * k * TILE, col,
    (rz and rz ~= 0) and {{"rot", v3(0, deg(rz), 0)}} or {}, fmt('<taper amount="%s"/>', f4(a)))
end

-- Four revolved segments turned a quarter step, so the base is square.
function Kit:pyramid(mat, x, y, z, s, h, col)
  self:revolve({{s * TILE / sqrt(2), 0}, {0, h * TILE}}, v3(x * TILE, z * TILE, -y * TILE), nil, 4, self:look(mat, col), {{"rot", "0 45 0"}})
end

-- Moves a plant inward so its reach stays on the footprint.
function Kit:fit(x, y, reach)
  local lx, ly = max(self.w / 2 - reach, 0), max(self.h / 2 - reach, 0)
  return max(-lx, min(lx, x)), max(-ly, min(ly, y))
end

function Kit:blob(x, y, z, r, leaves)
  self:ball("leaf", x, y, z, r, M.J(Rng.choice(leaves), .04), 10)
end

function Kit:puff(x, y, z, leaves, r)
  r = (r or .25) * 1.25
  x, y = self:fit(x, y, r * 1.17)
  for i = 0, 2 do
    local a = 2 * pi * i / 3 + Rng.uniform(0, 1)
    self:blob(x + r * .45 * cos(a), y + r * .45 * sin(a), z + r * .55, r * .72, leaves)
  end
  self:blob(x, y, z + r * 1.05, r * .75, leaves)
end

function Kit:palm(x, y, z, trunk, leaves, h, L, fronds)
  h, L, fronds = h or 1, L or .5, fronds or 6
  x, y = self:fit(x, y, L * 1.02)
  self:tube("wood", {x, y, z}, {x, y, z + h}, .09, .06, trunk, 10)
  for i = 0, fronds - 1 do
    local a = 2 * pi * i / fronds + Rng.uniform(-.15, .15)
    local base = M.J(Rng.choice(leaves), .04)
    self:open("group", {{"pos", v3(x * TILE, (z + h) * TILE, -y * TILE)}, {"rot", v3(0, deg(a), 0)}})
    self:ball("leaf", L * .5, 0, -L * .1, 1, base, 8, L * .52, L * .17, L * .04, "0 0 -22")
    self:close("group")
  end
  self:blob(x, y, z + h, .08, leaves)
end

function Kit:light(x, y, z, watts, color)
  self.lights[#self.lights + 1] = {x, y, z, watts, color or {1, .62, .28}}
end

function Kit:crate(x, y, z, s, wood, wood_l, h, rz)
  h = h or s
  self:box("wood", x, y, z, s, s, h, M.J(wood_l, .05), rz)
  self:box("wood", x, y, z + h, s + .05, s + .05, .06, wood, rz)
end

function Kit:basket(x, y, z, base, fruit, r)
  r = r or .2
  self:lathe("matte", x, y, z, {{r * .62, 0}, {r, r * .55}, {r * 1.05, r * .85}, {r * .85, r * .88}}, M.J(base, .05), 16)
  local col = Rng.choice(fruit)
  for i = 0, 2 do
    local a = 2 * pi * i / 3
    self:ball("matte", x + r * .42 * cos(a), y + r * .42 * sin(a), z + r * .85, r * .4, M.J(col, .05), 10)
  end
end

function Kit:finish()
  for _, l in ipairs(self.lights) do
    self:emit(fmt('<light pos="%s" color="%s" intensity="%s" radius="%s" castShadows="0"/>',
      pos(l[1], l[2], l[3]), rgb(l[5]), f(.35 * l[4]), f(TILE * 1.4)))
  end
  return self.lines
end

-- A frame on a wall's outer surface: x along it, -y out. Returns x, y, angle.
function M.face(a, b, t)
  t = t or .24
  local ang = atan(b[2] - a[2], b[1] - a[1])
  return a[1] + t / 2 * sin(ang), a[2] - t / 2 * cos(ang), ang
end

-- Builds every item with builders[key] on a shelf of whole-tile cells; returns shapes and body.
function M.assemble(items, builders, slab_color)
  local shapes, body, u, v, row_h = {ids = {}, order = {}}, {}, 0, 0, 0
  for i, it in ipairs(items) do
    Rng.seed(7 + (i - 1))
    local k = M.new_kit(it.w, it.h, shapes)
    k:box("slab", 0, 0, 0, it.w, it.h, G, slab_color)
    builders[it.key](k)
    if u + it.w > 12 then u, v, row_h = 0, v + row_h + 2, 0 end
    it.at = {u + it.w / 2, v + it.h / 2}
    body[#body + 1] = {it, k:finish()}
    u, row_h = u + it.w + 2, max(row_h, it.h)
  end
  return shapes, body
end

M.f, M.v3 = f, v3
return M
