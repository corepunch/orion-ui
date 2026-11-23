-- Normalize one explicitly selected seller account, then hand off to the
-- existing fulfillment planner. No marketplace or Ledger mutation here.
local M = {}
local function item_text(v, max)
  return type(v)=="string" and #v<=max and utf8.len(v) and not v:find("[%z\1-\31\127]")
    and v:find("%S") and v==v:match("^%s*(.-)%s*$")
end
local function bounded(v, low, high)
  return math.type(v)=="integer" and v>=low and v<=high
end

local function date(v)
  if type(v) ~= "string" then return false end
  local y,m,d,h,n,s = v:match("^(%d%d%d%d)-(%d%d)-(%d%d)T(%d%d):(%d%d):(%d%d)Z$")
  if not y then return false end
  y,m,d,h,n,s = tonumber(y),tonumber(m),tonumber(d),tonumber(h),tonumber(n),tonumber(s)
  local leap = y%4==0 and (y%100~=0 or y%400==0)
  local days = ({31,leap and 29 or 28,31,30,31,30,31,31,30,31,30,31})[m]
  return y>=2000 and days and d>0 and d<=days and h<24 and n<60 and s<60
end

function M.options(options, json)
  assert(type(options)=="table" and getmetatable(options)==nil,"expected market options object")
  local supported = {account=true,environment=true,mode=true,page_size=true,max_pages=true,from=true,until_date=true}
  for key in pairs(options) do assert(supported[key],"unexpected marketplace option; keep credentials out of JSON") end
  assert(type(options.account)=="string" and #options.account <= 24 and options.account:match("^[A-Za-z0-9_.-]+$"),
    "account must be an internal label of 1-24 ASCII letters, numbers, dots, underscores or hyphens")
  local out = {}
  for key, value in pairs(options) do
    assert(value ~= json.null,"marketplace options must not be null")
    out[key] = value
  end
  out.environment = out.environment or "production"
  assert(out.environment=="production" or out.environment=="sandbox","invalid marketplace environment")
  out.mode = out.mode or "both"
  out.max_pages = out.max_pages or 20
  assert(out.mode=="both" or out.mode=="orders" or out.mode=="availability","invalid marketplace mode")
  assert(out.page_size==nil or bounded(out.page_size,1,100),"page_size must be 1-100")
  assert(out.max_pages==nil or bounded(out.max_pages,1,50),"max_pages must be 1-50")
  if out.from ~= nil or out.until_date ~= nil then
    assert(date(out.from) and date(out.until_date) and out.from<out.until_date,
      "use an increasing from/until_date interval in UTC YYYY-MM-DDTHH:MM:SSZ")
  end
  return out
end

local function copy_text(src, dst, key, max, required)
  local v = src[key]
  assert((not required and v==nil) or item_text(v,max),"invalid marketplace field: " .. key)
  dst[key] = v
end

local function copy_quantity(src,dst,key,max,minimum)
  local v = src[key]
  assert(v==nil or bounded(v,minimum or 0,max),"invalid marketplace field: " .. key)
  dst[key] = v
end

