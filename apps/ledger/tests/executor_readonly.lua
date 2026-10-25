local doc = ledger.fetch("ib","documents")[1]
local ok, err = pcall(ledger.executor.execute,"documents.number",doc.id)
assert(not ok and err:find("writable script",1,true))
ok, err = pcall(ledger.executor.register,"documents.number",function() return {} end)
assert(not ok and err:find("startup",1,true))
