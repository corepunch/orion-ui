-- Strict data-only interchange for business service hosts. Never evaluates code.
local Json = {}
Json.null = setmetatable({}, {__tostring = function() return "null" end})
local array_mt = {}
function Json.array(value) return setmetatable(value or {}, array_mt) end
function Json.decode(source)
  assert(type(source) == "string" and #source <= 1048576, "invalid JSON input size")
  local pos, length = 1, #source
  local function skip() local _, last = source:find("^[ \t\r\n]*", pos); pos = (last or pos - 1) + 1 end
  local function fail() error("invalid JSON at byte " .. pos, 0) end
  local function string_value()
    if source:sub(pos, pos) ~= '"' then fail() end
    pos = pos + 1
    local parts = {}
    while pos <= length do
      local c = source:sub(pos, pos); pos = pos + 1
      if c == '"' then
        local value = table.concat(parts)
        if not utf8.len(value) then fail() end
        return value
      end
      if c == '\\' then
        c = source:sub(pos, pos); pos = pos + 1
        local escapes = {['"'] = '"', ['\\'] = '\\', ['/'] = '/', b = '\b', f = '\f', n = '\n', r = '\r', t = '\t'}
        if escapes[c] then parts[#parts + 1] = escapes[c]
        elseif c == 'u' then
          local hex = source:sub(pos, pos + 3)
          if #hex ~= 4 or not hex:match('^%x+$') then fail() end
          local cp = tonumber(hex, 16); pos = pos + 4
          if cp >= 0xd800 and cp <= 0xdbff then
            if source:sub(pos, pos + 1) ~= '\\u' then fail() end
            local low = source:sub(pos + 2, pos + 5)
            if #low ~= 4 or not low:match('^%x+$') then fail() end
            low = tonumber(low, 16)
            if low < 0xdc00 or low > 0xdfff then fail() end
            cp = 0x10000 + (cp - 0xd800) * 0x400 + low - 0xdc00; pos = pos + 6
          elseif cp >= 0xdc00 and cp <= 0xdfff then fail() end
          parts[#parts + 1] = utf8.char(cp)
        else fail() end
      elseif c:byte() < 32 then fail()
      else parts[#parts + 1] = c end
    end
    fail()
  end
  local value
  value = function(depth)
    if depth > 64 then fail() end
    skip()
    local c = source:sub(pos, pos)
    if c == '"' then return string_value() end
    if c == '{' or c == '[' then
      pos = pos + 1; skip()
      local object = c == '{'
      local result = object and {} or Json.array()
      local ending = object and '}' or ']'
      if source:sub(pos, pos) == ending then pos = pos + 1; return result end
      while true do
        local key = #result + 1
        if object then
          skip(); key = string_value(); skip()
          if source:sub(pos, pos) ~= ':' or result[key] ~= nil then fail() end
          pos = pos + 1
        end
        result[key] = value(depth + 1); skip()
        c = source:sub(pos, pos); pos = pos + 1
        if c == ending then return result end
        if c ~= ',' then fail() end
      end
    end
    for literal, result in pairs({['true'] = true, ['false'] = false, ['null'] = Json.null}) do
      if source:sub(pos, pos + #literal - 1) == literal then pos = pos + #literal; return result end
    end
    local start = pos
    if c == '-' then pos = pos + 1 end
    c = source:sub(pos, pos)
    if c == '0' then pos = pos + 1
    elseif c:match('[1-9]') then repeat pos = pos + 1 until not source:sub(pos, pos):match('%d')
    else fail() end
    if source:sub(pos, pos) == '.' then
      pos = pos + 1
      if not source:sub(pos, pos):match('%d') then fail() end
      repeat pos = pos + 1 until not source:sub(pos, pos):match('%d')
    end
    if source:sub(pos, pos):match('[eE]') then
      pos = pos + 1
      if source:sub(pos, pos):match('[+-]') then pos = pos + 1 end
      if not source:sub(pos, pos):match('%d') then fail() end
      repeat pos = pos + 1 until not source:sub(pos, pos):match('%d')
    end
    local token = source:sub(start, pos - 1)
    local number = tonumber(token)
    if not number or number ~= number or math.abs(number) == math.huge then fail() end
    if not token:find('[.eE]') and math.type(number) ~= 'integer' then fail() end
    return number
  end
  local result = value(0); skip()
  if pos <= length then fail() end
  return result
end
function Json.encode(input)
  local seen = {}
  local function quote(value)
    assert(utf8.len(value), "invalid UTF-8")
    return '"' .. value:gsub('[%z\1-\31\\"]', function(c)
      local escapes = {['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t'}
      return escapes[c] or string.format('\\u%04x', c:byte())
    end) .. '"'
  end
  local encode
  encode = function(value, depth)
    assert(depth <= 64, "JSON nesting limit")
    if value == Json.null then return 'null' end
    local kind = type(value)
    if kind == 'string' then return quote(value) end
    if kind == 'boolean' then return tostring(value) end
    if kind == 'number' then
      assert(value == value and math.abs(value) ~= math.huge, "invalid JSON number")
      return math.type(value) == 'integer' and tostring(value) or string.format('%.17g', value)
    end
    assert(kind == 'table' and not seen[value], "invalid or cyclic JSON value")
    seen[value] = true
    local array = getmetatable(value) == array_mt or #value > 0
    local keys, parts = {}, {}
    for key in pairs(value) do
      if array then assert(type(key) == 'number' and key % 1 == 0 and key >= 1 and key <= #value, "invalid JSON array")
      else assert(type(key) == 'string', "invalid JSON object key") end
      keys[#keys + 1] = key
    end
    table.sort(keys)
    if array then assert(#keys == #value, "sparse JSON array") end
    for _, key in ipairs(keys) do parts[#parts + 1] = (array and '' or quote(key) .. ':') .. encode(value[key], depth + 1) end
    seen[value] = nil
    return (array and '[' or '{') .. table.concat(parts, ',') .. (array and ']' or '}')
  end
  return encode(input, 0)
end
return Json
