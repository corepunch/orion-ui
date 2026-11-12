-- Validate untrusted plugin source without executing its factory.
local root = assert(arg[1], "missing package directory")
assert(not arg[2], "unexpected argument")
local function identifier(value)
  return type(value) == "string" and value:match("^[a-z][a-z0-9_]*$") ~= nil
end
local function positive_integer(value)
  return type(value) == "number" and math.type(value) == "integer" and value > 0
end
local chunk, problem = loadfile(root .. "/manifest.lua", "t", {})
assert(chunk, problem)
local remaining = 100000
local function budget()
  remaining = remaining - 1000
  if remaining <= 0 then error("manifest instruction limit exceeded", 0) end
end
debug.sethook(budget, "", 1000)
local ok, manifest = pcall(chunk)
debug.sethook()
assert(ok, manifest)
assert(type(manifest) == "table" and getmetatable(manifest) == nil, "manifest must be a plain table")
assert(identifier(manifest.id), "invalid plugin id")
assert(manifest.api == 1 and math.type(manifest.api) == "integer", "unsupported plugin API")
assert(positive_integer(manifest.version), "invalid plugin version")
assert(type(manifest.dependencies) == "table" and getmetatable(manifest.dependencies) == nil, "invalid dependencies")
local names = {}
for name, version in pairs(manifest.dependencies) do
  assert(identifier(name) and positive_integer(version), "invalid dependency")
  names[#names + 1] = name
end
table.sort(names)
local main, main_error = loadfile(root .. "/main.lua", "t", {})
assert(main, main_error)
io.write("ROOT\t", manifest.id, "\t1\t", manifest.version, "\n")
for _, name in ipairs(names) do
  io.write("DEP\t", name, "\t", manifest.dependencies[name], "\n")
end
