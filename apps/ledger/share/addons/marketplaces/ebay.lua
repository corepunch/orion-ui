-- Seller-authorized eBay read-only adapter. Credentials stay in the transport.
local M = {}
local origins = {production="https://api.ebay.com", sandbox="https://api.sandbox.ebay.com"}
local ORDER, INV = "/sell/fulfillment/v1/order", "/sell/inventory/v1/inventory_item"

local function text(value, max)
  if type(value) == "string" and #value <= max and value:find("%S") and utf8.len(value)
    and not value:find("[%z\1-\31\127]") and value == value:match("^%s*(.-)%s*$") then return value end
end

local function integer(v, lower, upper)
  return math.type(v) == "integer" and v >= lower and v <= upper
end

local function array(v)
  if type(v) ~= "table" then return false end
  local n, c = #v, 0
  for k in pairs(v) do
    if not integer(k,1,n) then return false end
    c = c + 1
  end
  return c == n
end

local function encode(value)
  return (value:gsub("([^A-Za-z0-9_.~-])", function(c) return ("%%%02X"):format(c:byte()) end))
end

-- Use documented offset/total with fixed endpoints, never server URLs.
local function collect(request, origin, path, field, opts, filter, add, warnings)
  local offset, count, limit, total = 0, 0, opts.page_size or 100
  for _ = 1, opts.max_pages or 20 do
    local url = origin .. path .. "?limit=" .. limit .. "&offset=" .. offset
    if filter then url = url .. "&filter=" .. encode(filter) end
    local page = request({method="GET",url=url})
    assert(type(page)=="table" and integer(page.total,0,1000000),"eBay returned no reliable page count")
    assert(not total or page.total==total,"eBay result changed during pagination; retry")
    total = page.total
    local rows = page[field]
    if total == 0 and rows == nil then rows = {} end
    assert(array(rows) and #rows <= limit and count + #rows <= total,"invalid eBay page")
    assert(page.offset == nil or page.offset == offset,"eBay returned a different page offset")
    if page.warnings ~= nil then warnings[#warnings+1] = "eBay returned API warnings; compare the snapshot with Seller Hub" end
    for _, row in ipairs(rows) do add(row) end
    count = count + #rows
    if count == total then
      assert(page.next == nil or page.next == "","eBay pagination count contradicts continuation")
      return
    end
    assert(#rows > 0,"eBay pagination did not advance")
    if path == INV then
      -- Inventory's documented next link supplies pagination state. Use
      -- only its bounded numeric offset; reconstruct the URL ourselves.
      local next_url = page.next
      local prefix = origin .. path .. "?"
      assert(type(next_url)=="string" and #next_url<=1024 and next_url:sub(1,#prefix)==prefix,
        "invalid eBay inventory continuation")
      local next_offset
      for arg in next_url:sub(#prefix+1):gmatch("[^&]+") do
        if arg:match("^offset=") then
          assert(not next_offset and arg:match("^offset=%d+$"),"invalid eBay inventory offset")
          next_offset = tonumber(arg:sub(8))
        end
      end
      assert(integer(next_offset,offset+1,offset+#rows),"invalid eBay inventory offset")
      offset = next_offset
    else offset = offset + #rows end
  end
  error("eBay page limit reached; no partial snapshot exported")
end

local function seller_ships(order)
  local steps = order.fulfillmentStartInstructions
  if not array(steps) or #steps == 0 then return false end
  for _, step in ipairs(steps) do
    if type(step) ~= "table" or step.fulfillmentInstructionsType ~= "SHIP_TO" then return false end
  end
  return true
end

local function order_row(raw)
  assert(type(raw)=="table","invalid eBay order")
  local id = assert(text(raw.orderId,64),"eBay order has no usable ID")
  local status = text(raw.orderFulfillmentStatus,64) or "UNKNOWN"
  local reason
  if status ~= "NOT_STARTED" then reason = "not_unstarted"
  elseif raw.orderPaymentStatus ~= "PAID" then reason = "not_confirmed_paid"
  elseif type(raw.cancelStatus) ~= "table" or raw.cancelStatus.cancelState ~= "NONE" then reason = "cancellation_unknown_or_pending"
  elseif not seller_ships(raw) then reason = "not_confirmed_seller_shipping" end
  local lines, seen = {}, {}
  if not array(raw.lineItems) or #raw.lineItems == 0 then reason = reason or "no_line_items"
  else
    for _, item in ipairs(raw.lineItems) do
      local line_id = type(item)=="table" and text(item.lineItemId,96)
      if not line_id or seen[line_id] then
        reason = reason or "invalid_or_duplicate_line_id"
      else
        seen[line_id] = true
        local sku = text(item.sku,100)
        local qty = integer(item.quantity,1,1000000) and item.quantity or nil
        local variation = text(item.legacyVariationId,96)
        local listing = text(item.legacyItemId,96)
        local variant
        if variation and variation ~= "0" and listing then variant = "eBay listing " .. listing .. " variation " .. variation
        elseif item.legacyVariationId ~= nil and item.legacyVariationId ~= "" and item.legacyVariationId ~= "0" then
          reason = reason or "variation_identity_incomplete"
        end
        if not sku or not qty then reason = reason or "missing_sku_or_quantity" end
        if item.lineItemFulfillmentStatus ~= "NOT_STARTED" then reason = reason or "line_not_unstarted" end
        lines[#lines+1] = {id=line_id,sku=sku,variant=variant,name=text(item.title,512),quantity=qty,
          source_item_id=listing,source_variant_id=variation}
      end
    end
  end
  return {id=id,status=status,eligible=not reason,hold_reason=reason,lines=lines}
end

local function count_or_fail(v)
  if v == nil then return nil end
  assert(integer(v,0,100000000),"invalid eBay availability quantity")
  return v
end

local function stock_rows(raw, add)
  assert(type(raw)=="table","invalid eBay inventory item")
  local sku = assert(text(raw.sku,100),"eBay inventory item has no usable SKU")
  local stock = raw.availability
  assert(stock == nil or type(stock)=="table","invalid eBay availability")
  stock = stock or {}
  local ship = stock.shipToLocationAvailability
  if ship ~= nil then
    assert(type(ship)=="table","invalid eBay shipping availability")
    local listed = count_or_fail(ship.quantity)
    if listed ~= nil then add({sku=sku,location_kind="shipping_total",listed_total=listed,basis="ebay_inventory_shipping_total"}) end
    if ship.availabilityDistributions ~= nil then
      assert(array(ship.availabilityDistributions),"invalid eBay shipping locations")
      for _, location in ipairs(ship.availabilityDistributions) do
        assert(type(location)=="table","invalid eBay shipping location")
        local key = assert(text(location.merchantLocationKey,128),"eBay location has no key")
        local quantity = count_or_fail(location.quantity)
        if quantity ~= nil then add({sku=sku,location_kind="shipping_location",source_location=key,
          listed_total=quantity,basis="ebay_inventory_shipping_distribution"}) end
      end
    end
  end
  if stock.pickupAtLocationAvailability ~= nil then
    assert(array(stock.pickupAtLocationAvailability),"invalid eBay pickup locations")
    for _, place in ipairs(stock.pickupAtLocationAvailability) do
      assert(type(place)=="table","invalid eBay pickup location")
      local key = assert(text(place.merchantLocationKey,128),"eBay pickup location has no key")
      local quantity = count_or_fail(place.quantity)
      if quantity ~= nil then add({sku=sku,location_kind="pickup_location",source_location=key,
        listed_total=quantity,basis="ebay_inventory_pickup_quantity"}) end
    end
  end
end

function M.fetch(opts, request)
  local origin = assert(origins[opts.environment],"invalid eBay environment")
  local result, order_ids, stock_ids = {orders={},availability={},warnings={}}, {}, {}
  if opts.mode ~= "availability" then
    local filter = opts.from and ("creationdate:[" .. opts.from .. ".." .. opts.until_date .. "]")
    collect(request,origin,ORDER,"orders",opts,filter,function(raw)
      local row = order_row(raw)
      assert(not order_ids[row.id],"duplicate eBay order across pages; retry")
      order_ids[row.id] = true
      result.orders[#result.orders+1] = row
    end,result.warnings)
  end
  if opts.mode ~= "orders" then
    result.warnings[#result.warnings+1] = "eBay Inventory API omits Seller Hub listings with no Inventory API record"
    collect(request,origin,INV,"inventoryItems",opts,nil,function(raw)
      local sku = type(raw)=="table" and text(raw.sku,100)
      assert(sku and not stock_ids[sku],"invalid or repeated eBay inventory item; retry")
      stock_ids[sku] = true
      stock_rows(raw,function(row) result.availability[#result.availability+1] = row end)
    end,result.warnings)
  end
  return result
end

return M
