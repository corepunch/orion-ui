-- Pure search shared by the desktop browser and the trusted Lua host.
local S = {}

function S.normalize(value)
  if value == nil then return "" end
  local input, out = tostring(value):match("^%s*(.-)%s*$"):lower(), {}
  assert(utf8.len(input), "invalid search text")
  for _, cp in utf8.codes(input) do
    if cp >= 0x410 and cp <= 0x42f then cp = cp + 32
    elseif cp == 0x401 then cp = 0x451 end
    out[#out + 1] = utf8.char(cp)
  end
  return table.concat(out)
end

function S.find(catalog, query, tags, kind)
  local out, wanted = {}, {}
  for _, tag in ipairs(tags or {}) do
    tag = S.normalize(tag)
    if tag ~= "" then wanted[tag] = true end
  end
  query = S.normalize(query)
  for _, collection in ipairs({{name="recipe", rows=catalog.recipes}, {name="node", rows=catalog.nodes}}) do
    if not kind or kind == collection.name then
      for _, item in ipairs(collection.rows) do
        local haystack = S.normalize(item.id .. " " .. item.title .. " " .. item.description .. " "
          .. table.concat(item.tags," ") .. " " .. table.concat(item.aliases," "))
        local match = true
        for word in query:gmatch("%S+") do
          if not haystack:find(word,1,true) then match = false; break end
        end
        if match and next(wanted) then
          local actual = {}
          for _, tag in ipairs(item.tags) do actual[S.normalize(tag)] = true end
          for tag in pairs(wanted) do
            if not actual[tag] then match = false; break end
          end
        end
        if match then out[#out+1] = item end
      end
    end
  end
  return out
end

return S
