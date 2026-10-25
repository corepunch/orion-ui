local directory = assert(debug.getinfo(1, "S").source:match("^@(.*/)"))
local policies = assert(loadfile(directory .. "documents/policies.lua"))()
assert(loadfile(directory .. "documents/register.lua"))()(policies)
