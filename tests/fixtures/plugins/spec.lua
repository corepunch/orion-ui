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

-- The production Ledger renderer and Lodge use this same plugin.
local filters = Plugins.use("template_filters", 1)
local escape = Plugins.use("text_escape", 1)
assert(escape.apply('a"b', "csv") == 'a""b')
assert(escape.apply('<raw>', "none") == '<raw>')
assert(filters.apply("2026-03-10", "dmy") == "10.03.2026")
assert(filters.apply("2026-03-10", "ymd") == "20260310")
assert(filters.apply("2026-03-10", "ddmm") == "1003")
assert(filters.apply("12.5", "decimal3comma") == "12,500")
assert(filters.apply("0", "zero_decimal3comma") == "—")
assert(filters.apply("", "dash") == "—")
local document = {title = '<Invoice>', lines = {
  {name = 'a"b', amount = 12.5, tax = {rate = "19.00"}},
  {name = 'Other', amount = 0, tax = {rate = "7.00"}},
}}
assert(template.render('{{title}}{{#lines tax.rate=19}}{{n}}:{{name}}={{amount|comma}}{{/lines}}', document) ==
  '&lt;Invoice&gt;1:a&quot;b=12,50')
assert(template.render('{{#lines tax.rate!=19}}{{name}}{{/lines}}', document) == 'Other')
assert(template.render('{{#if title!=}}{{title}}{{/if}}', document) == '&lt;Invoice&gt;')
assert(template.render('{{#lines}}{{#if amount=0}}zero{{/if}}{{/lines}}', document) == 'zero')
assert(template.render('{{#rows}}{{name}}{{#rows}}{{name}}{{/rows}}{{/rows}}',
  {rows = {{name = 'outer', rows = {{name = 'inner'}}}}}) == 'outerinner')
assert(template.render('"{{name}}"', {name = 'a"b'}, {format = 'csv'}) == '"a""b"')
assert(template.render('{{name}}', {name = '<raw>'}, {format = 'none'}) == '<raw>')
assert(not pcall(template.render, '{{#lines}}broken', document))
assert(not pcall(template.render, '{{#lines}}{{/other}}', document))
assert(not pcall(template.render, '{{name', document))
assert(not pcall(template.render, '{{#missing}}{{name}}{{/missing}}', document))
local normalize = Plugins.use('text_normalize', 1).new({['Ä'] = 'ä'})
assert(normalize('AÄ\194\160B') == 'aä b')
local lexicon = Plugins.use('lexicon', 1)
local options = {schemas = {actions = 'groups'}, normalize = normalize}
local dictionary = lexicon.compile({actions = {reserve = {'BOOK', 'book room'}, cancel = {'cancel'}}}, options)
local entry, consumed = dictionary:lookup({{text = 'book'}, {text = 'room'}}, 1)
assert(consumed == 2 and entry.roles.actions == 'reserve')
assert(not pcall(lexicon.compile, {actions = {one = {'BOOK'}, two = {'book'}}}, options))