local function array(src,max)
  assert(type(src)=="table" and #src<=max,"invalid marketplace collection")
  local n = 0
  for key in pairs(src) do
    assert(bounded(key,1,#src),"invalid marketplace collection")
    n = n + 1
  end
  assert(n==#src,"invalid marketplace collection")
  return src
end

function M.snapshot(provider, opts, request, adapter, json)
  assert(provider=="ebay" or provider=="ozon","unknown marketplace")
  local data = adapter.fetch(opts,request)
  local out = {schema="orion-marketplace-v1",provider=provider,account=opts.account,
    environment=opts.environment,mode=opts.mode,orders=json.array(),availability=json.array(),warnings=json.array()}
  if opts.mode~="availability" then out.from,out.until_date=opts.from,opts.until_date end
  local order_ids, availability_keys, warnings = {}, {}, {}
  for _, upstream in ipairs(array(data.orders,10000)) do
    assert(type(upstream)=="table","invalid marketplace order")
    local row = {lines=json.array(),eligible=upstream.eligible==true}
    copy_text(upstream,row,"id",64,true)
    copy_text(upstream,row,"status",64,true)
    copy_text(upstream,row,"hold_reason",160,false)
    copy_text(upstream,row,"source_order_id",128,false)
    copy_text(upstream,row,"source_order_number",100,false)
    copy_text(upstream,row,"fulfillment",64,false)
    assert(not order_ids[row.id],"duplicate marketplace order")
    order_ids[row.id] = true
    local seen = {}
    for _, src in ipairs(array(upstream.lines,4000)) do
      assert(type(src)=="table","invalid marketplace order line")
      local line = {}
      copy_text(src,line,"id",128,true)
      assert(not seen[line.id],"duplicate marketplace line")
      seen[line.id] = true
      copy_text(src,line,"sku",100,false)
      copy_text(src,line,"source_item_id",128,false)
      copy_text(src,line,"source_variant_id",128,false)
      copy_text(src,line,"variant",512,false)
      copy_text(src,line,"barcode",100,false)
      copy_text(src,line,"name",512,false)
      copy_quantity(src,line,"quantity",1000000,1)
      assert(not row.eligible or (line.sku and line.quantity),"eligible marketplace order contains an incomplete line")
      row.lines[#row.lines+1] = line
    end
    assert(not row.eligible or (#row.lines>0 and row.hold_reason==nil),"invalid eligible marketplace order")
    table.sort(row.lines,function(a,b) return a.id<b.id end)
    if not row.eligible then row.hold_reason = row.hold_reason or "provider_status_not_pickable" end
    out.orders[#out.orders+1] = row
  end
  for _, src in ipairs(array(data.availability,30000)) do
    assert(type(src)=="table","invalid marketplace availability")
    local row = {}
    copy_text(src,row,"source_item_id",128,false)
    copy_text(src,row,"sku",100,not row.source_item_id)
    copy_text(src,row,"shipment_type",64,false)
    copy_text(src,row,"variant",512,false)
    copy_text(src,row,"source_location",128,false)
    copy_text(src,row,"location_kind",64,true)
    copy_text(src,row,"basis",96,true)
    copy_quantity(src,row,"available",100000000)
    copy_quantity(src,row,"reserved",100000000)
    copy_quantity(src,row,"listed_total",100000000)
    assert(row.available~=nil or row.reserved~=nil or row.listed_total~=nil,"empty marketplace availability quantity")
    -- Control separators are forbidden by copy_text.
    local key = table.concat({row.sku or "",row.variant or "",row.source_item_id or "",row.location_kind,row.source_location or "",row.shipment_type or "",row.basis},"\0")
    assert(not availability_keys[key],"duplicate marketplace availability scope")
    availability_keys[key] = true
    out.availability[#out.availability+1] = {key=key,row=row}
  end
  for _, warning in ipairs(array(data.warnings,30000)) do
    assert(item_text(warning,512),"invalid marketplace warning")
    if not warnings[warning] then out.warnings[#out.warnings+1]=warning; warnings[warning]=true end
  end
  table.sort(out.orders,function(a,b) return a.id<b.id end)
  table.sort(out.availability,function(a,b) return a.key<b.key end)
  for i, value in ipairs(out.availability) do out.availability[i] = value.row end
  table.sort(out.warnings)
  return out
end

function M.picking(snapshot, json)
  local batch = {orders=json.array()}
  assert(snapshot.mode~="availability","availability does not contain orders")
  local identity = snapshot.provider .. "/" .. snapshot.environment .. "/" .. snapshot.account
  -- The local alias is not the buyer's name or delivery address. Bound the
  -- namespaced IDs to the same field budget as the existing pick planner.
  local within = identity .. "/"
  for _, order in ipairs(snapshot.orders) do
    if order.eligible then
      local id = within .. order.id
      assert(#id <= 100,"namespaced order ID exceeds the pick format; choose a shorter account label")
      local lines = json.array()
      for _, line in ipairs(order.lines) do
        lines[#lines+1] = {sku=line.sku,variant=line.variant,barcode=line.barcode,quantity=line.quantity}
      end
      batch.orders[#batch.orders+1] = {id=id,recipient=identity,status="open",selected=true,lines=lines}
    end
  end
  assert(#batch.orders <= 250,"too many eligible orders for one pick batch; narrow the interval")
  return batch
end

return M
