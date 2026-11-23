-- Read-only 1C OData v3 adapter. The caller supplies GET and JSON; no Ledger
-- transactions or credentials are owned by this module.
local M = {}
local SERVICE = "/odata/standard.odata"
local ZERO = "00000000-0000-0000-0000-000000000000"
local ID = "^%x%x%x%x%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%x%x%x%x%x%x%x%x$"
local catalogs = {
  {key="items", candidates={"Catalog_Номенклатура"}, stage="Справочник.Номенклатура", name_limit=160,
    fields={"Артикул"}},
  {key="counterparties", candidates={"Catalog_Контрагенты"}, stage="Справочник.Контрагенты", name_limit=160,
    fields={"ИНН", "КПП"}},
  {key="warehouses", candidates={"Catalog_Склады", "Catalog_СтруктурныеЕдиницы"},
    stage="Справочник.Склады", name_limit=128, fields={}},
}

local function quoted(value)
  return (value:gsub("[^a-zA-Z0-9._~-]", function(c) return ("%%%02X"):format(c:byte()) end))
end

local function safe_path(path)
  return not path:find("[^a-zA-Z0-9_./~-]") and not path:find("//", 1, true)
    and not path:find("/%.%./") and not path:find("/%./")
    and not path:match("/%.%.?$")
end

local function authority(value)
  local host, port = value:match("^([A-Za-z0-9.-]+):?(%d*)$")
  assert(host and host:match("^[A-Za-z0-9]") and host:match("[A-Za-z0-9]$")
    and not host:find("..", 1, true) and not value:match(":$"), "invalid HTTPS authority")
  assert(port == "" or (tonumber(port) >= 1 and tonumber(port) <= 65535), "invalid HTTPS port")
  return host:lower() .. ((port == "" or port == "443") and "" or ":" .. tostring(tonumber(port)))
end

