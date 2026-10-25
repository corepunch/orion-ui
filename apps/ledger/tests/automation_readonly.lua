assert(ledger.automation)
local ok, err = pcall(function() ledger.transaction(function() ledger.insert("items", {sku="DENIED"}) end) end)
assert(not ok and err:find("writable script", 1, true))
assert(#ledger.automation.preview("duplicates.items") == 0)
