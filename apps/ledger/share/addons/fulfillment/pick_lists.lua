-- Local batch-picking workflow: no account, network, posting, or shipment actions.
local M = {}
local MAX_ORDERS, MAX_LINES, MAX_QUANTITY = 250, 4000, 1000000

local function fields(row, allowed, where)
  assert(type(row) == "table", where .. " must be an object")
  for key in pairs(row) do assert(allowed[key], "unexpected field in " .. where) end
end

local function array(value, max, array_mt, where)
  assert(type(value) == "table" and (not array_mt or getmetatable(value) == array_mt),
    where .. " must be an array")
  local n, count = #value, 0
  assert(n > 0 and n <= max, where .. " has an invalid length")
  for key in pairs(value) do
    assert(math.type(key) == "integer" and key >= 1 and key <= n, where .. " must be a dense array")
    count = count + 1
  end
  assert(count == n, where .. " must be a dense array")
end

local function required_text(value, max, where)
  assert(type(value) == "string" and #value <= max and utf8.len(value)
    and value:find("%S") and not value:find("[%z\1-\31\127]"), "invalid " .. where)
  return value
end

function M.plan(input, json)
  local array_mt = json and getmetatable(json.array())
  fields(input, {orders=true}, "batch")
  array(input.orders, MAX_ORDERS, array_mt, "orders")
  local orders, picks, seen_orders, by_sku, line_count = {}, {}, {}, {}, 0
  local order_fields = {id=true, recipient=true, status=true, selected=true, lines=true}
  local line_fields = {sku=true, quantity=true}

  for _, src in ipairs(input.orders) do
    fields(src, order_fields, "order")
    local id = required_text(src.id, 100, "order ID")
    assert(not seen_orders[id], "duplicate order ID")
    seen_orders[id] = true
    assert(src.status == "open" or src.status == "closed" or src.status == "cancelled", "invalid order status")
    assert(type(src.selected) == "boolean", "selected must be explicit")
    if src.selected then
      assert(src.status == "open", "a selected order is not open")
      array(src.lines, MAX_LINES, array_mt, "order lines")
      local order = {id=id, recipient=required_text(src.recipient, 160, "recipient or internal recipient alias"), lines={}}
      local selected_lines = {}
      for _, raw in ipairs(src.lines) do
        line_count = line_count + 1
        assert(line_count <= MAX_LINES, "too many selected order lines")
        fields(raw, line_fields, "order line")
        local sku = required_text(raw.sku, 100, "SKU")
        assert(sku == sku:match("^%s*(.-)%s*$"), "SKU has surrounding whitespace")
        local qty = raw.quantity
        assert(math.type(qty) == "integer" and qty > 0 and qty <= MAX_QUANTITY, "invalid pick quantity")
        local line = selected_lines[sku]
        if not line then
          line = {sku=sku, quantity=0}
          selected_lines[sku] = line
          order.lines[#order.lines + 1] = line
        end
        line.quantity = line.quantity + qty
      end
      table.sort(order.lines, function(a, b) return a.sku < b.sku end)
      orders[#orders + 1] = order
      for _, line in ipairs(order.lines) do
        local pick = by_sku[line.sku]
        if not pick then
          pick = {sku=line.sku, quantity=0, orders={}}
          by_sku[line.sku] = pick
          picks[#picks + 1] = pick
        end
        pick.quantity = pick.quantity + line.quantity
        pick.orders[#pick.orders + 1] = {id=id, quantity=line.quantity}
      end
    end
  end
  assert(#orders > 0, "no selected open orders")
  table.sort(orders, function(a, b) return a.id < b.id end)
  table.sort(picks, function(a, b) return a.sku < b.sku end)
  for _, pick in ipairs(picks) do table.sort(pick.orders, function(a, b) return a.id < b.id end) end
  return {orders=orders, picks=picks}
end

local function escape(value)
  return (value:gsub("[&<>\"']", {["&"]="&amp;", ["<"]="&lt;", [">"]="&gt;", ['"']="&quot;", ["'"]="&#39;"}))
end

local function document(title, body)
  return '<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><title>' .. title .. [[</title>
<style>
body { color:#111; background:white; font: 15px system-ui,sans-serif; margin:1rem; }
h1 { font-size:1.45rem; }
h2 { font-size:1.1rem; margin:0; }
table { width:100%; border-collapse:collapse; margin:.75rem 0; }
th, td { border-bottom:1px solid #aaa; padding:.3rem; text-align:left; overflow-wrap:anywhere; }
thead { display:table-header-group; }
tr, article { break-inside:avoid; }
article { border:1px solid #555; margin:.75rem 0; padding:.75rem; overflow-wrap:anywhere; }
small { color:#333; }
@media print { body { margin:0; } }
</style></head><body>
]] .. table.concat(body, "\n") .. "\n</body></html>\n"
end

function M.pick_list(plan)
  local out = {'<h1>Batch pick list</h1>', '<p>Pick each SKU once, then split quantities into the listed orders.</p>',
    '<table><thead><tr><th>Picked</th><th>SKU</th><th>Total</th><th>Split by order ID</th></tr></thead><tbody>'}
  for _, pick in ipairs(plan.picks) do
    local splits = {}
    for _, part in ipairs(pick.orders) do
      splits[#splits + 1] = ("%s × %d"):format(escape(part.id), part.quantity)
    end
    out[#out + 1] = ('<tr><td><input type="checkbox" aria-label="Mark picked"></td><th scope="row">%s</th><td>%d</td><td>%s</td></tr>')
      :format(escape(pick.sku), pick.quantity, table.concat(splits, "<br>"))
  end
  out[#out + 1] = "</tbody></table>"
  return document("Batch pick list", out)
end

function M.order_labels(plan)
  local notice = "Internal packing label. Not a carrier shipping label."
  local out = {'<h1>Order packing labels</h1>', '<p>' .. notice .. '</p>'}
  for _, order in ipairs(plan.orders) do
    out[#out + 1] = ('<article><small>%s</small><h2>Order %s</h2><p>Recipient: %s</p>')
      :format(notice, escape(order.id), escape(order.recipient))
    out[#out + 1] = '<table><thead><tr><th>SKU</th><th>Quantity</th></tr></thead><tbody>'
    for _, line in ipairs(order.lines) do
      out[#out + 1] = ('<tr><th scope="row">%s</th><td>%d</td></tr>'):format(escape(line.sku), line.quantity)
    end
    out[#out + 1] = '</tbody></table></article>'
  end
  return document("Order packing labels", out)
end

return M
