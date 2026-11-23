-- Curl is a local CLI transport, never loaded by Ledger's posting runtime.
local M = {}
local epaths = {["/sell/fulfillment/v1/order"]=true,["/sell/inventory/v1/inventory_item"]=true}
local opaths = {["/v4/posting/fbs/list"]=true,["/v3/posting/fbo/list"]=true,["/v4/product/info/stocks"]=true}
local LIMIT = 1048576 -- shared JSON decoder's maximum

local function shell(value)
  assert(type(value)=="string" and not value:find("[%z\1-\31\127]"),"unsafe curl argument")
  return "'" .. value:gsub("'","'\\''") .. "'"
end

local function private_file(path)
  assert(type(path)=="string" and path:match("^/") and #path <= 4096,"set an absolute marketplace header-file path")
  local arg = shell(path)
  local command = "test -f " .. arg .. " && test ! -L " .. arg
    .. " && test -r " .. arg .. " && (stat -c '%a %u' " .. arg
    .. " 2>/dev/null || stat -f '%Lp %u' " .. arg .. " 2>/dev/null) && id -u"
  local pipe = assert(io.popen(command,"r"),"cannot inspect marketplace header file")
  local answer = pipe:read(128)
  local ok = pipe:close()
  local mode, uid, me
  if answer then mode, uid, me = answer:match("^(%d+) (%d+)\n(%d+)\n$") end
  assert(ok and (mode=="600" or mode=="400") and uid == me,
    "marketplace header file must be owned by you, regular (not symlink), and mode 600 or 400")
  local f = assert(io.open(path,"rb"),"cannot open marketplace header file")
  local s = f:read(32769); f:close()
  assert(s and #s <= 32768 and not s:find("[%z\1-\9\11-\31\127]"),"invalid marketplace header file")
  return s
end

local function validate_headers(s, provider)
  local headers = {}
  if s:sub(-1) == "\n" then s = s:sub(1,-2) end
  for line in (s .. "\n"):gmatch("([^\n]*)\n") do
    local name, value = line:match("^([A-Za-z-]+): ([ -~]+)$")
    assert(name and not headers[name],"invalid marketplace header")
    if provider=="ebay" then
      assert(name=="Authorization" and value:match("^Bearer [!-~]+$"),"expected only an eBay Bearer authorization header")
    else
      assert((name=="Client-Id" and value:match("^%d+$"))
        or (name=="Api-Key" and value:match("^[!-~]+$")),"expected only Ozon Client-Id and Api-Key headers")
    end
    headers[name]=true
  end
  assert(headers.Authorization or (headers["Client-Id"] and headers["Api-Key"]),"marketplace headers are incomplete")
end

function M.allowed(provider, environment, r)
  if type(r) ~= "table" or type(r.url) ~= "string" or #r.url > 1024 then return false end
  local host, path, tail = r.url:match("^https://([A-Za-z0-9.-]+)(/[^?]*)(.*)$")
  if provider=="ebay" and (environment=="production" or environment=="sandbox") then
    local expected = environment=="production" and "api.ebay.com" or "api.sandbox.ebay.com"
    return host==expected and epaths[path] and r.method=="GET" and r.body==nil
      and (tail=="" or tail:match("^%?[A-Za-z0-9_.~%%=&-]+$") ~= nil)
  elseif provider=="ozon" and environment=="production" then
    return host=="api-seller.ozon.ru" and opaths[path] and tail=="" and r.method=="POST" and type(r.body)=="table"
  end
  return false
end

function M.new(provider, environment, path, json)
  assert(package.config:sub(1,1)=="/","the marketplace CLI requires a POSIX shell")
  local limit = LIMIT
  return function(r)
    assert(M.allowed(provider,environment,r),"marketplace transport refused method, host or path")
    validate_headers(private_file(path),provider)
    local command = "curl -q --silent --globoff --noproxy '*' --proto '=https' --connect-timeout 10 --max-time 35"
      .. " --max-filesize " .. limit .. " --request " .. r.method .. " --header " .. shell("@" .. path)
      .. " --header 'Accept: application/json'"
    if r.body then
      local body = json.encode(r.body)
      assert(#body <= 16384,"marketplace request is too large")
      command = command .. " --header 'Content-Type: application/json' --data-binary " .. shell(body)
    end
    command = command .. " --write-out '\\n%{http_code}' -- " .. shell(r.url)
    local pipe = assert(io.popen(command,"r"),"cannot start marketplace transport")
    local response = pipe:read(limit+5)
    local ok = pipe:close()
    assert(ok and response and #response <= limit+4,"marketplace network request failed or response is too large")
    local raw, status = response:match("^(.*)\n(%d%d%d)$")
    status = tonumber(status)
    assert(status,"invalid marketplace HTTP response")
    if status < 200 or status >= 300 then
      local hint = ({
        [401]="; check credentials or token expiry",[403]="; check read permissions",
        [429]="; retry later", [301]="; redirect refused",[302]="; redirect refused",
        [303]="; redirect refused",[307]="; redirect refused",[308]="; redirect refused"
      })[status] or ""
      error("marketplace HTTP " .. status .. hint,0)
    end
    local decoded, data = pcall(json.decode,raw)
    assert(decoded and type(data)=="table" and getmetatable(data)==nil,"invalid marketplace JSON response")
    return data
  end
end
return M
