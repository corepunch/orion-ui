package.path = "share/lua/?.lua;" .. package.path
local registry = dofile("apps/ledger/share/business/registry.lua")
local json = require("orion.json")
local function reject(work, expected)
  local ok, err = pcall(work)
  assert(not ok and tostring(err):find(expected, 1, true), tostring(err))
end
local function manifest(id, deps, owns, commands)
  return {id=id, api=1, dependencies=deps or {}, owns=owns or {}, commands=commands or {}}
end
local function profile(...)
  return {id="ledger.example", api=1, modules={...}}
end
local function command(id, mode, ...)
  return {id=id, mode=mode, roles={...}, run=function(args) return args.value end}
end
local catalogue = manifest("ledger.catalogues", {}, {"ib.things"}, {
  command("catalogues.read", "read", "auditor", "admin"),
})
local feature = manifest("ledger.feature", {"ledger.catalogues"}, {"ib.jobs"}, {
  command("feature.run", "write", "accountant", "admin"),
})
local dormant = manifest("ledger.dormant", {}, {}, {
  command("dormant.call", "read", "admin"),
})
local runtime = registry.assemble(profile("ledger.feature", "ledger.catalogues"), {dormant, feature, catalogue})
local audit = runtime.describe("auditor")
assert(audit.api == 1 and audit.profile == "ledger.example")
assert(#audit.modules == 2 and audit.modules[1].id == "ledger.catalogues" and audit.modules[2].id == "ledger.feature")
assert(#audit.commands == 1 and audit.commands[1].id == "catalogues.read")
assert(runtime.invoke("admin", "feature.run", {value=42}) == 42)
audit.modules[1].resources[1] = "ib.modified"
catalogue.commands[1].roles[1] = "invalid"
assert(runtime.describe("auditor").modules[1].resources[1] == "ib.things")
assert(runtime.invoke("auditor", "catalogues.read", {value=11}) == 11)
reject(function() runtime.invoke("auditor", "feature.run", {}) end, "role cannot invoke")
reject(function() runtime.invoke("admin", "dormant.call", {}) end, "unsupported command")
reject(function() runtime.invoke("admin", "catalogues.read", json.array({})) end, "args must be an object")
reject(function() registry.assemble(profile("ledger.feature"), {feature}) end, "missing business module")
reject(function() registry.assemble(profile("ledger.feature"), {feature, feature}) end, "duplicate business module")
reject(function() registry.assemble(profile("ledger.feature"), {
  feature, manifest("ledger.catalogues", {"ledger.feature"}),
}) end, "cyclic business module")
reject(function() registry.assemble(profile("ledger.feature"), {
  feature, manifest("ledger.catalogues", {}, {"ib.jobs"}),
}) end, "duplicate resource owner")
reject(function() registry.assemble(profile("ledger.feature"), {
  manifest("ledger.feature", {}, {}, {command("feature.run", "write", "auditor")}),
}) end, "auditors cannot write")
reject(function() registry.assemble(profile("ledger.feature"), {
  manifest("ledger.feature", {}, {}, {command("documents.post", "write", "admin")}),
}) end, "invalid business command")
reject(function() registry.assemble(profile("ledger.feature"), {
  manifest("ledger.feature", {}, {}, {command("feature.run", "write", "admin"), command("feature.run", "read", "admin")}),
}) end, "duplicate business command")
reject(function() registry.assemble(profile("ledger.feature"), {
  {id="ledger.feature", api=2, dependencies={}, owns={}, commands={}},
}) end, "unsupported business module")

-- Exercise the exact JSON entry point without granting the fixture native database access.
local actor, role, writes, rows = nil, nil, 0, {}
ledger = {
  fetch=function(scope, key) return assert(rows[scope .. "." .. key], key) end,
  transaction=function(work) return work() end,
  insert=function(key, fields)
    assert(key == "items" and fields.sku == "FIXTURE")
    writes = writes + 1
    return {id=31}
  end,
  stock_balance=function(item, warehouse)
    assert(item == 31 and warehouse == 0)
    return {on_hand_milli=9007199254740993, reserved_milli=1000, available_milli=9007199254739993}
  end,
  close_through=function(date) assert(date == "2026-10-31"); writes = writes + 1 end,
}
rawset(_G, "ledger_service_native", {
  access=function(a, r) actor, role = a, r end,
  close_preview=function() return {preview=true} end,
  create=function()
    writes = writes + 1
    return 77
  end,
})
local tmp = os.tmpname()
local function service(r, cmd, args, expected)
  local file = assert(io.open(tmp, "wb"))
  file:write(json.encode({actor="fixture", role=r, command=cmd, args=args or {}, profile="ledger.elevated"}))
  file:close()
  local body, ok = assert(loadfile("apps/ledger/share/service.lua"))(tmp)
  local res = json.decode(body)
  assert(res.ok == ok and ok == expected, body)
  return ok and res.result or res.error
end
local ok, err = pcall(function()
  local audit_contract = service("auditor", "capabilities.list", {}, true)
  assert(actor == "fixture" and role == "auditor" and audit_contract.profile == "ledger.accounting")
  local admin_contract = service("admin", "capabilities.list", {}, true)
  assert(#admin_contract.commands > #audit_contract.commands and #admin_contract.modules == 7)
  for _, c in ipairs(audit_contract.commands) do assert(c.mode == "read" and c.id ~= "period.preview_close") end
  assert(service("auditor", "documents.create", {}, false):find("role cannot invoke", 1, true))
  assert(service("accountant", "period.close", {date="2026-10-31"}, false):find("role cannot invoke", 1, true))
  assert(service("auditor", "period.preview_close", {date="2026-10-31"}, false):find("role cannot invoke", 1, true))
  assert(service("auditor", "stock.balance", {item_id=31, warehouse_id=0}, true).on_hand_milli == "9007199254740993")
  assert(service("auditor", "stock.balance", {item_id=31, warehouse_id=-1}, false):find("invalid stock record id", 1, true))
  assert(writes == 0)
  assert(service("admin", "period.preview_close", {date="2026-10-31"}, true).preview and writes == 0)
  assert(service("accountant", "items.save", {fields={name="Fixture", sku="FIXTURE"}}, true).id == 31)
  assert(service("accountant", "documents.create", {}, true).document_id == 77 and writes == 2)
  assert(service("admin", "period.close", {date="2026-10-31"}, true).closed_through == "2026-10-31" and writes == 3)
  rows["ib.documents"] = {{id=1, posted=false}, {id=2, posted=true}}
  rows["ib.entries"] = {{id=1, document_id=1}, {id=2, document_id=2, amount_minor=9007199254740993}}
  local lines = service("auditor", "journal.list", {}, true)
  assert(#lines == 1 and lines[1].id == 2 and lines[1].amount_minor == "9007199254740993")
end)
os.remove(tmp)
assert(ok, err)
print("Ledger business profile and service tests passed")
