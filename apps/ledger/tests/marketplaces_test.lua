local repo = assert(arg[0]:match("^(.*)apps/ledger/tests/marketplaces_test%.lua$"))
local dir = repo .. "apps/ledger/share/addons/marketplaces/"
local json = dofile(repo .. "share/lua/orion/json.lua")
local api = dofile(dir .. "marketplaces.lua")
local ebay = dofile(dir .. "ebay.lua")
local ozon = dofile(dir .. "ozon.lua")
local transport = dofile(dir .. "transport.lua")
local picking = dofile(repo .. "apps/ledger/share/addons/fulfillment/pick_lists.lua")
local checks = 0
local function eq(a,b)
  checks=checks+1
  assert(a==b,("%s != %s"):format(tostring(a),tostring(b)))
end
local function reject(fn, needle)
  local ok, err=pcall(fn)
  eq(ok,false)
  assert(tostring(err):find(needle,1,true),tostring(err))
end
local function fixture(name)
  local file=assert(io.open(repo .. "apps/ledger/tests/fixtures/marketplaces/" .. name .. ".json","rb"))
  local data=json.decode(file:read("*a")); file:close()
  return data
end
local function ebay_request(data)
  return function(r)
    eq(transport.allowed("ebay","sandbox",r),true)
    assert(r.url:find("limit=2",1,true))
    local pos = assert(tonumber(r.url:match("[?&]offset=(%d+)")))
    if r.url:find("/fulfillment/",1,true) then
      assert(r.url:find("creationdate%3A%5B",1,true))
      return assert(data.orders[1+pos//2])
    else return assert(data.stock[1+pos]) end
  end
end
local options={account="demo", environment="sandbox",page_size=2,max_pages=3,
  from="2026-01-01T00:00:00Z",until_date="2026-01-02T00:00:00Z"}
local opts=api.options(options,json)
local function run(data)
  return api.snapshot("ebay",opts,ebay_request(data),ebay,json)
end
local snap=run(fixture("ebay"))
eq(#snap.orders,4); eq(#snap.availability,5)
eq(snap.orders[1].lines[1].id,"L1")
eq(snap.orders[1].lines[1].sku,"0007")
eq(snap.orders[4].eligible,false)
eq(snap.orders[4].hold_reason,"missing_sku_or_quantity")
eq(snap.orders[4].lines[2].sku,nil)
eq(json.encode(snap):find("PRIVATE",1,true),nil)
local stock={}
for _,x in ipairs(snap.availability) do
  eq(x.available,nil)
  stock[x.source_location or x.sku]=x.listed_total
end
eq(stock["0007"],12); eq(stock["1008"],0); eq(stock.store,2); eq(stock["warehouse-a"],4)
local input=api.picking(snap,json)
eq(#input.orders,3)
eq(input.orders[1].id,"ebay/sandbox/demo/27-10001-00001")
local plan=picking.plan(input,json)
eq(#plan.picks,2)
eq(plan.picks[1].quantity,5)
eq(plan.picks[2].quantity,1)
eq(#plan.picks[1].orders,2)
local html=picking.pick_list(plan)
assert(html:find("00010",1,true) and not html:find("PRIVATE",1,true) and not html:find("OK-ONE",1,true))
local labelled=picking.order_labels(plan)
assert(labelled:find("Not a carrier shipping label",1,true) and not labelled:find("PRIVATE",1,true))
-- Barcode identity must not disappear merely because the SKU and variant match.
input.orders[1].lines[1].barcode="000123"
input.orders[3].lines[1].barcode="000124"
local distinct=picking.plan(input,json)
eq(#distinct.picks,3)
local with_barcodes=picking.pick_list(distinct)
assert(with_barcodes:find("<th>Barcode</th>",1,true) and with_barcodes:find("000123",1,true))
local a=fixture("ebay")
a.orders[1].orders[1].fulfillmentStartInstructions[2]={fulfillmentInstructionsType="DIGITAL"}
eq(run(a).orders[1].eligible,false)
a=fixture("ebay"); a.orders[1].orders[1].orderPaymentStatus="PENDING"
eq(run(a).orders[1].hold_reason,"not_confirmed_paid")
a=fixture("ebay"); a.orders[1].orders[1].cancelStatus=nil
eq(run(a).orders[1].hold_reason,"cancellation_unknown_or_pending")
a=fixture("ebay"); a.orders[1].orders[1].lineItems[1].lineItemFulfillmentStatus="FULFILLED"
eq(run(a).orders[1].hold_reason,"line_not_unstarted")
a=fixture("ebay"); a.orders[2].orders[1].orderId=a.orders[1].orders[1].orderId
reject(function() run(a) end,"duplicate eBay order")
a=fixture("ebay"); a.stock[1].next="https://other.example/sell/inventory/v1/inventory_item?offset=1"
reject(function() run(a) end,"inventory continuation")
a=fixture("ebay"); a.stock[1].next=a.stock[1].next:gsub("offset=1","offset=0")
reject(function() run(a) end,"inventory offset")
a=fixture("ebay"); a.orders[2].total=5
reject(function() run(a) end,"result changed")
local failopts=api.options(options,json); failopts.max_pages=1
reject(function() api.snapshot("ebay",failopts,ebay_request(fixture("ebay")),ebay,json) end,"no partial snapshot")
local wrong=api.options(options,json); wrong.mode="availability"
local only=api.snapshot("ebay",wrong,ebay_request(fixture("ebay")),ebay,json)
eq(#only.orders,0); reject(function() api.picking(only,json) end,"does not contain orders")
reject(function() api.options({account="demo",token="secret"},json) end,"keep credentials out")
reject(function() api.options({account="demo",from="2026-02-31T00:00:00Z",until_date=opts.until_date},json) end,"increasing")

for _, bad in ipairs({
  "https://api.sandbox.ebay.com.attacker.invalid/sell/fulfillment/v1/order",
  "https://api.sandbox.ebay.com@attacker.invalid/sell/fulfillment/v1/order",
  "https://api.sandbox.ebay.com/sell/fulfillment/v1/order/1/issue_refund",
  "https://api.sandbox.ebay.com/sell/fulfillment/v1/order?limit=1#x",
  "https://api.ebay.com/sell/fulfillment/v1/order",
}) do eq(not transport.allowed("ebay","sandbox",{method="GET",url=bad}),true) end
eq(not transport.allowed("ebay","sandbox",{method="POST",url="https://api.sandbox.ebay.com/sell/fulfillment/v1/order"}),true)
eq(not transport.allowed("ozon","production",{method="POST",url="https://api-seller.ozon.ru/v2/posting/fbs/ship",body={}}),true)

local of=fixture("ozon")
local idx={}
local endpoints={["/v4/posting/fbs/list"]="fbs",["/v3/posting/fbo/list"]="fbo",["/v4/product/info/stocks"]="stocks"}
local osnap=api.snapshot("ozon",api.options({
  account="demo",page_size=2,max_pages=3,from=opts.from,until_date=opts.until_date
},json),function(r)
  eq(transport.allowed("ozon","production",r),true)
  local path=r.url:match("^https://api%-seller%.ozon%.ru(.*)")
  local key=assert(endpoints[path])
  idx[key]=(idx[key] or 0)+1
  return assert(of[key][idx[key]])
end,ozon,json)
eq(osnap.provider,"ozon")
eq(#osnap.availability,4)
eq(osnap.orders[1].lines[1].source_item_id,"1904686181")
eq(api.picking(osnap,json).orders[1].id:sub(1,21),"ozon/production/demo/")
print(("Marketplace pipeline: %d checks passed"):format(checks))
