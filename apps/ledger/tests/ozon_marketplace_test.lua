local json = dofile("share/lua/orion/json.lua")
local plugin = dofile("apps/ledger/share/addons/marketplaces/ozon.lua")
local f = assert(io.open("apps/ledger/tests/fixtures/marketplaces/ozon.json", "rb"))
local source = f:read("*a")
f:close()

local count = 0
local function eq(a, b) count = count + 1; assert(a == b, tostring(a) .. " ~= " .. tostring(b)) end
local base = "https://api-seller.ozon.ru"
local endpoints = {[base .. "/v4/posting/fbs/list"]="fbs", [base .. "/v3/posting/fbo/list"]="fbo",
                   [base .. "/v4/product/info/stocks"]="stocks"}
local opts = {account="seller-de-01",environment="production",from="2026-11-01T00:00:00Z",
              until_date="2026-11-24T23:59:59.9Z",page_size=2}
local function copy(t)
  local r = {}
  for k,v in pairs(t) do r[k] = v end
  return r
end
local function transport(data)
  local calls = {}
  local function request(req)
    eq(req.method, "POST")
    eq(req.headers, nil)
    local name = assert(endpoints[req.url], "unexpected endpoint")
    local requests = (calls[name] or 0) + 1
    calls[name] = requests
    eq(req.body.limit, 2)
    if name == "stocks" then
      eq(req.body.filter.visibility, "ALL")
    else
      eq(req.body.filter.since, opts.from)
      eq(req.body.filter.to, opts.until_date)
      eq(req.body.with, nil)
    end
    eq(req.body.cursor, requests == 1 and "" or name == "fbs" and "next-fbs==" or "stock-2")
    return assert(data[name][requests], "unexpected request count")
  end
  return request, calls
end
local function rejects(data, config, expected)
  local ok, err = pcall(plugin.fetch, config, (transport(data)))
  eq(ok, false)
  assert(type(err) == "string" and err:find(expected, 1, true), tostring(err))
end
local request, calls = transport(json.decode(source))
local out = plugin.fetch(opts, request)
eq(calls.fbs, 2); eq(calls.fbo, 1); eq(calls.stocks, 2)
eq(#out.orders, 5)
eq(out.orders[1].id, "02001-1000-1")
eq(out.orders[1].source_order_number, "02001-1000")
eq(out.orders[1].source_order_id, "33301885134")
eq(out.orders[1].account, opts.account)
eq(out.orders[1].status, "awaiting_packaging")
eq(out.orders[1].eligible, true)
eq(out.orders[1].lines[1].sku, "0005-BLUE")
eq(out.orders[1].lines[1].variant, "0005-BLUE")
eq(out.orders[1].lines[1].source_item_id, "1904686181")
eq(out.orders[1].lines[1].quantity, 2)
eq(out.orders[1].lines[1].barcode, nil)
eq(out.orders[2].lines[1].sku, nil)
eq(out.orders[2].eligible, false)
eq(out.orders[3].status, "cancelled")
eq(out.orders[3].eligible, false)
eq(out.orders[4].status, "awaiting_deliver")
eq(out.orders[4].eligible, false)
eq(out.orders[5].fulfillment, "fbo")
eq(out.orders[5].eligible, false)
local output = json.encode(out)
eq(output:find("EXCLUDE", 1, true), nil)
eq(#out.availability, 4)
eq(out.availability[1].listed_total, 8)
eq(out.availability[1].reserved, 3)
eq(out.availability[1].available, 5)
eq(out.availability[1].location_kind, "fulfillment_scheme")
eq(out.availability[2].source_location, "fbo")
eq(out.availability[3].available, 0)
eq(out.availability[4].sku, nil)
eq(out.availability[4].available, nil)
eq(#out.warnings, 3)

local stocks_only = copy(opts); stocks_only.mode = "availability"
stocks_only.from, stocks_only.until_date = nil, nil
request, calls = transport(json.decode(source))
eq(#plugin.fetch(stocks_only, request).orders, 0)
eq(calls.fbs, nil); eq(calls.fbo, nil)
local orders_only = copy(opts); orders_only.mode = "orders"
request, calls = transport(json.decode(source))
eq(#plugin.fetch(orders_only, request).availability, 0)
eq(calls.stocks, nil)

local data = json.decode(source)
data.fbs[1].result.cursor = nil
rejects(data, opts, "continuation is missing")
data = json.decode(source)
data.fbs[2].cursor, data.fbs[2].has_next = "next-fbs==", true
rejects(data, opts, "repeated")
data = json.decode(source)
data.stocks[2].total = 4
rejects(data, opts, "unstable Ozon stock item total")
data = json.decode(source)
data.fbo[1].postings[1].posting_number = "02001-1000-1"
rejects(data, opts, "duplicate Ozon posting")
data = json.decode(source)
data.fbs[1].result.postings[1].products[1].digital_codes = {"CODE-PRIVATE"}
local held = plugin.fetch(opts, (transport(data)))
eq(held.orders[1].eligible, false)
eq(json.encode(held):find("CODE-PRIVATE", 1, true), nil)
data = json.decode(source)
data.fbs[1].result.postings[1].cancellation = {cancel_reason_id="123", cancelled_after_ship=false}
eq(plugin.fetch(opts, (transport(data))).orders[1].eligible, false)
data = json.decode(source)
data.fbs[1].result.postings[1].products[2] = {offer_id="SECOND",sku=14527}
eq(plugin.fetch(opts, (transport(data))).orders[1].eligible, false)
local raw_transport_error = "clean transport status 429"
local ok, err = pcall(plugin.fetch, opts, function() error(raw_transport_error,0) end)
eq(ok,false); eq(err,raw_transport_error)
local bad = copy(opts); bad.environment = "sandbox"
rejects(json.decode(source), bad, "production")
bad = copy(opts); bad.max_pages = 1
rejects(json.decode(source), bad, "page limit")
bad = copy(opts); bad.until_date = "2026-02-30T23:59:59Z"
rejects(json.decode(source), bad, "posting window")
print(("Ozon marketplace: %d checks passed"):format(count))
