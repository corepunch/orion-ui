-- Report item membership at the document level, then keep its whole tabular part.
local function date(value)
  if type(value) ~= "string" then return false end
  local y, m, d = value:match("^(%d%d%d%d)%-(%d%d)%-(%d%d)$")
  if not y then return false end
  y, m, d = tonumber(y), tonumber(m), tonumber(d)
  local days = {31, y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0) and 29 or 28, 31,30,31,30,31,31,30,31,30,31}
  return y > 0 and days[m] ~= nil and d >= 1 and d <= days[m]
end

local function select_item(p)
  assert((p.item_id ~= nil) ~= (p.sku ~= nil), "supply either item_id or sku, not both")
  if p.item_id then
    assert(math.type(p.item_id) == "integer" and p.item_id > 0, "invalid item ID")
    return assert(ledger.find("ib", "items", p.item_id), "unknown item ID")
  end
  assert(type(p.sku) == "string" and p.sku:match("%S"), "nonempty SKU required")
  local matches = ledger.fetch("ib", "items", "sku", p.sku)
  assert(#matches > 0, "unknown SKU")
  assert(#matches == 1, "ambiguous SKU; select by item_id")
  return matches[1]
end

return function(p)
  assert(p.date_from == nil or date(p.date_from), "invalid date_from (use YYYY-MM-DD)")
  assert(p.date_to == nil or date(p.date_to), "invalid date_to (use YYYY-MM-DD)")
  assert(not p.date_from or not p.date_to or p.date_from <= p.date_to, "date_from must not follow date_to")
  local item = select_item(p)
  local matched = {}
  for _, line in ipairs(ledger.fetch("ib", "doc_lines", "item_id", item.id)) do
    matched[line.document_id] = true
  end
  if not next(matched) then return {} end

  local result, selected = {}, {}
  for _, doc in ipairs(ledger.fetch("ib", "documents")) do
    if matched[doc.id] and (not p.posted_only or doc.posted) and
        (not p.date_from or (date(doc.date) and doc.date >= p.date_from)) and
        (not p.date_to or (date(doc.date) and doc.date <= p.date_to)) then
      local entry = {document=doc, lines={}, matching_lines=0}
      selected[doc.id], result[#result + 1] = entry, entry
    end
  end
  if #result == 0 then return result end

  local items = {[item.id]=item}
  for _, line in ipairs(ledger.fetch("ib", "doc_lines")) do
    local entry = selected[line.document_id]
    if entry then
      local line_item = items[line.item_id]
      if line_item == nil then
        line_item = ledger.find("ib", "items", line.item_id) or false
        items[line.item_id] = line_item
      end
      line.matches_item = line.item_id == item.id
      if line_item then line.item_name, line.item_sku = line_item.name, line_item.sku end
      if line.matches_item then entry.matching_lines = entry.matching_lines + 1 end
      entry.lines[#entry.lines + 1] = line
    end
  end
  for _, entry in ipairs(result) do
    table.sort(entry.lines, function(a,b) return a.id < b.id end)
  end
  table.sort(result, function(a,b)
    local first, second = a.document, b.document
    if first.date == second.date then return first.id < second.id end
    return first.date < second.date
  end)
  return result
end
