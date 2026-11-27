local json = dofile("share/lua/orion/json.lua")
local workshop = dofile("apps/ledger/share/addons/workshop/workshop.lua")
local output = dofile("apps/ledger/share/addons/workshop/output.lua")
local app = workshop.open(json)
local revision, number, last_min = 0, 0, 100000
local function call(command, args, options)
  options = options or {}
  number = number + 1
  local req = { request_id = "request-" .. number, command = command, args = args,
    expected_revision = options.revision or revision, actor = "shop-owner",
    at_min = options.minute or last_min }
  local receipt, err = app.apply(req)
  if options.error then assert(not receipt and err:find(options.error, 1, true), err); return end
  assert(receipt, err)
  revision, last_min = revision + 1, req.at_min
  assert(receipt.revision == revision)
  return receipt, req
end

call("vehicle.register", { id="first", owner_ref="customer-82", vin="wvWzzz1jz3w386752",
  plate="B-XW 142", jurisdiction="de-be", make="Volkswagen", model="Golf", year=2003, odometer_km=100001 })
call("vehicle.register", { id="second", owner_ref="customer-82", make="Toyota", model="Yaris", year=2004 })
call("vehicle.register", { id="third", owner_ref="customer-83", vin="WVWZZZ1JZ3W386752",
  make="VW", model="Golf", year=2003 }, { error="vin already registered" })
