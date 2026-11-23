-- Data-only presentation on the independent native Marketplace worker.
local M = {}
local function brief(row)
  local parts = { row.sku or (row.source_item_id and ("unmapped ID " .. row.source_item_id)) or "unmapped" }
  if row.variant and row.variant ~= row.sku then parts[#parts+1] = row.variant end
  if row.source_location then parts[#parts+1] = row.source_location end
  if row.shipment_type then parts[#parts+1] = row.shipment_type end
  local quantities = {}
  for _, field in ipairs({"available","reserved","listed_total"}) do
    if row[field] ~= nil then quantities[#quantities+1] = field .. "=" .. row[field] end
  end
  parts[#parts+1] = table.concat(quantities,", ")
  parts[#parts+1] = row.basis
  return table.concat(parts," | ")
end
function M.prepare(provider, opts, json, api, adapter, fulfillment, request)
  local snapshot = api.snapshot(provider,opts,request,adapter,json)
  local eligible, rows = 0, {}
  if opts.mode ~= "availability" then
    rows[#rows+1] = "Orders"
    for i, order in ipairs(snapshot.orders) do
      if order.eligible then eligible = eligible+1 end
      if i <= 75 then rows[#rows+1] = order.id .. " | " .. order.status
        .. (order.eligible and " | ready to pick" or " | held: " .. order.hold_reason) end
    end
    if #snapshot.orders > 75 then rows[#rows+1] = "Showing the first 75 orders." end
  end
  if opts.mode ~= "orders" then
    rows[#rows+1] = ""
    rows[#rows+1] = "Marketplace availability (not Ledger warehouse stock; do not sum scopes)"
    for i=1,math.min(#snapshot.availability,75) do rows[#rows+1] = brief(snapshot.availability[i]) end
    if #snapshot.availability > 75 then rows[#rows+1] = "Showing the first 75 availability rows." end
  end
  if #snapshot.warnings > 0 then
    rows[#rows+1] = ""
    for i=1,math.min(#snapshot.warnings,10) do rows[#rows+1] = snapshot.warnings[i] end
  end
  local preview, html
  if eligible > 0 then
    local ok, batch = pcall(api.picking,snapshot,json)
    if ok then
      local planned, plan = pcall(fulfillment.plan,batch,json)
      if planned then
        local lines = {"SKU | variant | barcode | quantity | order split"}
        for i=1,math.min(#plan.picks,150) do
          local pick = plan.picks[i]
          local split = {}
          for _, entry in ipairs(pick.orders) do split[#split+1] = entry.id .. " × " .. entry.quantity end
          lines[#lines+1] = table.concat({pick.sku,pick.variant or "",pick.barcode or "",
            tostring(pick.quantity),table.concat(split,", ")}," | ")
          if #table.concat(lines,"\n") > 90000 then break end
        end
        if #lines-1 < #plan.picks then lines[#lines+1] = "Preview shortened; saved sheet includes all eligible lines." end
        preview = table.concat(lines,"\n")
        html = fulfillment.pick_list(plan)
        if #html > 8388608 then preview,html=nil,nil end
      end
    end
  end
  return json.encode({orders=#snapshot.orders,eligible=eligible,held=#snapshot.orders-eligible,
    availability=#snapshot.availability,warnings=#snapshot.warnings, details=table.concat(rows,"\n"),
    pick_available=html~=nil}),preview,html
end
return M
