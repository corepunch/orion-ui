-- Shipped recipe adapters: local data in, preview/document out. No network,
-- file paths supplied by the caller, writes, payment matching or stock posting.
local directory = assert(debug.getinfo(1, "S").source:match("^@(.*/)"))
local function load_module(relative)
  return assert(loadfile(directory .. relative))()
end
local labels = load_module("../../addons/labels/label_sheet.lua")
local cards = load_module("../../addons/product_card/product_card.lua")
local picks = load_module("../../addons/fulfillment/pick_lists.lua")
local bank = load_module("../bank_statement/client_bank.lua")
local W, codec = {}, nil

function W.json()
  if codec then return codec end
  local candidates = {
    "../../../orion/lua/orion/json.lua", "../../../lua/orion/json.lua",
    "../../../../../share/lua/orion/json.lua",
  }
  for _, path in ipairs(candidates) do
    local loader = loadfile(directory .. path)
    if loader then codec = loader(); return codec end
  end
  error("Orion JSON module not installed",0)
end

function W.print_item_labels(p)
  local rows = p.rows
  if p.item_id then
    assert(not rows and p.item_id > 0, "provide an item ID or label lines, not both")
    local item = assert(ledger.find("ib","items",p.item_id), "item not found")
    rows = {{name=item.name, sku=item.sku, copies=p.copies or 1}}
  else
    assert(not p.copies, "put the copy count on each label line")
  end
  local output, count, pages = labels.render(rows, p.options)
  return {format="html", document=output, labels=count, pages=pages}
end

function W.print_product_card(p)
  assert(p.item_id > 0, "choose an item")
  local item = assert(ledger.find("ib","items",p.item_id), "item not found")
  return {format="html", document=cards.render(item, p.details or {})}
end

local function plan_orders(orders)
  local json = W.json()
  -- Canonicalize plain Lua arrays and objects at the data boundary. Do not
  -- depend on the caller sharing our JSON decoder's array metatable.
  return picks.plan(json.decode(json.encode(orders)), json)
end

function W.report_pick_orders(p)
  local plan = plan_orders(p.orders)
  return {format="html", document=picks.pick_list(plan), orders=#plan.orders, pick_lines=#plan.picks}
end

function W.print_order_labels(p)
  local plan = plan_orders(p.orders)
  return {format="html", document=picks.order_labels(plan), orders=#plan.orders}
end

function W.preview_bank_statement(p)
  return bank.preview(p.statement,p.account)
end

return W
