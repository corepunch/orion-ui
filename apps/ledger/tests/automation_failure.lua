ledger.transaction(function() ledger.insert("items", {sku="OUTER-ROLLBACK", name="Must not survive"}) end)
error("deliberate script failure after a committed transaction")
