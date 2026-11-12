local normalize = require('normalize')

local D = {}
local descriptors = {
  counterparties = {field = 'tax_id', country = 'country_code'},
  items = {field = 'sku'},
}

local function normalized_key(value)
  if type(value) ~= 'string' then return '' end
  return normalize((value:gsub('^%s+', ''):gsub('%s+$', '')))
    :gsub('^%s+', ''):gsub('%s+$', '')
end

-- Return duplicate records in source order and groups in first-seen key order.
-- Rows are never edited or copied, so callers retain the host's record identity.
function D.find(rows, entity)
  local descriptor = descriptors[entity]
  if not descriptor or type(rows) ~= 'table' then return {}, {} end

  local by_key, ordered = {}, {}
  for _, row in ipairs(rows) do
    if type(row) == 'table' then
      local value = normalized_key(row[descriptor.field])
      local country = descriptor.country and normalized_key(row[descriptor.country]) or ''
      local key = value ~= '' and (country .. '\31' .. value) or ''
      if key ~= '' then
        local group = by_key[key]
        if not group then
          group = {key = country ~= '' and (country .. ':' .. value) or value,
            value = value, country = country, field = descriptor.field, rows = {}}
          by_key[key] = group
          ordered[#ordered + 1] = group
        end
        group.rows[#group.rows + 1] = row
      end
    end
  end

  local records, groups = {}, {}
  for _, group in ipairs(ordered) do
    if #group.rows > 1 then
      groups[#groups + 1] = group
      for _, row in ipairs(group.rows) do records[#records + 1] = row end
    end
  end
  return records, groups
end

return D
