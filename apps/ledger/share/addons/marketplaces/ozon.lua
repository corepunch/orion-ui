-- Ozon Seller API, read-only queries over an injected credential-aware transport.
-- HTTPS POST is used by the provider for these list operations, not for writes.
local M = {}
local ORIGIN = "https://api-seller.ozon.ru"
local FBS, FBO, STOCK = "/v4/posting/fbs/list", "/v3/posting/fbo/list", "/v4/product/info/stocks"

local function text(value, max)
  if type(value) == "string" and #value > 0 and #value <= max and utf8.len(value)
    and not value:find("[%z\1-\31\127]") and value == value:match("^%s*(.-)%s*$") then return value end
end

local function source_id(value)
  if math.type(value) == "integer" and value > 0 then return tostring(value) end
  if type(value) == "string" and #value <= 32 and value:match("^%d+$") and value:find("[1-9]") then return value end
end

local function uint(n)
  if math.type(n) == "integer" and n >= 0 then return n end
end

local function list(value, what, max)
  assert(type(value) == "table" and #value <= max, "invalid Ozon " .. what)
  local count = 0
  for k in pairs(value) do
    assert(math.type(k) == "integer" and k >= 1 and k <= #value, "invalid Ozon " .. what)
    count = count + 1
  end
  assert(count == #value, "invalid Ozon " .. what)
  return value
end

-- Validate calendar dates and UTC, without depending on the machine timezone.
local function timestamp(value)
  if type(value) ~= "string" or #value > 31 then return end
  local y, m, d, h, mi, s, sub = value:match("^(%d%d%d%d)%-(%d%d)%-(%d%d)T(%d%d):(%d%d):(%d%d)([.%d]*)Z$")
  if not y or #sub > 10 or (sub ~= "" and not sub:match("^%.%d+$")) then return end
  y, m, d, h, mi, s = tonumber(y), tonumber(m), tonumber(d), tonumber(h), tonumber(mi), tonumber(s)
  if y < 2000 or m < 1 or m > 12 or h > 23 or mi > 59 or s > 59 then return end
  local leap = y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0)
  local months = {31, leap and 29 or 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
  if d < 1 or d > months[m] then return end
  local previous = y - 1
  local days = previous * 365 + previous // 4 - previous // 100 + previous // 400
  for i = 1, m - 1 do days = days + months[i] end
  local fraction = sub == "" and "" or sub:sub(2)
  return ((days + d) * 24 + h) * 3600 + mi * 60 + s, fraction .. string.rep("0", 9 - #fraction)
end

local function paging(request, path, size, page_cap, body, rows_name, accept, stock)
  local cursor, seen, total, count = "", {}, nil, 0
  for _ = 1, page_cap do
    body.cursor = cursor
    local response = request({method="POST", url=ORIGIN .. path, body=body})
    assert(type(response) == "table", "invalid Ozon response")
    -- FBS v4 has appeared both with a result envelope and as a top-level
    -- collection. Accept either shape, never two competing collections.
    local top, envelope = response[rows_name], response.result
    if envelope ~= nil then
      assert(path == FBS and top == nil and type(envelope) == "table", "ambiguous Ozon response envelope")
      response = envelope
    end
    local rows = list(response[rows_name], "page rows", size)
    local next_cursor = response.cursor
    assert(next_cursor == nil or next_cursor == "" or text(next_cursor, 8192), "invalid Ozon cursor")
    if stock then
      local reported = uint(response.total)
      assert(reported and (total == nil or total == reported), "unstable Ozon stock item total")
      total, count = reported, count + #rows
      assert(count <= total, "Ozon stock exceeded its reported total")
      if count == total then
        for _, row in ipairs(rows) do accept(row) end
        return
      end
    else
      -- Some protobuf responses omit scalar false. Only infer EOF if there
      -- is no next cursor; a contradictory response is rejected.
      local more = response.has_next
      assert(more == nil or type(more) == "boolean", "invalid Ozon more-pages flag")
      if more == false or (more == nil and (next_cursor == nil or next_cursor == "")) then
        for _, row in ipairs(rows) do accept(row) end
        return
      end
    end
    assert(#rows > 0 and next_cursor and next_cursor ~= "" and next_cursor ~= cursor and not seen[next_cursor],
      "Ozon continuation is missing, empty, or repeated")
    for _, row in ipairs(rows) do accept(row) end
    cursor, seen[next_cursor] = next_cursor, true
  end
  error("Ozon page limit reached; no partial preview was exported", 0)
end

local function posting_identity(row, scheme)
  assert(type(row) == "table", "invalid Ozon posting")
  local id = text(row.posting_number, 64)
  local raw = text(row.status or row.status_alias, 64)
  assert(id and raw and raw:match("^[a-zA-Z0-9_-]+$"), "missing Ozon posting ID or status")
  local delivery = row.delivery_schema
  if delivery ~= nil then
    assert(text(delivery, 32) and delivery:match("^[a-zA-Z0-9_-]+$"), "invalid Ozon delivery scheme")
    delivery = delivery:lower()
  end
  return id, raw:lower(), delivery or scheme
end

local function add_posting(rows, seen, warnings, account, scheme, row)
  local id, status, delivery = posting_identity(row, scheme)
  assert(not seen[id], "duplicate Ozon posting; rerun the preview")
  seen[id] = true
  local products = list(row.products, "posting products", 4000)
  local cancel = row.cancellation
  local cancelled = source_id(row.cancel_reason_id) or
    (type(cancel) == "table" and (source_id(cancel.cancel_reason_id) or cancel.cancelled_after_ship == true))
  local eligible = not cancelled and scheme == "fbs" and (delivery == "fbs" or delivery == "rfbs")
    and status == "awaiting_packaging" and #products > 0
  local lines, line_ids, incomplete = {}, {}, false
  for i, item in ipairs(products) do
    assert(type(item) == "table", "invalid Ozon posting product")
    local offer = text(item.offer_id or item.product_offer_id, 100)
    local remote_id = source_id(item.sku) or source_id(item.product_id)
    local line_id = remote_id or (offer and ("offer:" .. offer))
    local quantity = uint(item.quantity)
    if quantity and (quantity == 0 or quantity > 1000000) then quantity = nil end
    if not (offer and line_id and quantity) then incomplete = true end
    local duplicated = line_id and line_ids[line_id]
    if duplicated then incomplete = true end
    if line_id then line_ids[line_id] = true end
    local digital_codes = item.digital_codes
    local digital = (digital_codes ~= nil and #list(digital_codes, "digital codes", 4000) > 0)
      or item.is_digital == true
    if digital or item.is_marketplace_buyout == true then incomplete = true end
    lines[#lines + 1] = {
      id=(line_id and not duplicated) and line_id or ("unresolved-line:" .. i),
      sku=offer, variant=offer, source_item_id=remote_id,
      name=text(item.name or item.product_name, 512), quantity=quantity,
    }
  end
  if eligible and incomplete then warnings[#warnings + 1] = "Ozon posting held: incomplete or non-physical lines" end
  if scheme == "fbs" and #products == 0 then warnings[#warnings + 1] = "Ozon posting held: no product lines" end
  rows[#rows + 1] = {
    id=id, source_order_id=source_id(row.order_id), source_order_number=text(row.order_number, 100),
    source="ozon", account=account, status=status, fulfillment=delivery, lines=lines, eligible=eligible and not incomplete,
    hold_reason=cancelled and "cancelled_or_pending" or incomplete and "incomplete_or_nonphysical_lines"
      or not eligible and "not_seller_pickable" or nil,
  }
end

local function add_stock(rows, seen, warnings, account, row)
  assert(type(row) == "table", "invalid Ozon stock item")
  local sku = text(row.offer_id, 100)
  local product_id = source_id(row.product_id)
  if not sku then warnings[#warnings + 1] = "Ozon stock item held: missing seller article" end
  for _, entry in ipairs(list(row.stocks, "stock counts", 100)) do
    assert(type(entry) == "table", "invalid Ozon stock count")
    local scheme = text(entry.type, 32)
    local shipment = entry.shipment_type == nil and "" or text(entry.shipment_type, 64)
    local stock_sku = source_id(entry.sku)
    local present, reserved = uint(entry.present), uint(entry.reserved)
    assert(scheme and scheme:match("^[a-zA-Z0-9_-]+$") and shipment
      and (shipment == "" or shipment:match("^[a-zA-Z0-9_-]+$")), "invalid Ozon stock classification")
    assert(present and reserved, "missing Ozon stock quantities")
    if sku then
      local key = (product_id or sku) .. "\0" .. (stock_sku or "") .. "\0" .. scheme .. "\0" .. shipment
      assert(not seen[key], "duplicate Ozon stock row; rerun the preview")
      seen[key] = true
    end
    if reserved > present then warnings[#warnings + 1] = "Ozon stock held: reserved exceeds present" end
    rows[#rows + 1] = {
      source="ozon", account=account, sku=sku, variant=sku, source_item_id=stock_sku or product_id,
      source_location=scheme, location_kind="fulfillment_scheme", shipment_type=shipment ~= "" and shipment or nil,
      listed_total=present, reserved=reserved, available=reserved <= present and present - reserved or nil,
      basis="ozon_reported_present_minus_reserved_by_fulfillment_scheme",
    }
  end
end

function M.fetch(opts, request)
  assert(type(opts) == "table" and type(request) == "function", "Ozon options and transport are required")
  local account = text(opts.account, 100)
  local mode = opts.mode or "both"
  local size, cap = opts.page_size or 100, opts.max_pages or 100
  assert(account and account:match("^[a-zA-Z0-9_.-]+$"), "invalid Ozon local account label")
  assert(opts.environment == "production", "Ozon only supports the production Seller API")
  assert(mode == "orders" or mode == "availability" or mode == "both", "invalid Ozon preview mode")
  assert(uint(size) and size >= 1 and size <= 500 and uint(cap) and cap >= 1 and cap <= 1000,
    "invalid Ozon pagination bounds")
  if mode ~= "availability" then
    local start, sf = timestamp(opts.from)
    local finish, ff = timestamp(opts.until_date)
    assert(start and finish and (finish - start < 365 * 86400 or finish - start == 365 * 86400 and ff <= sf)
      and (finish > start or finish == start and ff > sf), "invalid Ozon posting window; use UTC and at most 365 days")
  end
  local out = {orders={}, availability={}, warnings={}}
  if mode ~= "availability" then
    local seen = {}
    for _, endpoint in ipairs({{FBS, "fbs"}, {FBO, "fbo"}}) do
      -- Do not request optional account, customer, address, payment, or
      -- barcode fields; provider-supplied extraneous values are not exported.
      local body = {filter={since=opts.from,to=opts.until_date}, limit=size, sort_dir="asc"}
      paging(request, endpoint[1], size, cap, body, "postings", function(row)
        add_posting(out.orders, seen, out.warnings, account, endpoint[2], row)
      end)
    end
    table.sort(out.orders, function(a,b) return a.id < b.id end)
  end
  if mode ~= "orders" then
    local seen = {}
    paging(request, STOCK, size, cap, {filter={visibility="ALL"},limit=size}, "items", function(row)
      add_stock(out.availability, seen, out.warnings, account, row)
    end, true)
  end
  return out
end

return M
