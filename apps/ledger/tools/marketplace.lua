-- From the checkout: lua apps/ledger/tools/marketplace.lua PROVIDER OUTPUT OPTIONS.json
local repo = assert(arg[0]:match("^(.*)apps/ledger/tools/marketplace%.lua$"),
  "run apps/ledger/tools/marketplace.lua from the checkout")
local dir = repo .. "apps/ledger/share/addons/marketplaces/"
local json = dofile(repo .. "share/lua/orion/json.lua")
local api = dofile(dir .. "marketplaces.lua")

local function settings(path)
  local f = assert(io.open(path,"rb"),"cannot open marketplace options file")
  local raw = f:read(32769); f:close()
  assert(raw and #raw <= 32768,"invalid marketplace options size")
  return api.options(json.decode(raw),json)
end

local function main()
  local provider, output, config = arg[1],arg[2],arg[3]
  assert(#arg==3 and (provider=="ozon" or provider=="ebay")
    and (output=="snapshot" or output=="pick-json" or output=="pick-html" or output=="labels-html"),
    "usage: lua apps/ledger/tools/marketplace.lua ebay|ozon snapshot|pick-json|pick-html|labels-html OPTIONS.json")
  local opts = settings(config)
  if output ~= "snapshot" then
    assert(opts.mode~="availability","pick output requires orders")
    opts.mode = "orders"
  end
  local path = os.getenv(provider=="ebay" and "ORION_EBAY_HEADERS" or "ORION_OZON_HEADERS")
  local request = dofile(dir .. "transport.lua").new(provider,opts.environment,path,json)
  local snapshot = api.snapshot(provider,opts,request,dofile(dir .. provider .. ".lua"),json)
  if output=="snapshot" then return json.encode(snapshot) .. "\n" end
  local picks = api.picking(snapshot,json)
  local dropped = #snapshot.orders - #picks.orders
  if dropped > 0 then io.stderr:write(("Held %d order(s); review a snapshot for reasons.\n"):format(dropped)) end
  for _, warning in ipairs(snapshot.warnings) do io.stderr:write(warning,"\n") end
  local workflow = dofile(repo .. "apps/ledger/share/addons/fulfillment/pick_lists.lua")
  if #picks.orders==0 then error("No eligible orders; review a snapshot before picking",0) end
  local plan = workflow.plan(picks,json)
  if output=="pick-json" then
    local encoded = json.encode(picks)
    assert(#encoded <= 1048576,"pick batch file would exceed the fulfillment input limit; narrow the interval")
    return encoded .. "\n"
  end
  return output=="pick-html" and workflow.pick_list(plan) or workflow.order_labels(plan)
end

local ok, result = pcall(main)
if ok then assert(io.write(result),"cannot write marketplace output")
else
  io.stderr:write("Marketplaces: ",tostring(result):gsub("^.-:%d+: ",""),"\n")
  os.exit(1)
end
