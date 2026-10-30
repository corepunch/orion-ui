-- Static checks for every Lua source (`make lua-check`; CI runs it too).
-- Sandbox-hosted code (share/lua, add-ons, controllers, plugins, their tests)
-- is module-style: it returns tables and never assigns globals, which the
-- runtime also refuses. Ledger's configuration host is different: it loads
-- rules.lua, stdplugs and plugins into one state with the `ledger` module as
-- a global and finds posting, tax and closing functions by their global names.
std = "lua54"
self = false              -- an unused `self` in a method is not a mistake
unused_args = false       -- hooks and callbacks keep the signature they are called with
max_line_length = false   -- compact one-line cases are the house style
exclude_files = {"build/**", "platform/**"}

-- 131: a rule defined here is found by name from C, so it looks unused.
local ledger_host = {globals = {"ledger"}, allow_defined_top = true, ignore = {"131"}}
files["apps/ledger/share/configurations"] = ledger_host
files["apps/ledger/share/stdplugs"] = ledger_host
files["apps/ledger/tests"] = ledger_host
-- Declarations for the editor, not code that runs.
files["apps/ledger/share/stdplugs/automation/ledger-api.d.lua"] = {ignore = {"1"}}
-- Scripts of the terminal app's own interpreter.
files["apps/foobar.lua"] = {std = "lua54"}
files["apps/interactive.lua"] = {std = "lua54"}
files["tests/test_*.lua"] = {std = "lua54"}
