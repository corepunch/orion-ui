return function()
  local Escape = {}
  function Escape.html(value)
    return (tostring(value):gsub("&", "&amp;"):gsub("<", "&lt;"):gsub(">", "&gt;"):gsub('"', "&quot;"):gsub("'", "&#39;"))
  end
  function Escape.apply(value, format)
    value = tostring(value)
    if format == "none" then return value end
    if format == "csv" then return (value:gsub('"', '""')) end
    assert(format == nil or format == "html" or format == "xml", "unknown template format: " .. tostring(format))
    return Escape.html(value)
  end
  return Escape
end
