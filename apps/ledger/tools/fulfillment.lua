-- Local-only CLI; see apps/ledger/docs/fulfillment.md for the input format.
local repo = assert(arg[0]:match("^(.*)apps/ledger/tools/fulfillment%.lua$"), "invoke by repository path")
local json = dofile(repo .. "share/lua/orion/json.lua")
local workflow = dofile(repo .. "apps/ledger/share/addons/fulfillment/pick_lists.lua")

local function main()
  local action, path = arg[1], arg[2]
  assert(#arg == 2 and (action == "pick" or action == "labels"),
    "usage: lua apps/ledger/tools/fulfillment.lua pick|labels ORDERS.json")
  local file = assert(io.open(path, "rb"), "cannot open local order file")
  local data = file:read(1048577)
  file:close()
  assert(data and #data <= 1048576, "invalid order file size")
  local plan = workflow.plan(json.decode(data), json)
  local page = action == "pick" and workflow.pick_list(plan) or workflow.order_labels(plan)
  assert(io.write(page) and io.flush(), "cannot write HTML")
end

local ok, err = pcall(main)
if not ok then
  io.stderr:write("Fulfillment: ", tostring(err):gsub("^.-:%d+: ", ""), "\n")
  os.exit(1)
end
