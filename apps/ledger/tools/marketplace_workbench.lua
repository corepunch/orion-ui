-- POSIX offline operator workbench. The workspace directory must be private.
local repo=assert(arg[0]:match("^(.*)apps/ledger/tools/marketplace_workbench%.lua$"),"run from the checkout")
local json=dofile(repo.."share/lua/orion/json.lua")
local market=dofile(repo.."apps/ledger/share/addons/marketplaces/reconciliation.lua")
local pick=dofile(repo.."apps/ledger/share/addons/fulfillment/pick_lists.lua")
local limit=1048576
local usage=[[
usage: lua apps/ledger/tools/marketplace_workbench.lua WORKSPACE COMMAND ...
  sync SNAPSHOT.json [CAPTURE_EPOCH]   Import a complete fetched snapshot
  report                              Print local state and unresolved lines
  map SELLER_SKU VARIANT ITEM_ID BARCODE   Use an empty string for no variant
  scan ORDER_ID BARCODE UNIQUE_EVENT_ID
  pack ORDER_ID
  restart ORDER_ID stock-reconciled   Restart a changed eligible order after physical review
  pick-html                           Print outstanding resolved lines as HTML
  attach-label ORDER_ID LABEL_ID CARRIER.pdf
  reprint-label ORDER_ID LABEL_ID     Write the original carrier PDF to stdout
]]
local function read(path,max)
  local f=assert(io.open(path,"rb"),"cannot open input file")
  local value=f:read(max+1)
  assert(f:close())
  value=value or ""
  assert(#value<=max,"input exceeds size limit")
  return value
end
local function quote(s) return "'"..s:gsub("'","'\\''").."'" end
local function command_ok(command)
  local status=os.execute(command)
  return status==true or status==0
end
local forms={sync={3,4},report={2},map={6},scan={5},pack={3},restart={4},
  ["pick-html"]={2},["attach-label"]={5},["reprint-label"]={4}}
local command=arg[2]
if arg[1]=="--help" then io.write(usage); return end
local function run()
  local lengths=forms[command]
  assert(lengths and (#arg==lengths[1] or #arg==lengths[2]) and arg[1]~="",usage)
  local dir=arg[1]
  -- Prefix all relative paths so a directory beginning with '-' is never a flag.
  if dir:sub(1,1)~="/" then dir="./"..dir end
  local path, lock=dir.."/state.json",dir.."/.writer"
  local readonly={report=true,["pick-html"]=true,["reprint-label"]=true}
  local function state()
    local f,_,errno=io.open(path,"rb")
    if not f then
      assert(errno==2,"could not read marketplace workspace")
      return market.new()
    end
    local raw=f:read(limit+1) or ""
    assert(f:close())
    assert(#raw <= limit, "marketplace state exceeds size limit")
    return market.open(json.decode(raw))
  end
  if readonly[command] then
    local data=state()
    assert(data.scope,"no marketplace snapshot in this workspace")
    if command=="report" then return json.encode(market.report(data,json)).."\n" end
    if command=="reprint-label" then return market.label(data,arg[3],arg[4]) end
    return pick.pick_list(pick.plan(market.picks(data,json),json))
  end
  local snapshot,epoch,pdf
  if command=="sync" then
    snapshot=json.decode(read(arg[3],limit))
    if arg[4] then epoch=assert(tonumber(arg[4]),"invalid capture epoch") end
  elseif command=="attach-label" then pdf=read(arg[5],market.max_label) end
  assert(command_ok("umask 077 && mkdir "..quote(lock).." 2>/dev/null"),
    "cannot lock workspace; create it privately and make sure no other writer is active")
  local temp=lock.."/new"
  local ok,result=pcall(function()
    local data=state()
    local output
    if command=="sync" then output=market.sync(data,snapshot,epoch,json)
    elseif command=="map" then market.map(data,arg[3],arg[4]~="" and arg[4] or nil,arg[5],arg[6])
    elseif command=="scan" then output=market.scan(data,arg[3],arg[4],arg[5])
    elseif command=="pack" then market.pack(data,arg[3])
    elseif command=="restart" then
      assert(arg[4]=="stock-reconciled","confirm that physical picks and labels have been reconciled")
      market.restart(data,arg[3])
    elseif command=="attach-label" then market.attach(data,arg[3],arg[4],pdf) end
    local bytes=json.encode(data).."\n"
    assert(#bytes<=limit,"workspace exceeds 1 MiB; start a separately scoped workspace")
    local file=assert(io.open(temp,"wb"),"cannot create temporary marketplace state")
    local wrote=file:write(bytes)
    local closed=file:close()
    assert(wrote and closed,"cannot write marketplace state")
    assert(os.rename(temp,path),"cannot atomically publish marketplace state")
    return json.encode(output or {ok=true}).."\n"
  end)
  os.remove(temp)
  local released=os.remove(lock)
  assert(released,"failed to release marketplace workspace lock; inspect local state before retrying")
  if not ok then error(result,0) end
  return result
end
local ok,result=pcall(run)
if ok then assert(io.write(result),"cannot write marketplace output")
else io.stderr:write("Marketplaces: ",tostring(result):gsub("^.-:%d+: ",""),"\n"); os.exit(1) end
