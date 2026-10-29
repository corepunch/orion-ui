local Plugins = require("orion.plugins")
local calendar = Plugins.use("calendar", 1)
assert(calendar.between("2024-02-28", "2024-03-01") == 2)
assert(calendar.between("2026-03-28", "2026-03-30") == 2)
assert(calendar.add("2024-02-28", 2) == "2024-03-01")
assert(calendar.add("2026-01-01", -1) == "2025-12-31")
assert(not pcall(calendar.day, "2026-02-29"))
assert(not pcall(calendar.day, "2026-13-01"))
local intervals = Plugins.use("intervals", 1)
assert(intervals.overlap("2026-01-01", "2026-01-03", "2026-01-02", "2026-01-04"))
assert(not intervals.overlap("2026-01-01", "2026-01-03", "2026-01-03", "2026-01-04"))
assert(not intervals.valid("2026-01-03", "2026-01-03"))
local money = Plugins.use("money", 1)
assert(money.minor(1.005) == 101 and money.minor(-1.005) == -101)
assert(not pcall(money.minor, math.huge))
local template = Plugins.use("template", 1)
assert(template.render("{{name}}", {name = '<script a="x">&'}) == '&lt;script a=&quot;x&quot;&gt;&amp;')
assert(not pcall(template.render, "{{missing}}", {}))
assert(Plugins.use("calendar", 1) == calendar)
assert(not pcall(Plugins.use, "calendar", 2))
assert(not pcall(Plugins.use, "../calendar"))

local manifests, factories, calls = {}, {}, {}
local function define(id, deps)
  manifests[id] = {id = id, api = 1, version = 1, dependencies = deps or {}}
  factories[id] = function(dependencies) calls[#calls + 1] = id; return {dependencies = dependencies} end
end
local manager = Plugins.new(function(id, part)
  assert(manifests[id], "missing plugin: " .. id)
  return part == "manifest" and manifests[id] or factories[id]
end)
define("a", {b = 1}); define("b")
assert(manager:use("a").dependencies.b == manager:use("b"))
assert(table.concat(calls) == "ba")
define("cycle_a", {cycle_b = 1}); define("cycle_b", {cycle_a = 1})
assert(not pcall(manager.use, manager, "cycle_a"))
manifests.cycle_b.dependencies = {}
assert(manager:use("cycle_a")) -- failed loading clears visiting state
assert(not pcall(manager.use, manager, "missing"))
define("future"); manifests.future.api = 2
assert(not pcall(manager.use, manager, "future"))
define("bad", {b = 2})
assert(not pcall(manager.use, manager, "bad"))
define("broken"); factories.broken = function() error("factory failed") end
assert(not pcall(manager.use, manager, "broken"))
factories.broken = function() return {} end
assert(manager:use("broken"))
