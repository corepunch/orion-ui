-- Local CSV is intentionally not accepted: JSON preserves SKU leading zeros.
local repo = assert(arg[0]:match("^(.*)apps/ledger/tools/label_sheet%.lua$"),
  "invoke apps/ledger/tools/label_sheet.lua from the checkout")
local labels = dofile(repo .. "apps/ledger/share/addons/labels/label_sheet.lua")
local json = dofile(repo .. "share/lua/orion/json.lua")
local function main()
  assert(#arg == 1, "usage: lua5.4 apps/ledger/tools/label_sheet.lua LABELS.json > labels.html")
  local file = assert(io.open(arg[1], "rb"), "cannot open label input")
  local payload = file:read(1048577)
  file:close()
  local job = json.decode(payload)
  assert(type(job) == "table" and type(job.rows) == "table", "expected a label job object with rows")
  local output = labels.render(job.rows, job.options)
  assert(io.write(output), "cannot write printable labels")
end
local ok, err = pcall(main)
if not ok then
  io.stderr:write(tostring(err):gsub("^.-:%d+: ", ""), "\n")
  os.exit(1)
end
