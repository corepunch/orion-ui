assert(require("ledger.executor") == ledger.executor)
local doc
for _, row in ipairs(ledger.fetch("ib", "documents")) do if row.number == "EXEC-ACCRUAL" then doc = row end end
assert(doc)
local plan = ledger.executor.execute("payroll.fill", doc.id)
assert(#plan.replace.payroll_lines == 3)
assert(ledger.find("ib","documents",doc.id).total == 36000)
local ok, err = pcall(ledger.executor.register,"documents.number",function() return {} end)
assert(not ok and err:find("startup",1,true))
ok, err = pcall(ledger.executor.execute,"unknown",doc.id)
assert(not ok and err:find("unknown executor",1,true))

local good, message = pcall(ledger.executor.execute,"documents.number",doc.id + 4294967296)
assert(not good and message:find("out of range",1,true))
