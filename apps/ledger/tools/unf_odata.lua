-- Run from the checkout with Lua 5.4 (see docs/unf-odata.md).
-- There is no transport or network activity in Ledger's posting runtime.
local repo = assert(arg[0]:match("^(.*)apps/ledger/tools/unf_odata%.lua$"),
  "run apps/ledger/tools/unf_odata.lua from the checkout")
local json = dofile(repo .. "share/lua/orion/json.lua")
local unf = dofile(repo .. "apps/ledger/share/stdplugs/one_c/unf_odata.lua")

local function shell(value)
  assert(not value:find("[%z\1-\31\127]"), "invalid curl argument")
  return "'" .. value:gsub("'", "'\\''") .. "'"
end

local function read_file(path, max)
  local file = assert(io.open(path, "rb"), "cannot open settings file")
  local data = file:read(max + 1)
  file:close()
  assert(data and #data <= max, "invalid settings file size")
  return data
end

local function main()
  local action, path = arg[1], arg[2]
  assert(#arg == 2 and (action == "discover" or action == "catalogs" or action == "stock"),
    "usage: lua apps/ledger/tools/unf_odata.lua discover|catalogs|stock SETTINGS.json")
  assert(package.config:sub(1, 1) == "/", "the bundled curl transport requires a POSIX shell")
  local settings = json.decode(read_file(path, 32768))
  assert(type(settings) == "table", "expected a JSON settings object")
  for key in pairs(settings) do
    assert(({endpoint=true, catalogs=true, stock=true, page_size=true, max_pages=true})[key],
      "unsupported settings field; keep credentials outside JSON")
  end
  local netrc = os.getenv("ORION_UNF_NETRC")
  assert(netrc and #netrc > 0, "set ORION_UNF_NETRC to a private, host-specific netrc file")
  local function get(url, accept, limit)
    local command = "curl -q --silent --globoff --proto '=https' --request GET --connect-timeout 10"
      .. " --max-time 30 --max-filesize " .. limit .. " --netrc-file " .. shell(netrc)
      .. " --header " .. shell("Accept: " .. accept)
      .. " --write-out '\\n%{http_code}' -- " .. shell(url)
    local pipe = io.popen(command, "r")
    if not pipe then return nil end
    local response = pipe:read(limit + 5)
    local success = pipe:close()
    if not response then return nil end
    local body, status = response:match("^(.*)\n(%d%d%d)$")
    if not success and (not status or status == "200" or status == "000") then return nil end
    return body, tonumber(status)
  end

  local client = unf.connect(settings, get, json)
  local output
  if action == "discover" then
    local resources = json.array()
    for name, set in pairs(client.sets) do
      if name:match("^Catalog_") or name:match("^AccumulationRegister_") then
        local fields = json.array()
        for field in pairs(set.fields) do fields[#fields+1] = field end
        table.sort(fields)
        resources[#resources+1] = {name=name, fields=fields}
      end
    end
    table.sort(resources, function(a, b) return a.name < b.name end)
    output = json.encode({resources=resources}) .. "\n"
  elseif action == "catalogs" then output = unf.stage(client:catalogs())
  else output = json.encode(client:stock()) .. "\n" end
  assert(io.write(output), "cannot write output")
end

local ok, err = pcall(main)
if not ok then
  io.stderr:write("1C UNF: ", tostring(err):gsub("^.-:%d+: ", ""), "\n")
  os.exit(1)
end
