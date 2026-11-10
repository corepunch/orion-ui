-- Building kit for Scener, Lua edition. Same API and same output as building_kit.py: a building is a
-- function drawing into a Kit in pivot-local, whole-tile coordinates (X = u, Y = -v, Z up, 1 unit =
-- 1 tile) and the kit writes Scener XML (Y up, centimetres): (x, y, z) tiles -> (x, z, -y) * TILE.
-- Run under Lua 5.4: lua gen_nile_buildings.lua [out.blks]
local M = {}

local pi, sin, cos, sqrt, atan, acos, floor, abs, min, max =
  math.pi, math.sin, math.cos, math.sqrt, math.atan, math.acos, math.floor, math.abs, math.min, math.max
local fmt = string.format

local TILE, G = 160.0, 0.1
local SOLID_ROUND, THIN = 0.45, 1.5
M.TILE, M.G = TILE, G

-- material -> {shininess, bevel width in tiles, unlit}
local MATERIALS = {
  stone = {6, .04, false}, slab = {6, .04, false}, wood = {8, .03, false}, clay = {14, .03, false},
  glaze = {40, .028, false}, gold = {60, .022, false}, cloth = {3, .025, false}, hedge = {6, .07, false},
  leaf = {10, 0, false}, matte = {6, 0, false}, glass = {80, 0, false}, water = {70, 0, false},
  flame = {4, 0, true}, glow = {4, 0, true},
}

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
  local p = {{"color", rgb(col)}, {"shininess", m[1]}}
  if m[3] then p[#p + 1] = {"unlit", 1}; p[#p + 1] = {"castShadow", 0} end
  return p
end

local function pos(x, y, z) return v3(x * TILE, z * TILE, -y * TILE) end

function Kit:box(mat, x, y, z, sx, sy, sz, col, rz)
  rz = rz or 0
  local bev = MATERIALS[mat][2] * TILE
  local rad = min(bev, SOLID_ROUND * min(sx, sz) * TILE)
  local rot = rz ~= 0 and {{"rot", v3(0, deg(rz), 0)}} or {}
  local p = {"pos", v3(x * TILE, (z + sz / 2) * TILE, y * TILE * -1)}
  if rad < .8 or sy * TILE < 2.4 then
    self:emit(fmt('<box%s size="%s"%s%s/>', attrs({p}), v3(sx * TILE, sz * TILE, sy * TILE), attrs(rot), attrs(self:look(mat, col))))
    return
  end
  local b = min(.85 * rad, SOLID_ROUND * sy * TILE)
  self:emit(fmt('<rounded-rect%s size="%s %s" radius="%s" segments="4"%s%s><extrude amount="%s"/><bevel amount="%s" bevelSegments="3"/></rounded-rect>',
    attrs({p}), f(sx * TILE), f(sz * TILE), f(rad), attrs(rot), attrs(self:look(mat, col)), f(sy * TILE), f(b)))
end

function Kit:beam(mat, a, b, w, d, col)
  local dx, dy, dz = b[1] - a[1], b[2] - a[2], b[3] - a[3]
  local L = sqrt(dx * dx + dy * dy + dz * dz)
  local yaw, pitch = atan(dy, dx), atan(dz, math.sqrt(dx * dx + dy * dy))
  local mx, my, mz = (a[1] + b[1]) / 2, (a[2] + b[2]) / 2, (a[3] + b[3]) / 2
  self:open("group", {{"pos", v3(mx * TILE, mz * TILE, -my * TILE)}, {"rot", v3(0, deg(yaw), 0)}})
  self:emit(fmt('<box size="%s" rot="%s"%s/>', v3(L * TILE, d * TILE, w * TILE), v3(0, 0, deg(pitch)), attrs(self:look(mat, col))))
  self:close("group")
end

function Kit:tube(mat, a, b, r0, r1, col, n, smooth)
  n = n or 10
  local dx, dy, dz = b[1] - a[1], b[2] - a[2], b[3] - a[3]
  local L = sqrt(dx * dx + dy * dy + dz * dz)
  local mx, my, mz = (a[1] + b[1]) / 2, (a[2] + b[2]) / 2, (a[3] + b[3]) / 2
  local tail = (smooth == false and ' smooth="0"' or "")
  local cone = fmt('<cone%%s radius="%s" radiusTop="%s" height="%s" sides="%d"%s%s/>', f(r0 * TILE), f(r1 * TILE), f(L * TILE), n, tail, attrs(self:look(mat, col)))
  local tilt = deg(acos(max(-1, min(1, dz / L))))
  if tilt < .01 then
    self:emit(fmt(cone, fmt(' pos="%s"', pos(mx, my, mz))))
    return
  end
  self:open("group", {{"pos", v3(mx * TILE, mz * TILE, -my * TILE)}, {"rot", v3(0, deg(atan(dy, dx)), 0)}})
  self:emit(fmt(cone, fmt(' rot="%s"', v3(0, 0, -tilt))))
  self:close("group")
end

function Kit:lathe(mat, x, y, z, prof, col, n, sx, sy)
  n, sx, sy = n or 18, sx or 1, sy or 1
  local parts = {}
  for i, p in ipairs(prof) do
    local r = p[1] * TILE
    parts[i] = f(r < .3 and 0 or r) .. "," .. f(p[2] * TILE)
  end
  local key = table.concat(parts, " ")
  local sid = self.shapes.ids[key]
  if not sid then
    sid = "lathe" .. #self.shapes.order
    self.shapes.ids[key] = sid
    self.shapes.order[#self.shapes.order + 1] = key
  end
  local scale = (sx ~= 1 or sy ~= 1) and {{"scale", v3(sx, 1, sy)}} or {}
  if type(col) == "function" then col = col(prof[#prof // 2 + 1][2]) end
  self:emit(fmt('<lathe shape="%s" segments="%d" pos="%s"%s%s/>', sid, n, pos(x, y, z), attrs(scale), attrs(self:look(mat, col))))
end

function Kit:ball(mat, x, y, z, r, col, n, sx, sy, sz)
  n, sx, sy, sz = n or 12, sx or 1, sy or 1, sz or 1
  if type(col) == "function" then col = col(r) end
  self:emit(fmt('<ellipsoid pos="%s" radii="%s" rings="%d" slices="%d"%s/>',
    pos(x, y, z), v3(r * sx * TILE, r * sz * TILE, r * sy * TILE), max(4, n // 2), n, attrs(self:look(mat, col))))
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

-- A battered block: the top is inset by tx on each side (Scener tapers both axes alike).
function Kit:frustum(mat, x, y, z, sx, sy, h, tx, ty, col, rz)
  local ratio = (sx - 2 * tx) / sx
  local a = (ratio - 1) / (ratio + 1)
  local k = 1 / (1 - a)
  local rot = (rz and rz ~= 0) and {{"rot", v3(0, deg(rz), 0)}} or {}
  self:emit(fmt('<box pos="%s" size="%s"%s%s><taper amount="%s"/></box>',
    v3(x * TILE, (z + h / 2) * TILE, -y * TILE), v3(sx * k * TILE, h * TILE, sy * k * TILE), attrs(rot), attrs(self:look(mat, col)), f(a)))
end

function Kit:pyramid(mat, x, y, z, s, h, col)
  self:emit(fmt('<pyramid pos="%s" radius="%s" height="%s" rot="0 45 0"%s/>',
    v3(x * TILE, (z + h / 2) * TILE, -y * TILE), f(s * TILE / sqrt(2)), f(h * TILE), attrs(self:look(mat, col))))
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
    self:emit(fmt('<ellipsoid pos="%s" radii="%s" rot="0 0 -22" rings="4" slices="8"%s/>',
      v3(L * .5 * TILE, -L * .1 * TILE, 0), v3(L * .52 * TILE, L * .04 * TILE, L * .17 * TILE), attrs(self:look("leaf", base))))
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
