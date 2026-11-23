-- Print-ready HTML from a read-only Ledger catalog response.
local repo = assert(arg[0]:match("^(.*)apps/ledger/tools/product_card%.lua$"),
  "run apps/ledger/tools/product_card.lua from the checkout")
local json = dofile(repo .. "share/lua/orion/json.lua")
local card = dofile(repo .. "apps/ledger/share/addons/product_card/product_card.lua")
local function read(path, limit, label)
  assert(path ~= "", "missing " .. label)
  local f = assert(io.open(path, "rb"), "cannot read " .. label)
  local data = f:read(limit + 1)
  f:close()
  assert(data and #data <= limit, label .. " is empty or exceeds size limit")
  return data
end

local function main()
  assert(#arg >= 2 and #arg <= 4,
    "usage: lua apps/ledger/tools/product_card.lua CATALOG.json ITEM_ID [DETAILS.json|-] [PHOTO.png|PHOTO.jpg]")
  local id = arg[2]:match("^%d+$") and math.tointeger(tonumber(arg[2]))
  local item = card.select_item(json.decode(read(arg[1], 1048576, "catalog")), id)
  local details = arg[3] and arg[3] ~= "-" and json.decode(read(arg[3], 32768, "details")) or nil
  if arg[3] and arg[3] ~= "-" then assert(type(details) == "table", "expected a details object") end
  local photo = arg[4] and read(arg[4], card.MAX_PHOTO, "photo") or nil
  assert(io.write(card.render(item, details, photo)), "cannot write product card")
end

local ok, err = pcall(main)
if not ok then
  io.stderr:write("Product card: ", tostring(err):gsub("^.-:%d+: ", ""), "\n")
  os.exit(1)
end
