-- Run with Lua 5.4; manifests execute as data expressions in an empty environment.
local root, shared, external = assert(arg[1]), assert(arg[2]), arg[3]
debug.sethook(function() error("manifest instruction budget exceeded") end, "", 100000)
local Plugins = assert(loadfile(shared .. "/orion/plugins.lua", "t"))()
local function manifest(path)
  return assert(loadfile(path, "t", {}))()
end
local main = manifest(root .. "/manifest.lua")
assert(type(main) == "table" and type(main.id) == "string", "invalid plugin manifest")
assert(math.type(main.version) == "integer" and main.version > 0, "invalid plugin version")
local manager = Plugins.new(function(id, part)
  assert(part == "manifest", "inspection must not execute factories")
  local directory = id == main.id and root or shared .. "/orion/plugins/" .. id
  local file = io.open(directory .. "/manifest.lua", "r")
  if file then file:close()
  elseif external and external ~= "" then directory = external .. "/orion/plugins/" .. id end
  return manifest(directory .. "/manifest.lua")
end)
for _, item in ipairs(manager:inspect({[main.id] = main.version})) do
  io.write(item.id, "@", item.version, "\n")
end
