local json = dofile("share/lua/orion/json.lua")
local workflow = dofile("apps/ledger/share/addons/fulfillment/pick_lists.lua")
local file = assert(io.open("apps/ledger/tests/fixtures/fulfillment/orders.json", "r"))
local source = file:read("*a")
file:close()
local function fixture() return json.decode(source) end
local assertions = 0
local function eq(actual, expected)
  assertions = assertions + 1
  assert(actual == expected, ("%s ~= %s"):format(tostring(actual), tostring(expected)))
end
local function includes(page, fragment) eq(page:find(fragment, 1, true) ~= nil, true) end
local function rejects(change, problem)
  local input = fixture()
  change(input)
  local ok, error_text = pcall(workflow.plan, input, json)
  eq(ok, false)
  includes(error_text, problem)
end

local plan = workflow.plan(fixture(), json)
eq(#plan.orders, 3)
eq(#plan.picks, 4)
eq(plan.picks[1].sku, "0007")
eq(plan.picks[1].quantity, 9)
eq(plan.picks[1].orders[1].id, "O-101")
eq(plan.picks[1].orders[1].quantity, 4)
eq(plan.picks[1].orders[2].id, "O-102")
eq(plan.picks[1].orders[2].quantity, 5)
eq(plan.orders[1].recipient, "Packing recipient A")
eq(plan.orders[2].lines[2].sku, "A-15")
eq(plan.orders[2].lines[2].quantity, 1)
eq(plan.picks[1].variant, nil)
local wide, narrow = plan.picks[3], plan.picks[4]
local wide_variant, narrow_variant = "Navy / 105 cm / straight handle", "Navy / 90 cm / curved handle"
eq(wide.sku, "UMBRELLA")
eq(wide.variant, wide_variant)
eq(wide.quantity, 3)
eq(#wide.orders, 2)
eq(wide.orders[1].id, "O-101")
eq(wide.orders[1].quantity, 1)
eq(wide.orders[2].id, "O-102")
eq(wide.orders[2].quantity, 2)
eq(narrow.sku, "UMBRELLA")
eq(narrow.variant, narrow_variant)
eq(narrow.quantity, 4)
eq(#narrow.orders, 1)
eq(narrow.orders[1].id, "O-105")
eq(narrow.orders[1].quantity, 4)

local pick = workflow.pick_list(plan)
includes(pick, "O-101 × 4<br>O-102 × 5")
includes(pick, wide_variant .. "</td><td>3</td><td>O-101 × 1<br>O-102 × 2")
includes(pick, narrow_variant .. "</td><td>4</td><td>O-105 × 4")
eq(pick:find("Packing recipient", 1, true), nil)
eq(pick:find("O-103", 1, true), nil)
eq(pick:find("O-104", 1, true), nil)
local labels = workflow.order_labels(plan)
includes(labels, "Packing recipient A")
includes(labels, "Packing recipient B")
includes(labels, "Not a carrier shipping label.")
eq(labels:find("O-104", 1, true), nil)
local function card(order_id)
  for article in labels:gmatch("<article>(.-)</article>") do
    if article:find("Order " .. order_id, 1, true) then return article end
  end
  error("missing label: " .. order_id)
end
includes(card("O-101"), wide_variant .. "</td><td>1")
includes(card("O-102"), wide_variant .. "</td><td>2")
includes(card("O-105"), narrow_variant .. "</td><td>4")
eq(card("O-105"):find(wide_variant, 1, true), nil)
eq(card("O-101"):find(narrow_variant, 1, true), nil)

local reordered = fixture()
local function reverse(array)
  for i = 1, #array // 2 do array[i], array[#array+1-i] = array[#array+1-i], array[i] end
end
reverse(reordered.orders)
for _, order in ipairs(reordered.orders) do if order.lines then reverse(order.lines) end end
local reordered_plan = workflow.plan(reordered, json)
eq(workflow.pick_list(reordered_plan), pick)
eq(workflow.order_labels(reordered_plan), labels)

local mixed = fixture()
mixed.orders[3].lines[2] = {sku="UMBRELLA", quantity=1}
mixed.orders[3].lines[3] = {sku="UMBRELLA", variant="navy / 90 cm / curved handle", quantity=1}
local finished = workflow.plan(mixed, json)
eq(#finished.picks, 6)
eq(finished.picks[3].variant, nil)
eq(finished.picks[3].quantity, 1)
eq(finished.picks[6].variant, "navy / 90 cm / curved handle")

local altered = fixture()
altered.orders[1].id = [[A "<&>]]
altered.orders[1].recipient = "<img src=x>"
altered.orders[1].lines[1].sku = "<script>"
altered.orders[1].lines[1].variant = "<variant>"
local safe = workflow.plan(altered, json)
includes(workflow.pick_list(safe), "&lt;script&gt;")
includes(workflow.pick_list(safe), "&lt;variant&gt;")
local rendered = workflow.order_labels(safe)
includes(rendered, "A &quot;&lt;&amp;&gt;")
includes(rendered, "&lt;img src=x&gt;")
eq(rendered:find("<img", 1, true), nil)
rejects(function(x) x.orders[4].selected = true end, "not open")
rejects(function(x) x.orders[1].selected = false; x.orders[2].selected = false; x.orders[3].selected = false end, "no selected open orders")
rejects(function(x) x.orders[1].lines[1].quantity = 0 end, "invalid pick quantity")
rejects(function(x) x.orders[1].lines[1].quantity = 1.5 end, "invalid pick quantity")
rejects(function(x) x.orders[1].lines[1].sku = " 0007" end, "surrounding whitespace")
rejects(function(x) x.orders[1].lines[1].sku = 7 end, "invalid SKU")
rejects(function(x) x.orders[1].lines[1].variant = "" end, "invalid variant")
rejects(function(x) x.orders[1].lines[1].variant = json.null end, "invalid variant")
rejects(function(x) x.orders[1].lines[1].variant = " Navy" end, "variant has surrounding whitespace")
rejects(function(x) x.orders[1].id = x.orders[2].id end, "duplicate order ID")
rejects(function(x) x.orders[1].recipient = "" end, "invalid recipient")
rejects(function(x) x.orders[1].address = "do not ingest" end, "unexpected field")
rejects(function(x) x.orders[1].lines = {} end, "must be an array")
rejects(function(x) x.orders[1].selected = nil end, "selected must be explicit")
print(("Ledger fulfillment: %d checks passed"):format(assertions))
