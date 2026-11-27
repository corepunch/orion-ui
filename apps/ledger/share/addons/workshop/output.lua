-- Offline output. Whitelist columns and HTML-escape every user-provided value.
local Output = {}

local escape = { ['&'] = '&amp;', ['<'] = '&lt;', ['>'] = '&gt;', ['"'] = '&quot;', ["'"] = '&#39;' }
local function html(value)
  return (tostring(value):gsub('[&<>"\']', escape))
end

local function value(label, raw)
  return "<dt>" .. label .. "</dt><dd>" .. html(raw) .. "</dd>"
end

function Output.work_order(order, vehicle)
  local quote = order.estimates[order.approved or #order.estimates]
  local parts = {
    '<!doctype html><html lang="en"><meta charset="utf-8"><title>Workshop order</title>',
    '<style>body{font:16px system-ui,sans-serif;margin:2rem;color:#151515}dl{display:grid;grid-template-columns:max-content 1fr;gap:.35rem 1rem}dt{font-weight:bold}dd{margin:0;overflow-wrap:anywhere}table{width:100%;border-collapse:collapse}th,td{padding:.5rem;border-bottom:1px solid #999;text-align:left;overflow-wrap:anywhere}tr{break-inside:avoid}@media print{body{margin:0}}</style><body>',
    "<h1>Workshop order</h1><dl>",
    value("Reference", order.id), value("Status", order.status),
    value("Owner reference", order.owner_ref), value("Vehicle", vehicle.make .. " " .. vehicle.model),
  }
  if vehicle.vin then parts[#parts + 1] = value("VIN", vehicle.vin) end
  if vehicle.registration then parts[#parts + 1] = value("Current plate", vehicle.registration.plate) end
  parts[#parts + 1] = value("Complaint", order.complaint)
  if order.inspection then parts[#parts + 1] = value("Inspection", order.inspection) end
  if order.cancel_reason then parts[#parts + 1] = value("Cancellation", order.cancel_reason) end
  parts[#parts + 1] = "</dl>"
  if quote then
    parts[#parts + 1] = (order.approved and "<h2>Approved estimate</h2>" or "<h2>Unapproved estimate</h2>")
    parts[#parts + 1] = "<p>Estimate version " .. html(quote.version) .. "; " .. html(quote.currency) .. " minor units</p>"
    parts[#parts + 1] = "<table><thead><tr><th>Item</th><th>Type</th><th>Quoted quantity</th><th>Reported part usage</th><th>Quoted amount</th></tr></thead><tbody>"
    for _, line in ipairs(quote.lines) do
      local actual = line.kind == "part" and order.actual_parts[line.reference]
      parts[#parts + 1] = "<tr><td>" .. html(line.description) .. "</td><td>" .. html(line.kind)
        .. "</td><td>" .. html(line.quantity) .. "</td><td>" .. (actual and html(actual.quantity) or "")
        .. "</td><td>" .. html(line.subtotal_minor) .. "</td></tr>"
    end
    parts[#parts + 1] = "<tr><th scope=\"row\" colspan=\"4\">Quoted total</th><td>" .. html(quote.quoted_minor)
      .. "</td></tr></tbody></table><p>Operational record. Not an invoice or a stock movement.</p>"
  end
  parts[#parts + 1] = "</body></html>"
  return table.concat(parts, "\n")
end

function Output.activity_csv(report)
  local ordered = {}
  for id in pairs(report.resources) do ordered[#ordered + 1] = id end
  table.sort(ordered)
  local lines = { "resource_id,start_min,end_min,scheduled_min,unavailable_min,recorded_worker_min" }
  for _, id in ipairs(ordered) do
    assert(id:match("^[A-Za-z0-9][A-Za-z0-9_.:%-]*$"), "invalid resource reference")
    local values = report.resources[id]
    lines[#lines + 1] = table.concat({ id, report.start_min, report.end_min, values.scheduled_min,
      values.unavailable_min, values.actual_min or "" }, ",")
  end
  return table.concat(lines, "\n")
end

return Output