function M.endpoint(value)
  assert(type(value) == "string" and #value <= 1024, "expected an HTTPS application or OData URL")
  local host, path = value:match("^https://([^/]+)(/.*)$")
  assert(host and safe_path(path), "expected an HTTPS application or OData URL without credentials, query or fragment")
  local origin = "https://" .. authority(host)
  path = path:gsub("/$", "")
  assert(path ~= "", "use the application URL, not the server homepage")
  if path:sub(-#SERVICE) ~= SERVICE then
    assert(not path:find(SERVICE, 1, true), "use the OData service URL, not an entity URL")
    path = path .. SERVICE
  end
  return origin .. path, origin, path
end

local function identifier(value)
  if type(value) ~= "string" or #value == 0 or #value > 200 or not utf8.len(value) then return false end
  for _, cp in utf8.codes(value) do
    if not (cp == 95 or (cp >= 48 and cp <= 57) or (cp >= 65 and cp <= 90)
      or (cp >= 97 and cp <= 122) or (cp >= 0x400 and cp <= 0x4ff)) then return false end
  end
  return true
end

-- Read entity/field declarations only, without expanding entities or resolving
-- schemas. 1C's standard metadata uses literal UTF-8 identifier attributes.
function M.metadata(xml)
  assert(type(xml) == "string" and #xml <= 16 * 1024 * 1024, "invalid OData metadata size")
  xml = xml:gsub("<!%-%-.-%-%->", "")
  assert(not xml:find("<!", 1, true), "unsupported declaration in OData metadata")
  local types, sets, current = {}, {}, nil
  local metadata = false
  for close, tag, attrs in xml:gmatch("<(/?)([%w_.:-]+)([^<>]*)>") do
    tag = tag:match("[^:]+$")
    if tag == "Edmx" then metadata = true end
    if close == "/" and tag == "EntityType" then current = nil end
    if close == "" and (tag == "EntityType" or tag == "EntitySet" or tag == "Property") then
      local fields = {}
      for key, _, value in attrs:gmatch("([%w_:.-]+)%s*=%s*(['\"])(.-)%2") do fields[key] = value end
      local name = fields.Name
      assert(identifier(name), "invalid OData metadata identifier")
      if tag == "EntityType" then
        assert(not types[name], "duplicate OData entity type")
        current = {}
        types[name] = current
        if attrs:match("/%s*$") then current = nil end
      elseif tag == "Property" and current then
        current[name] = true
      elseif tag == "EntitySet" then
        assert(not sets[name], "duplicate OData entity set")
        sets[name] = {type=assert(fields.EntityType, "missing OData entity type"):match("[^.]+$")}
      end
    end
  end
  assert(metadata and next(sets), "not an OData metadata document with entity sets")
  for _, set in pairs(sets) do set.fields = types[set.type] or {} end
  return sets
end

local function continuation(link, origin, path, resource)
  assert(type(link) == "string" and #link <= 8192 and not link:find("[%z\1-\32\127#\\]"),
    "invalid OData continuation")
  local target
  if link:sub(1, 8) == "https://" then
    local host, suffix = link:match("^https://([^/]+)(/.*)$")
    assert(host and "https://" .. authority(host) == origin, "OData continuation changed origin")
    target = suffix
  elseif link:sub(1, 1) == "/" then target = link
  elseif link:sub(1, 1) == "?" then target = path .. "/" .. resource .. link
  else target = path .. "/" .. link end
  local urlpath, query = target:match("^([^?]+)(%?.+)$")
  local wanted = path .. "/" .. resource
  local function decode(s) return (s:gsub("%%(%x%x)", function(h) return string.char(tonumber(h, 16)) end)) end
  assert(urlpath and decode(urlpath) == decode(wanted), "OData continuation changed resource")
  return origin .. wanted .. query
end

local function http(get, url, accept, limit)
  local body, status = get(url, accept, limit)
  assert(status == 200, type(status) == "number" and ("1C HTTP " .. tostring(status)) or "1C HTTPS request failed")
  assert(type(body) == "string" and #body > 0 and #body <= limit, "invalid OData response size")
  return body
end

function M.connect(settings, get, json)
  local base, origin, path = M.endpoint(settings.endpoint)
  local page_size = settings.page_size or 250
  local max_pages = settings.max_pages or 200
  assert(math.type(page_size) == "integer" and page_size >= 1 and page_size <= 500, "page_size must be 1–500")
  assert(math.type(max_pages) == "integer" and max_pages >= 1 and max_pages <= 1000, "max_pages must be 1–1000")
  local sets = M.metadata(http(get, base .. "/$metadata", "application/xml", 16 * 1024 * 1024))
  local client = {sets=sets}

  function client:rows(resource, options, each)
    local skip, seen, total, count, url = 0, {}, nil, 0, nil
    local function page_url()
      local query = "$format=json&$top=" .. page_size .. "&$skip=" .. skip .. options
      return base .. "/" .. resource .. "?" .. query
    end
    for _ = 1, max_pages do
      url = url or page_url()
      assert(not seen[url], "repeated OData page")
      seen[url] = true
      local result = json.decode(http(get, url, "application/json", 1048576))
      local object = type(result) == "table" and (result.d or result)
      local rows = type(object) == "table" and (object.value or object.results)
      assert(type(rows) == "table" and getmetatable(rows) == getmetatable(json.array()), "invalid OData collection")
      assert(#rows <= page_size, "1C ignored the requested page size")
      local reported = object["odata.count"] or object.__count or result["odata.count"]
      if reported ~= nil then
        reported = math.tointeger(tonumber(reported))
        assert(reported and reported >= 0 and (not total or total == reported), "unstable OData collection count")
        total = reported
      end
      for _, row in ipairs(rows) do
        assert(type(row) == "table" and row ~= json.null, "invalid OData row")
        each(row)
      end
      count = count + #rows
      assert(count <= 50000, "OData row limit reached; nothing was exported")
      assert(not total or count <= total, "OData collection exceeded its count")
      local link = object["odata.nextLink"] or object.__next or result["odata.nextLink"]
      if link then
        assert(#rows > 0 and (not total or count < total), "inconsistent OData continuation")
        url = continuation(link, origin, path, resource)
      else
        -- A server can impose a smaller page cap than requested. In the
        -- absence of a reported count, only an empty page proves EOF.
        if total and count == total or not total and #rows == 0 then return end
        assert(#rows > 0, "OData collection ended before its count")
        assert(not seen.continuation, "OData continuation ended before its count")
        skip, url = count, nil
      end
      if link then seen.continuation = true end
    end
    error("OData page limit reached; nothing was exported", 0)
  end

  local function choose(spec)
    local override = (settings.catalogs or {})[spec.key]
    if override then
      assert(identifier(override) and override:match("^Catalog_") and sets[override],
        "configured catalog is not exposed: " .. spec.key)
      return override
    end
    local selected
    for _, name in ipairs(spec.candidates) do
      if sets[name] then
        assert(not selected, "more than one catalog candidate; configure: " .. spec.key)
        selected = name
      end
    end
    assert(selected, "publish or configure the 1C catalog: " .. spec.key)
    assert(selected ~= "Catalog_СтруктурныеЕдиницы", "confirm the warehouse catalog mapping explicitly")
    return selected
  end

  function client:catalogs()
    local records, seen = {}, {}
    for _, spec in ipairs(catalogs) do
      local name = choose(spec)
      local fields = sets[name].fields
      assert(fields.Ref_Key and fields.Description, "missing catalog Ref_Key or Description: " .. spec.key)
      local selected = {"Ref_Key", "Description"}
      for _, field in ipairs({"IsFolder", "DeletionMark", table.unpack(spec.fields)}) do
        if fields[field] then selected[#selected+1] = field end
      end
      self:rows(quoted(name), "&$orderby=Ref_Key&$select=" .. quoted(table.concat(selected, ",")), function(row)
        local guid = M.guid(row.Ref_Key)
        assert(not seen[guid], "duplicate catalog GUID; aborting export")
        seen[guid] = true
        assert(not fields.IsFolder or type(row.IsFolder) == "boolean", "invalid IsFolder in 1C catalog")
        assert(not fields.DeletionMark or type(row.DeletionMark) == "boolean", "invalid DeletionMark in 1C catalog")
        if not row.IsFolder and not row.DeletionMark then
          local rec = {id=guid, type=spec.stage, description=row.Description, values={}}
          assert(type(rec.description) == "string" and rec.description:match("%S"), "empty or invalid 1C catalog description")
          assert(#rec.description < spec.name_limit, "catalog name exceeds Ledger storage size: " .. guid)
          for _, field in ipairs(spec.fields) do
            local val = row[field]
            if val ~= nil and val ~= json.null then
              assert(type(val) == "string", "unexpected non-text catalog field")
              assert(#val < 16, "catalog field exceeds Ledger storage size: " .. field .. " (" .. guid .. ")")
              rec.values[field] = val
            end
          end
          records[#records+1] = rec
        end
      end)
    end
    return records
  end

  function client:stock()
    local config = settings.stock
    assert(type(config) == "table" and identifier(config.register)
      and config.register:match("^AccumulationRegister_") and sets[config.register], "configure an exposed accumulation register")
    assert(identifier(config.item_field) and identifier(config.warehouse_field) and identifier(config.quantity_field),
      "configure stock item, warehouse and quantity fields")
    assert((identifier(config.unit_field) and not config.unit)
      or (not config.unit_field and type(config.unit) == "string" and config.unit:match("%S")),
      "configure a stock unit field or an explicit common unit")
    local period = config.period
    assert(type(period) == "string" and period:match("^%d%d%d%d%-%d%d%-%d%dT%d%d:%d%d:%d%d$"),
      "configure a stock timestamp: YYYY-MM-DDTHH:MM:SS")
    local dimensions = config.dimensions
    assert(type(dimensions) == "table" and #dimensions >= 2 and #dimensions <= 12, "configure stock dimensions")
    local defined = {}
    for _, dim in ipairs(dimensions) do
      assert(identifier(dim) and not defined[dim], "invalid or duplicate stock dimension")
      defined[dim] = true
    end
    assert(defined[config.item_field:gsub("_Key$", "")] and defined[config.warehouse_field:gsub("_Key$", "")],
      "stock dimensions must include item and warehouse")
    if config.unit_field then
      assert(defined[config.unit_field:gsub("_Key$", "")], "stock dimensions must include the requested unit field")
    end
    local resource = quoted(config.register) .. "/Balance(Period=" .. quoted("datetime'" .. period .. "'")
      .. ",Dimensions=" .. quoted("'" .. table.concat(dimensions, ",") .. "'") .. ")"
    local rows, seen_stock = json.array(), {}
    local order = {}
    for _, dim in ipairs(dimensions) do
      if dim == config.item_field:gsub("_Key$", "") then order[#order+1] = config.item_field
      elseif dim == config.warehouse_field:gsub("_Key$", "") then order[#order+1] = config.warehouse_field
      elseif config.unit_field and dim == config.unit_field:gsub("_Key$", "") then order[#order+1] = config.unit_field
      else order[#order+1] = dim end
    end
    self:rows(resource, "&$orderby=" .. quoted(table.concat(order, ",")) .. "", function(row)
      local quantity = row[config.quantity_field]
      if type(quantity) == "number" then quantity = tostring(quantity) end
      assert(type(quantity) == "string" and (quantity:match("^[+-]?%d+$") or quantity:match("^[+-]?%d+%.%d+$")
        or quantity:match("^[+-]?%d+%.?%d*[eE][+-]?%d+$")), "invalid 1C stock quantity")
      local unit = config.unit or row[config.unit_field]
      assert(type(unit) == "string" and unit:match("%S") and unit ~= ZERO, "invalid 1C stock unit")
      local values, key = {}, {}
      for _, field in ipairs(order) do
        local value = row[field] == nil and row[field .. "_Key"] or row[field]
        assert(type(value) == "string" or type(value) == "number" or type(value) == "boolean",
          "missing 1C stock dimension")
        values[field], key[#key+1] = value, value
      end
      local identity = json.encode(key)
      assert(not seen_stock[identity], "duplicate stock dimensions; aborting export")
      seen_stock[identity] = true
      rows[#rows+1] = {item=M.guid(row[config.item_field]), warehouse=M.guid(row[config.warehouse_field]),
        quantity=quantity, unit=unit, dimensions=values}
    end)
    return {register=config.register, as_of=period, rows=rows}
  end
  return client
end

function M.guid(value)
  assert(type(value) == "string" and value:match(ID) and value ~= ZERO, "invalid 1C reference key")
  return value:lower()
end

local function xml(value)
  assert(type(value) == "string" and utf8.len(value) and not value:find("[%z\1-\8\11\12\14-\31\127]"),
    "invalid XML text in catalog")
  for _, cp in utf8.codes(value) do
    assert(not (cp >= 0xd800 and cp <= 0xdfff) and cp ~= 0xfffe and cp ~= 0xffff, "invalid XML catalog character")
  end
  return (value:gsub("[&<>\"'\n\r\t]", {
    ["&"]="&amp;", ["<"]="&lt;", [">"]="&gt;", ['"']="&quot;", ["'"]="&apos;",
    ["\n"]="&#10;", ["\r"]="&#13;", ["\t"]="&#9;",
  }))
end

function M.stage(records)
  -- The existing transactional importer recognizes the format on line one.
  local out = {'<stage format="orion-1cd-stage-v1" source="unf-odata">', "  <records>"}
  for _, row in ipairs(records) do
    out[#out+1] = ('    <record type="%s" id="%s" description="%s">'):format(xml(row.type), M.guid(row.id), xml(row.description))
    local fields = {}
    for field in pairs(row.values) do fields[#fields+1] = field end
    table.sort(fields)
    for _, field in ipairs(fields) do
      out[#out+1] = ('      <v n="%s">%s</v>'):format(xml(field), xml(row.values[field]))
    end
    out[#out+1] = "    </record>"
  end
  out[#out+1], out[#out+2], out[#out+3] = "  </records>", "</stage>", ""
  return table.concat(out, "\n")
end
return M
