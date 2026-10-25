-- This file explicitly requests changes; the host must also be started with --write.
local plan = ledger.automation.preview("import.items", {
  rows = {{sku="EXAMPLE-A", name="Example item", price=125.50}},
})
local records = ledger.automation.apply(plan)
print("Changed records", #records)
