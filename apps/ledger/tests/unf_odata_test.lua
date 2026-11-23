local json = dofile("share/lua/orion/json.lua")
local unf = dofile("apps/ledger/share/stdplugs/one_c/unf_odata.lua")
local metadata = assert(io.open("apps/ledger/tests/fixtures/unf/metadata.xml"))
local xml = metadata:read("*a")
metadata:close()
local base = "https://example.invalid/tenant/odata/standard.odata"
local id = "aa000000-0000-0000-0000-000000000001"
local cp = "aa000000-0000-0000-0000-000000000002"
local wh = "aa000000-0000-0000-0000-000000000003"
local settings = {
  endpoint="https://EXAMPLE.invalid:443/tenant/",
  page_size=2,
  stock={register="AccumulationRegister_StockFixture", period="2026-11-01T00:00:00",
    dimensions={"Item","Warehouse"}, item_field="Item_Key", warehouse_field="Warehouse_Key",
    quantity_field="QuantityBalance", unit="pcs"},
}
local calls = 0
local item_link, status_override, repeated, emptytruncated, missingquantity, stockcalls = nil, nil, false, false, false, 0
local function fake(url, accept, limit)
  calls = calls + 1
  assert(url:sub(1, #base) == base)
  if url == base .. "/$metadata" then
    assert(accept == "application/xml" and limit >= #xml)
    return xml, status_override or 200
  end
  assert(accept == "application/json" and limit == 1048576)
  local decoded = url:gsub("%%(%x%x)", function(h) return string.char(tonumber(h, 16)) end)
  local function payload(rows, n, link)
    return json.encode({value=json.array(rows),["odata.count"]=n,["odata.nextLink"]=link}), 200
  end
  if decoded:find("/Catalog_Номенклатура?",1,true) then
    assert(decoded:find("$orderby=Ref_Key",1,true) or decoded:find("skiptoken=",1,true))
    if decoded:find("$skip=0",1,true) then
      if emptytruncated then return payload({},3) end
      local link = item_link == "relative" and "?$skiptoken=remainder" or item_link
      return payload({
        {Ref_Key=id, Description='Bolt & <Nut> "8"', ["Артикул"]="00045", IsFolder=false, DeletionMark=false},
        {Ref_Key=cp:gsub("2$", "4"), Description="Folder", IsFolder=true, DeletionMark=false},
      },3,link)
    end
    return payload({{Ref_Key=repeated and id or cp:gsub("2$","5"), Description="Deleted", IsFolder=false, DeletionMark=true}},3)
  elseif decoded:find("/Catalog_Контрагенты?",1,true) then
    assert(decoded:find("ИНН,КПП",1,true))
    return json.encode({d={results={{Ref_Key=cp, Description="ООО Пример", ["ИНН"]="0000000001", ["КПП"]="000001001"}}, __count="1"}}), 200
  elseif decoded:find("/Catalog_Склады?",1,true) then
    return payload({{Ref_Key=wh, Description="Главный склад"}},1)
  else
    stockcalls = stockcalls + 1
    assert(decoded:find("/AccumulationRegister_StockFixture/Balance(Period=datetime'2026-11-01T00:00:00',Dimensions='Item,Warehouse')",1,true))
    return payload({{Item_Key=id, Warehouse_Key=wh, QuantityBalance=not missingquantity and "-0.125" or nil}},1)
  end
end
local function connect(options)
  return unf.connect(options or settings,fake,json)
end
local function fails(fn, expected)
  local ok, err = pcall(fn)
  assert(not ok and tostring(err):find(expected, 1, true), tostring(err))
end

assert(unf.endpoint(settings.endpoint) == base)
assert(unf.endpoint(base .. "/") == base)
for _, url in ipairs({"http://example.invalid/base", "https://test@example.invalid/base",
    "https://example.invalid/base?token=bad", "https://example.invalid/a/../base",
    "https://example.invalid/base%2fmore", base .. "/Catalog_NotBase"}) do
  fails(function() unf.endpoint(url) end, url:find("NotBase",1,true) and "use the OData" or "HTTPS")
end
local client = connect()
local records = client:catalogs()
assert(calls == 5 and #records == 3)
local stage = unf.stage(records)
assert(stage:find('source="unf-odata"',1,true))
assert(stage:find('Bolt &amp; &lt;Nut&gt; &quot;8&quot;',1,true))
assert(stage:find('<v n="Артикул">00045</v>',1,true))
assert(stage:find('<v n="ИНН">0000000001</v>',1,true))
assert(stage:find("Главный склад",1,true) and not stage:find("Deleted",1,true))
assert(not stage:find("<documents",1,true) and not stage:find("https://",1,true))
local stock = client:stock()
assert(stockcalls == 1 and stock.rows[1].quantity == "-0.125" and stock.rows[1].unit == "pcs")
assert(stock.rows[1].warehouse == wh and stock.rows[1].item == id and stock.as_of == settings.stock.period)
item_link = "relative"
assert(#client:catalogs() == 3)
item_link = "https://elsewhere.invalid/tenant/odata/standard.odata/" .. "Catalog_Номенклатура?$skiptoken=other"
local previous = calls
fails(function() client:catalogs() end,"changed origin")
assert(calls == previous + 1)
item_link = base .. "/Catalog_Контрагенты?$skiptoken=other"
fails(function() client:catalogs() end, "changed resource")
item_link = nil
repeated = true
fails(function() client:catalogs() end, "duplicate catalog GUID")
repeated = false
emptytruncated = true
fails(function() client:catalogs() end, "ended before its count")
emptytruncated = false
local cap_calls = 0
local function capped(url, accept, limit)
  if url:find("$metadata",1,true) then return xml,200 end
  local decoded = url:gsub("%%(%x%x)", function(h) return string.char(tonumber(h, 16)) end)
  if not decoded:find("/Catalog_Номенклатура?",1,true) then return fake(url,accept,limit) end
  cap_calls = cap_calls + 1
  local offset = assert(tonumber(url:match("%$skip=(%d+)")))
  local rows = json.array()
  if offset < 2 then rows[1] = {Ref_Key=offset == 0 and id or "aa000000-0000-0000-0000-000000000006",
    Description="Same description", IsFolder=false, DeletionMark=false} end
  return json.encode({value=rows}),200
end
local capped_records = unf.connect(settings,capped,json):catalogs()
assert(cap_calls == 3 and #capped_records == 4 and capped_records[1].description == capped_records[2].description,
  "short server pages and duplicate names must not silently lose records")
local bounded = connect({endpoint=base,page_size=2,max_pages=1})
fails(function() bounded:catalogs() end, "page limit reached")
status_override = 401
fails(connect, "1C HTTP 401")
status_override = nil
missingquantity = true
fails(function() client:stock() end, "invalid 1C stock quantity")
fails(function() unf.metadata('<!DOCTYPE unsafe>' .. xml) end, "unsupported declaration")
local ambiguous = xml:gsub("</EntityContainer>", '<EntitySet Name="Catalog_СтруктурныеЕдиницы" EntityType="StandardODATA.Catalog_Склады"/></EntityContainer>')
local function metadata_variant(url, accept, limit)
  if url:find("$metadata",1,true) then return ambiguous,200 end
  return fake(url,accept,limit)
end
fails(function() unf.connect(settings,metadata_variant,json):catalogs() end, "more than one catalog")
settings.catalogs = {warehouses="Catalog_Склады"}
assert(#unf.connect(settings,metadata_variant,json):catalogs() == 3)
print("UNF OData: metadata, bounded paging, safe continuations, XML, stock dimensions and errors passed")
