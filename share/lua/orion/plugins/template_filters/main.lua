return function()
  local orion = require("orion")
  local Filters = {}
  function Filters.apply(value, name, custom)
    value = tostring(value)
    if not name then return value end
    if name == "dash" then return value == "" and "—" or value end
    if name == "zero_dash" then return orion.parseNumber(value) == 0 and "—" or value end
    if name == "iso" or name == "dmy" or name == "ymd" or name == "ddmm" then
      local iso = orion.parseDate(value)
      if not iso then return value end
      local y, m, d = iso:match("^(%d%d%d%d)%-(%d%d)%-(%d%d)$")
      if name == "iso" then return iso end
      if name == "dmy" then return d .. "." .. m .. "." .. y end
      if name == "ymd" then return y .. m .. d end
      return d .. m
    end
    if name == "decimal3" or name == "decimal3comma" or name == "zero_comma" or name == "zero_decimal3comma" or name == "dot" or name == "comma" then
      local numeric = orion.parseNumber(value)
      if name:sub(1, 5) == "zero_" and numeric == 0 then return "—" end
      local formatted = string.format(name:find("decimal3", 1, true) and "%.3f" or "%.2f", numeric)
      if name:find("comma", 1, true) then formatted = formatted:gsub("%.", ",") end
      return formatted
    end
    local formatted = custom and custom(name, value)
    return formatted or value
  end
  return Filters
end
