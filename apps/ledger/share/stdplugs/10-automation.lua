-- A shipped plugin uses the same entry-point convention as custom plugins.
local directory = assert(debug.getinfo(1, "S").source:match("^@(.*/)"))
assert(loadfile(directory .. "automation/init.lua"))()
