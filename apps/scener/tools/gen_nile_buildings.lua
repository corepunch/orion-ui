#!/usr/bin/env lua
-- Generate nile_buildings.blks: six Nile Archive buildings, ported from the Blender scripts.
--   lua apps/scener/tools/gen_nile_buildings.lua [out.blks]
-- The Lua twin of gen_nile_buildings.py; both write the same file.
local here = (arg[0]:match("^(.*)/[^/]*$") or ".")
package.path = here .. "/?.lua;" .. package.path
local K = require("building_kit")
local C, J, f, v3, face, TILE, G = K.C, K.J, K.f, K.v3, K.face, K.TILE, K.G
local cos, sin = math.cos, math.sin

local SLAB = C("#CDB283")
local LIME, LIME_D, LIME_L = C("#E6D3A3"), C("#C7AC78"), C("#F3E6C2")
local MUD, MUD_L, WASH = C("#B98B5C"), C("#D3A976"), C("#F1E7CF")
local WOOD, WOOD_L, WOOD_D = C("#8E5427"), C("#B47436"), C("#5A3216")
local CLAY, CLAY_L, POT = C("#C94E2C"), C("#E06A3A"), C("#B5613A")
local TEAL, LAPIS, OCHRE = C("#16A296"), C("#2B5CAE"), C("#B9552F")
local GOLD, FLAME, DOOR = C("#E8AE2C"), C("#FF7A0A"), C("#3A2614")
local LINEN, INK, DATE, BASKET = C("#F0E4C6"), C("#2B2420"), C("#C8641E"), C("#C39146")
local LEAVES, FRUIT = {}, {}
for _, h in ipairs {"#3FB23C", "#55C43F", "#6FD043"} do LEAVES[#LEAVES + 1] = C(h) end
for _, h in ipairs {"#D9402E", "#EE8A1E", "#7DBB33", "#E9C32E", "#C2352E"} do FRUIT[#FRUIT + 1] = C(h) end

local function puff(k, x, y, z, r) k:puff(x, y, z, LEAVES, r) end
local function crate(k, x, y, z, s, h, rz) k:crate(x, y, z, s, WOOD, WOOD_L, h, rz) end
local function basket(k, x, y, z, r) k:basket(x, y, z, BASKET, FRUIT, r) end

-- The flared top of an Egyptian wall: a painted band and a pale lip.
local function cornice(k, x, y, z, sx, sy, band)
  k:box("glaze", x, y, z, sx + .06, sy + .06, .08, band or TEAL)
  k:box("stone", x, y, z + .08, sx + .14, sy + .14, .1, LIME_L)
end

-- A dark door on the outside of wall a-b with a pale frame and a lintel.
local function doorway(k, a, b, s, w, h, z)
  z = z or 0
  local fx, fy, fa = face(a, b, 0)
  k:at(fx, fy, fa, function()
    k:box("matte", s, -.012, z, w, .03, h, DOOR)
    for _, side in ipairs {-1, 1} do k:box("stone", s + side * (w / 2 + .05), -.02, z, .1, .05, h, LIME_L) end
    k:box("stone", s, -.03, z + h, w + .26, .07, .12, LIME_L)
    k:box("glaze", s, -.05, z + h + .02, w * .6, .02, .07, LAPIS)
  end)
end

local function slot(k, a, b, s, z, w, h, col)
  local fx, fy, fa = face(a, b, 0)
  k:at(fx, fy, fa, function() k:box("matte", s, -.012, z, w or .12, .03, h or .22, col or DOOR) end)
end

-- A painted panel on a wall: three figures as coloured blocks.
local function relief(k, a, b, s, z, w, h, cols)
  cols = cols or {OCHRE, LAPIS, GOLD}
  local fx, fy, fa = face(a, b, 0)
  k:at(fx, fy, fa, function()
    k:box("matte", s, -.01, z, w, .02, h, J(LIME_L, .02))
    for i, c in ipairs(cols) do
      local px = s + ((i - 1) - (#cols - 1) / 2) * w / (#cols + .4)
      k:box("matte", px, -.022, z + h * .14, w * .13, .02, h * .52, c)
      k:ball("matte", px, -.026, z + h * .76, w * .07, INK, 8, 1, .3)
    end
  end)
end

local function flag(k, x, y, z, h, col, d)
  d = d or {0, -1}
  k:tube("wood", {x, y, z}, {x, y, z + h}, .035, .03, WOOD_D, 8)
  k:ball("gold", x, y, z + h + .02, .05, GOLD, 10)
  k:poly("cloth", {{x, y, z + h - .04}, {x + d[1] * .42, y + d[2] * .42, z + h - .14}, {x, y, z + h - .36}}, col)
end

local function jar(k, x, y, z, r, col, band)
  r = r or .12
  k:lathe("clay", x, y, z, {{r * .45, 0}, {r, r * .8}, {r * .95, r * 1.5}, {r * .45, r * 2.1}, {r * .5, r * 2.35}, {r * .38, r * 2.4}}, col or J(POT, .06), 14)
  if band then k:lathe("glaze", x, y, z + r * 1.15, {{r * 1.01, 0}, {r * 1.01, r * .22}}, band, 14) end
end

local function date_palm(k, x, y, z, h, L, fronds)
  h, L, fronds = h or 1, L or .5, fronds or 7
  k:palm(x, y, z, WOOD, LEAVES, h, L, fronds)
  x, y = k:fit(x, y, L * 1.02)
  for _, a in ipairs {.6, 2.6, 4.5} do k:ball("matte", x + .08 * cos(a), y + .08 * sin(a), z + h - .1, .06, DATE, 8, 1, 1, 1.4) end
end

local B = {}

function B.small_pyramid(k)
  for i = 0, 3 do
    local s = 1.66 - .36 * i
    k:box("stone", 0, 0, G + i * .3, s, s, .3, J(LIME, .02))
  end
  k:pyramid("gold", 0, 0, G + 1.2, .3, .3, GOLD)
  doorway(k, {.83, -.83}, {.83, .83}, .83, .3, .26, G)
  puff(k, .8, -.8, G, .14)
  puff(k, -.8, -.82, G, .13)
  date_palm(k, -.72, .72, G, .8, .38, 6)
end

function B.mud_house(k)
  k:box("stone", 0, .1, G, 1.3, 1.3, 1.0, J(WASH, .02))
  k:box("stone", 0, .1, G + 1.0, 1.38, 1.38, .07, LIME_D)
  k:box("stone", -.36, .44, G + 1.07, .5, .58, .5, J(WASH, .02))
  k:box("cloth", .25, -.1, G + 1.45, .8, .8, .04, TEAL)
  for _, p in ipairs {{-.1, -.45}, {.6, -.45}, {.6, .35}} do k:tube("wood", {p[1], p[2], G + 1.07}, {p[1], p[2], G + 1.45}, .025, .025, WOOD_D, 6) end
  doorway(k, {.65, -.55}, {.65, .75}, .55, .28, .58, G)
  slot(k, {-.65, -.55}, {.65, -.55}, .35, G + .55, .18, .2)
  slot(k, {-.65, -.55}, {.65, -.55}, .95, G + .55, .18, .2)
  for i = 0, 4 do k:box("stone", -.55 + i * .2, -.72, G, .2, .22, .2 + i * .2, J(LIME_L, .02)) end
  jar(k, .8, -.75, G, .1, nil, LAPIS)
  date_palm(k, .75, .78, G, .9, .35, 6)
end

function B.pottery(k)
  k:box("stone", -.3, .35, G, 1.0, .9, .75, J(MUD_L, .03))
  k:box("stone", -.3, .35, G + .75, 1.06, .96, .06, MUD)
  k:box("cloth", -.3, -.3, G + .6, 1.0, .5, .04, OCHRE)
  for _, x in ipairs {-.75, .15} do k:tube("wood", {x, -.5, G}, {x, -.5, G + .6}, .03, .03, WOOD_D, 6) end
  doorway(k, {-.8, -.1}, {.2, -.1}, .7, .26, .5, G)
  k:lathe("clay", .5, .45, G, {{.38, 0}, {.36, .3}, {.22, .62}, {.08, .72}, {.001, .74}}, J(CLAY, .03), 16)
  k:box("glow", .5, .07, G + .1, .2, .04, .18, FLAME)
  k:light(.5, -.05, G + .2, .8)
  k:lathe("wood", .3, -.45, G, {{.06, 0}, {.06, .26}, {.2, .26}, {.2, .3}}, WOOD_L, 14)
  k:lathe("clay", .3, -.45, G + .3, {{.08, 0}, {.1, .1}, {.05, .16}}, CLAY_L, 12)
  -- a false entry stands in for Python's None band
  for _, j in ipairs {{-.8, -.8, .1, TEAL}, {.75, -.75, .09, false}, {.8, -.4, .08, LAPIS}, {-.55, -.78, .08, false}} do
    jar(k, j[1], j[2], G, j[3], nil, j[4] or nil)
  end
end

function B.bakery(k)
  k:box("stone", -.35, .2, G, 1.7, 1.1, .85, J(MUD_L, .03))
  cornice(k, -.35, .2, G + .85, 1.7, 1.1, OCHRE)
  doorway(k, {-1.2, -.35}, {.5, -.35}, .85, .3, .55, G)
  slot(k, {-1.2, -.35}, {.5, -.35}, .35, G + .45, .2, .18)
  slot(k, {-1.2, -.35}, {.5, -.35}, 1.35, G + .45, .2, .18)
  for _, y in ipairs {-.4, .35} do
    k:lathe("clay", 1.0, y, G, {{.3, 0}, {.29, .2}, {.18, .45}, {.001, .52}}, J(CLAY, .03), 16)
    k:box("glow", 1.29, y, G + .06, .04, .16, .14, FLAME)
    k:light(1.36, y, G + .14, .6)
  end
  k:box("wood", .3, -.72, G, .6, .3, .3, WOOD_L)
  for i = 0, 3 do k:ball("matte", .12 + i * .12, -.72, G + .33, .06, C("#D39446"), 10, 1, 1, .6) end
  basket(k, -.9, -.7, G, .14)
  k:box("cloth", -.35, -.55, G + .62, 1.6, .4, .04, LINEN)
end

function B.watchtower(k)
  k:box("stone", 0, 0, G, 1.5, 1.5, .12, LIME_L)
  k:frustum("stone", 0, 0, G + .12, 1.15, 1.15, 1.9, .1, .1, J(LIME_D, .02))
  k:box("stone", 0, 0, G + 2.02, 1.05, 1.05, .1, LIME)
  for _, s in ipairs {-.36, 0, .36} do
    for _, p in ipairs {{s, -.45}, {s, .45}, {-.45, s}, {.45, s}} do
      if not (p[1] == 0 and p[2] == 0) then k:box("stone", p[1], p[2], G + 2.12, .18, .18, .18, LIME_L) end
    end
  end
  flag(k, .2, .2, G + 2.12, .7, LAPIS, {1, 0})
  doorway(k, {-.57, -.57}, {.57, -.57}, .57, .3, .5, G + .12)
  slot(k, {.55, -.55}, {.55, .55}, .55, G + 1.3, .1, .3)
  slot(k, {-.55, -.55}, {.55, -.55}, .55, G + 1.3, .1, .3)
  crate(k, -.6, -.62, G, .22)
end

function B.pylon_gate(k)
  for _, x in ipairs {-.85, .85} do
    k:frustum("stone", x, 0, G, 1.1, 1.1, 1.85, .16, .1, J(LIME, .02))
    cornice(k, x, 0, G + 1.85, .78, .9, TEAL)
    relief(k, {x - .5, -.5}, {x + .5, -.5}, .5, G + .5, .58, .9)
    flag(k, x + (x > 0 and .32 or -.32), -.62, G, 2.0, (x < 0 and OCHRE or LAPIS), {x < 0 and -1 or 1, 0})
  end
  k:box("stone", 0, 0, G, .64, .8, 1.25, LIME_D)
  cornice(k, 0, 0, G + 1.25, .6, .8, LAPIS)
  doorway(k, {-.32, -.4}, {.32, -.4}, .32, .36, .85, G)
  k:ball("gold", 0, -.44, G + 1.36, .1, GOLD, 12, 1, .3)
  for _, s in ipairs {-1, 1} do k:box("glaze", s * .2, -.44, G + 1.38, .26, .03, .06, LAPIS) end
end

local ITEMS = {}
for _, r in ipairs {
  {"small_pyramid", "Small pyramid", 2, 2}, {"mud_house", "Mud-brick house", 2, 2}, {"pottery", "Pottery", 2, 2},
  {"bakery", "Bakery", 3, 2}, {"watchtower", "Watchtower", 2, 2}, {"pylon_gate", "Pylon gate", 3, 2}} do
  ITEMS[#ITEMS + 1] = {key = r[1], title = r[2], w = r[3], h = r[4]}
end

local HEAD = [[<?xml version="1.0" encoding="UTF-8"?>
<!-- Six Nile Archive buildings on their paved bases, ported from the Blender building scripts
     (nile-archive tools/building_kit.py). Generated by tools/gen_nile_buildings.py or .lua; each building
     is a named group you can copy into another scene. -->
<scene ambient="0.55 0.54 0.58" background="0.62 0.72 0.95">
  <include src="utilities.xml"/>
  <sun dir="-0.4 -1 -0.9" color="1.0 0.93 0.80" intensity="0.95" castShadows="1"/>
  <sun dir="1.0 -0.5 0.6" color="1.0 0.88 0.72" intensity="0.22" castShadows="0"/>
  <material id="grass" color="0.36 0.54 0.24" shininess="3"/>]]

local GROUND_MARGIN = 3

local function main(path)
  local shapes, body = K.assemble(ITEMS, B, SLAB)
  local x0, x1, v0, v1 = math.huge, -math.huge, math.huge, -math.huge
  for _, e in ipairs(body) do
    local it = e[1]
    x0, x1 = math.min(x0, it.at[1] - it.w / 2), math.max(x1, it.at[1] + it.w / 2)
    v0, v1 = math.min(v0, it.at[2] - it.h / 2), math.max(v1, it.at[2] + it.h / 2)
  end
  x0, x1, v0, v1 = x0 - GROUND_MARGIN, x1 + GROUND_MARGIN, v0 - GROUND_MARGIN, v1 + GROUND_MARGIN
  local cx, cv = (x0 + x1) / 2, (v0 + v1) / 2
  local out = {HEAD}
  local W, D = (x1 - x0) * TILE, (v1 - v0) * TILE
  out[#out + 1] = string.format('  <camera name="Overview" comment="All buildings, three-quarter view from the front corner" pos="%s" look="%s" fov="30"/>',
    v3(W * .2, W * .42, D * .5 + W * .42), v3(0, 40, 0))
  for _, e in ipairs(body) do
    local it = e[1]
    local bx, bz = (it.at[1] - cx) * TILE, (it.at[2] - cv) * TILE
    out[#out + 1] = string.format('  <camera name="%s" comment="%s" pos="%s" look="%s" fov="32"/>', it.title, it.title, v3(bx + 420, 420, bz + 700), v3(bx, 90, bz))
  end
  for i, key in ipairs(shapes.order) do
    local vs = {}
    for p in key:gmatch("%S+") do
      local x, y = p:match("^([^,]+),(.+)$")
      vs[#vs + 1] = x .. " " .. y
    end
    out[#out + 1] = string.format('  <shape id="%s" closed="0" v="%s"/>', shapes.ids[key], table.concat(vs, " "))
  end
  out[#out + 1] = string.format('  <box pos="0 -8 0" size="%s 16 %s" material="grass"/>', f(W), f(D))
  for _, e in ipairs(body) do
    local it = e[1]
    out[#out + 1] = string.format('  <group name="%s" pos="%s">', it.key, v3((it.at[1] - cx) * TILE, 0, (it.at[2] - cv) * TILE))
    for _, l in ipairs(e[2]) do out[#out + 1] = l end
    out[#out + 1] = "  </group>"
  end
  out[#out + 1] = "</scene>\n"
  local fh = assert(io.open(path, "w"))
  fh:write(table.concat(out, "\n"))
  fh:close()
  print(string.format("wrote %s: %d buildings", path, #body))
end

main(arg[1] or (here .. "/../scenes/nile_buildings.blks"))
