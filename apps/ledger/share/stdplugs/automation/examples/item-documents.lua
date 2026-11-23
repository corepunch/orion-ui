-- Read-only. Set LEDGER_ITEM_ID or LEDGER_SKU in the caller's environment.
local id, sku = os.getenv("LEDGER_ITEM_ID"), os.getenv("LEDGER_SKU")
if id then
  id = id:match("^%d+$") and math.tointeger(tonumber(id))
  assert(id and id > 0, "LEDGER_ITEM_ID must be a positive integer")
end
local report = ledger.automation.preview("report.item_documents", {item_id=id, sku=sku,
  date_from=os.getenv("LEDGER_DATE_FROM"), date_to=os.getenv("LEDGER_DATE_TO"),
  posted_only=os.getenv("LEDGER_POSTED_ONLY") == "1"})

local function cell(value)
  return (tostring(value or ""):gsub("[%z\1-\31\127]", " "))
end

print("Document ID\tDate\tNumber\tPosted\tLine ID\tItem ID\tSKU\tItem\tQuantity\tMatch")
for _, group in ipairs(report) do
  local doc = group.document
  for _, line in ipairs(group.lines) do
    local qty
    if doc.exact_money and line.quantity_milli ~= nil then
      local milli = line.quantity_milli
      -- Keep exact quantities exact, including Lua's most negative integer.
      local digits = tostring(milli):gsub("^%-", "")
      digits = #digits < 4 and string.rep("0", 4 - #digits) .. digits or digits
      qty = (milli < 0 and "-" or "") .. digits:sub(1, -4) .. "." .. digits:sub(-3)
    else
      qty = line.quantity
    end
    print(table.concat({cell(doc.id),cell(doc.date),cell(doc.number),doc.posted and "yes" or "no",
      cell(line.id),cell(line.item_id),cell(line.item_sku),cell(line.item_name),cell(qty),
      line.matches_item and "yes" or ""}, "\t"))
  end
end