assert(#app.find_vehicle({owner_ref="customer-82"}) == 2)
assert(app.find_vehicle({vin="wvwzzz1jz3w386752"})[1].id == "first")
assert(app.find_vehicle({plate="BXW142", jurisdiction="DE-BE"})[1].id == "first")
call("vehicle.registration", { id="first", plate="HH-X 8", jurisdiction="DE-HH", reason="moved" })
assert(#app.find_vehicle({plate="BXW142", jurisdiction="DE-BE"}) == 0)
assert(#app.snapshot().vehicles.first.registration_history == 2)
call("vehicle.odometer", { id="first", km=100000 }, { error="correction reason" })
call("vehicle.odometer", { id="first", km=100000, reason="input correction" })
assert(app.snapshot().vehicles.first.odometer_history[2].reason == "input correction")

call("resource.register", {id="mechanic-1", name="Mechanic A", kind="worker"})
call("resource.register", {id="mechanic-2", name="Mechanic B", kind="worker"})
call("resource.register", {id="bay-1", name="Lift A", kind="bay"})
call("resource.block", {id="closure", resource="bay-1", start_min=100000, end_min=100020, reason="safety inspection"})
call("order.open", {id="repair-1", vehicle="first", complaint="Noise <script> & clang while driving"})
local lines = {
  {kind="part", reference="pads-ab", description="Pads", quantity=1, unit_minor=5100},
  {kind="labor", reference="brake-service", description="Brake service", quantity=2, unit_minor=4000},
}
call("order.quote", {id="repair-1", currency="EUR", lines={
  {kind="part", reference="pads-ab", description="Pads", quantity=100000, unit_minor=math.maxinteger},
}}, {error="unit_minor"})
local quoted = call("order.quote", {id="repair-1", currency="EUR", lines=lines})
assert(quoted.quoted_minor == 13100 and quoted.estimate_version == 1)
local updated = call("order.quote", {id="repair-1", currency="EUR", lines=lines})
assert(updated.estimate_version == 2)
call("order.approve", {id="repair-1", estimate_version=1}, {error="quote is not current"})
call("order.approve", {id="repair-1", estimate_version=2})

local team_a = {"mechanic-1", "bay-1"}
local team_b = {"mechanic-2", "bay-1"}
call("order.schedule", {id="repair-1", start_min=100010, end_min=100030, resources=team_a},
  {error="resource unavailable"})
call("order.schedule", {id="repair-1", start_min=100020, end_min=100080, resources=team_a})
assert(app.activity(100000, 100050).resources["bay-1"].unavailable_min == 20)
local status, req = call("order.schedule", {id="repair-1", start_min=100030, end_min=100100, resources=team_a})
assert(app.apply(req).revision == status.revision and #app.journal() == revision)
local data = req.args
data.start_min = 100022
local denied, error_text = app.apply(req)
assert(not denied and error_text:find("already used", 1, true))
assert(app.snapshot().slots["order:repair-1"].start_min == 100030)
call("order.open", {id="backdated", vehicle="first", complaint="Bad event clock"}, {minute=99999, error="command time"})

call("order.open", {id="repair-2", vehicle="second", complaint="Oil change"})
call("order.quote", {id="repair-2", currency="EUR", lines=lines})
call("order.approve", {id="repair-2", estimate_version=1})
call("order.schedule", {id="repair-2", start_min=100099, end_min=100130, resources=team_b}, {error="resource unavailable"})
call("order.schedule", {id="repair-2", start_min=100100, end_min=100130, resources=team_b},
  {revision=revision-1, error="stale revision"})
call("order.schedule", {id="repair-2", start_min=100100, end_min=100130, resources=team_b})
call("order.cancel", {id="repair-2", reason="customer cancelled"})
assert(app.snapshot().slots["order:repair-2"].active == false)
call("order.schedule", {id="repair-1", start_min=100100, end_min=100130, resources=team_a})
assert(app.activity(100020,100120).resources["bay-1"].scheduled_min == 20)

call("order.start", {id="repair-1"}, {minute=100105})
call("time.log", {id="repair-1", entry="time-1", worker="mechanic-2", start_min=100105,
  end_min=100121, billable_min=15}, {minute=100125, error="not assigned"})
call("time.log", {id="repair-1", entry="time-1", worker="mechanic-1", start_min=100105,
  end_min=100124, billable_min=15}, {minute=100125})
call("time.log", {id="repair-1", entry="time-overlapping", worker="mechanic-1", start_min=100123,
  end_min=100125, billable_min=2}, {minute=100125, error="overlapping worker time"})
call("time.correct", {id="repair-1", entry="time-1", replacement="time-2", worker="mechanic-1",
  start_min=100108, end_min=100125, billable_min=12, reason="recorded incorrect start"}, {minute=100126})
assert(app.activity(100100,100150).resources["mechanic-1"].actual_min == 17)
assert(app.snapshot().time["time-2"].billable_min == 12)

call("parts.record", {id="repair-1", reference="wrong", quantity=1, reason="part replacement"},
  {error="not in the approved estimate"})
call("parts.record", {id="repair-1", reference="pads-ab", quantity=2, reason="too many"},
  {error="actual part quantity"})
call("parts.record", {id="repair-1", reference="pads-ab", quantity=1, reason="fitted"}, {minute=100127})
assert(app.history("first").orders[1].actual_parts["pads-ab"].quantity == 1)
local completed, completion = call("order.complete", {id="repair-1", inspection="Brake function checked"})
assert(completed.invoice_intent == "workshop:repair-1")
call("order.complete", {id="repair-1", inspection="Brake function checked"}, {error="not in progress"})
local store = app.journal()
local reopened = workshop.open(json, json.decode(json.encode(store)))
assert(reopened.apply(completion).invoice_intent == completed.invoice_intent)
local snap = reopened.snapshot()
assert(snap.orders["repair-1"].intent.quoted_minor == 13100 and snap.orders["repair-1"].intent.status == "review_required")
assert(snap.orders["repair-1"].intent.lines[1].reference == "pads-ab")
assert(snap.time["time-1"].replaced_by == "time-2" and snap.time["time-1"].current == false)
assert(json.encode(snap) == json.encode(app.snapshot()))
local html = output.work_order(snap.orders["repair-1"], snap.vehicles.first)
assert(html:find("&lt;script&gt; &amp;", 1, true) and not html:find("<script>", 1, true))
assert(html:find("Approved estimate", 1, true) and html:find("13100", 1, true))
local csv = output.activity_csv(app.activity(100100, 100130))
assert(csv:find("\nbay-1,100100,100130,30,0,\n", 1, true))
assert(csv:find("\nmechanic-1,100100,100130,30,0,17\n", 1, true))
snap.orders["repair-1"].intent.lines[1].unit_minor = 0
assert(reopened.snapshot().orders["repair-1"].intent.lines[1].unit_minor == 5100)
assert(not pcall(workshop.open, json, json.decode("[{}]")))
assert(not pcall(workshop.open, json, json.decode("{}")))
assert(not pcall(workshop.open, json, json.null))
assert(not pcall(app.find_vehicle, { owner_ref=false, plate="HH-X 8", jurisdiction="DE-HH" }))

print("Workshop intake, estimates, resource conflicts, time corrections and invoice-intent replay passed")
