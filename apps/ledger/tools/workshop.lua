-- Local journal operator. Commands print a successor journal, never mutate the input.
local repo = assert(arg[0]:match("^(.*)apps/ledger/tools/workshop%.lua$"), "invoke by repository path")
local json = dofile(repo .. "share/lua/orion/json.lua")
local Workshop = dofile(repo .. "apps/ledger/share/addons/workshop/workshop.lua")
local output = dofile(repo .. "apps/ledger/share/addons/workshop/output.lua")

local function read(path)
  local file = assert(io.open(path, "rb"), "cannot open input file")
  local data = file:read(1048577)
  file:close()
  assert(data and #data <= 1048576, "invalid input file size")
  return json.decode(data)
end

local function main()
  local action, path = arg[1], arg[2]
  assert((action == "init" and #arg == 1)
      or (action == "show" and #arg == 2)
      or ((action == "apply" or action == "find" or action == "history" or action == "print") and #arg == 3)
      or ((action == "report" or action == "report-csv") and #arg == 4),
    "usage: lua apps/ledger/tools/workshop.lua init | apply JOURNAL COMMAND | show JOURNAL | find JOURNAL QUERY | history JOURNAL VEHICLE | print JOURNAL ORDER | report|report-csv JOURNAL START_MIN END_MIN")
  local workshop = Workshop.open(json, path and read(path))
  local result, printable
  if action == "apply" then
    local receipt, err = workshop.apply(read(arg[3]))
    assert(receipt, err)
    io.stderr:write("Accepted: ", json.encode(receipt), "\n")
    result = workshop.journal()
  elseif action == "show" then result = workshop.snapshot()
  elseif action == "report" or action == "report-csv" then
    result = workshop.activity(tonumber(arg[3]), tonumber(arg[4]))
    if action == "report-csv" then printable = output.activity_csv(result) end
  elseif action == "print" then
    local snapshot = workshop.snapshot()
    local order = assert(snapshot.orders[arg[3]], "unknown work order")
    printable = output.work_order(order, snapshot.vehicles[order.vehicle])
  elseif action == "find" then result = workshop.find_vehicle(read(arg[3]))
  elseif action == "history" then result = workshop.history(arg[3])
  else result = workshop.journal() end
  assert(io.write(printable or json.encode(result), "\n") and io.flush(), "cannot write output")
end

local ok, err = pcall(main)
if not ok then
  io.stderr:write("Workshop: ", tostring(err):gsub("^.-:%d+: ", ""), "\n")
  os.exit(1)
end
