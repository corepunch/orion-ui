-- Read-only, dependency-free rendering of one Ledger product card.
local M = {}

local function text(value, field, limit)
  assert(type(value) == "string" and #value <= limit and utf8.len(value)
    and value:match("%S") and not value:find("[%z\1-\31\127]"), "invalid " .. field)
  return value
end

local function escape(value)
  local map = {["&"]="&amp;", ["<"]="&lt;", [">"]="&gt;", ['"']="&quot;", ["'"]="&#39;"}
  return (value:gsub([[([&<>"'])]], map))
end

local function fields(obj, allowed, what)
  assert(type(obj) == "table" and getmetatable(obj) == nil, "expected " .. what .. " object")
  for key in pairs(obj) do assert(allowed[key], "unsupported " .. what .. " field") end
end

local alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local function encode64(bytes)
  local result = {}
  for i = 1, #bytes, 3 do
    local a, b, c = bytes:byte(i, i + 2)
    local n = (a << 16) | ((b or 0) << 8) | (c or 0)
    local function at(shift)
      local j = ((n >> shift) & 63) + 1
      return alphabet:sub(j, j)
    end
    result[#result+1] = at(18) .. at(12) .. (b and at(6) or "=") .. (c and at(0) or "=")
  end
  return table.concat(result)
end

M.MAX_PHOTO = 2 * 1024 * 1024
local function photo_type(bytes)
  assert(type(bytes) == "string" and #bytes >= 16 and #bytes <= M.MAX_PHOTO,
    "photo must be a local PNG or JPEG under 2 MiB")
  local kind, width, height
  if bytes:sub(1, 8) == "\137PNG\r\n\26\n" and bytes:sub(9, 16) == "\0\0\0\13IHDR" and #bytes >= 45 then
    kind = "png"
    width, height = string.unpack(">I4I4", bytes, 17)
  elseif bytes:sub(1, 2) == "\255\216" and bytes:sub(-2) == "\255\217" then
    local i = 3
    while i + 3 <= #bytes and bytes:byte(i) == 255 do
      i = i + 1
      while bytes:byte(i) == 255 do i = i + 1 end
      if i + 2 > #bytes then break end
      local marker = bytes:byte(i)
      if marker == 0xda or marker == 0xd9 then break end
      local size = string.unpack(">I2", bytes, i + 1)
      if size < 2 or i + size > #bytes then break end
      if (marker == 0xc0 or marker == 0xc1 or marker == 0xc2) and size >= 8 then
        kind = "jpeg"
        height, width = string.unpack(">I2I2", bytes, i + 4)
        break
      end
      i = i + size + 1
    end
  end
  assert(kind and width >= 1 and height >= 1 and width <= 4096 and height <= 4096
    and width * height <= 12000000, "unsupported photo type or dimensions (use PNG/JPEG, at most 12 megapixels)")
  return kind
end

local labels = {
  en={title="Product card",sku="Item code",size="Size",variant="Variant",quantity="Quantity snapshot",source="Source",as_of="As of",photo="Product photo"},
  ru={title="Карточка товара",sku="Артикул",size="Размер",variant="Вариант",quantity="Количество на дату",source="Источник",as_of="На дату",photo="Фото товара"},
  de={title="Produktkarte",sku="Artikelnummer",size="Maße",variant="Variante",quantity="Bestandsmomentaufnahme",source="Quelle",as_of="Stand",photo="Produktfoto"}
}

function M.select_item(service_response, id)
  assert(type(id) == "number" and math.type(id) == "integer" and id > 0, "invalid item ID")
  assert(type(service_response) == "table" and service_response.ok == true
    and type(service_response.result) == "table", "expected a successful catalogs.list response")
  local item
  for _, row in ipairs(service_response.result) do
    assert(type(row) == "table", "invalid catalog row")
    if row.id == id then
      assert(not item, "duplicate item ID in snapshot")
      item = row
    end
  end
  assert(item, "item not in catalog snapshot; export the page containing its ID")
  return item
end

function M.render(item, details, photo)
  assert(type(item) == "table" and type(item.id) == "number" and math.type(item.id) == "integer"
    and item.id > 0, "invalid Ledger item")
  details = details or {}
  fields(details, {language=true, size=true, variant=true, stock=true}, "product card details")
  local language = details.language or "en"
  local l = assert(labels[language], "language must be en, ru or de")
  local name = escape(text(item.name, "item name", 512))
  local result = { '<!doctype html>\n<html lang="' .. language .. '"><head>\n', [[<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'">
<style>
*{box-sizing:border-box}
body{margin:0;color:#181818;background:white;font:14pt system-ui,sans-serif}
main{max-width:175mm;margin:12mm auto;padding:8mm}
header{border-bottom:1px solid #999;padding-bottom:4mm;margin-bottom:5mm}
h1{font-size:24pt;line-height:1.18;margin:1mm 0;overflow-wrap:anywhere}
.kicker{font-size:11pt;color:#444}
img{display:block;width:auto;max-width:100%;height:auto;max-height:95mm;margin:0 auto 6mm;object-fit:contain}
dl{display:grid;grid-template-columns:minmax(6em,40%) 1fr;margin:0}
dt,dd{border-bottom:1px solid #ddd;margin:0;padding:2mm 0;overflow-wrap:anywhere}
dt{font-weight:600;padding-right:3mm}
@page{size:A4;margin:12mm}
@media print{main{max-width:none;margin:0;padding:0}header,dl,img{break-inside:avoid}}
</style>]],
    '<title>' .. name .. '</title></head><body><main><header><div class="kicker">' ..
    l.title .. '</div><h1>' .. name .. '</h1></header>' }
  if photo then
    result[#result+1] = '<img alt="' .. l.photo .. '" src="data:image/' .. photo_type(photo) .. ';base64,' .. encode64(photo) .. '">'
  end
  result[#result+1] = "<dl>"
  local function row(label, value, key, limit)
    if value ~= nil then
      result[#result+1] = '<dt>' .. label .. '</dt><dd>' .. escape(text(value, key, limit)) .. '</dd>'
    end
  end
  if item.sku and item.sku ~= "" then row(l.sku, item.sku, "item code", 128) end
  row(l.size, details.size, "size", 160)
  row(l.variant, details.variant, "variant", 256)
  if details.stock ~= nil then
    local stock = details.stock
    fields(stock, {quantity=true,unit=true,source=true,as_of=true}, "quantity snapshot")
    local quantity = text(stock.quantity, "quantity", 32)
    assert(quantity:match("^%-?%d+$") or quantity:match("^%-?%d+%.%d+$"), "invalid quantity")
    row(l.quantity, quantity .. " " .. text(stock.unit, "quantity unit", 24), "quantity and unit", 57)
    row(l.source, text(stock.source, "quantity source", 160), "quantity source", 160)
    local date = text(stock.as_of, "quantity date", 19)
    local y, m, d = date:match("^(%d%d%d%d)%-(%d%d)%-(%d%d)$")
    y, m, d = tonumber(y), tonumber(m), tonumber(d)
    local days = y and {31, y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0) and 29 or 28, 31,30,31,30,31,31,30,31,30,31}
    assert(y and y > 0 and days[m] and d >= 1 and d <= days[m], "invalid quantity date (use YYYY-MM-DD)")
    row(l.as_of, date, "quantity date", 19)
  end
  result[#result+1] = "</dl></main></body></html>\n"
  return table.concat(result, "\n")
end

return M
