-- Replace the sample with your own UTF-8 CSV text or load a selected local file.
local plan = ledger.automation.preview("import.items", {
  csv = "Артикул;Товар;Цена\nEXAMPLE-A;Example item;125,50\n",
  columns = {["Артикул"]="sku", ["Товар"]="name", ["Цена"]="price"},
})
for _, issue in ipairs(plan.errors) do print(issue.code, issue.row, issue.message or issue.field) end
for _, change in ipairs(plan.changes) do
  print(change.id and "UPDATE" or "INSERT", change.after.sku, change.after.name, change.after.price)
end
print("Unchanged", plan.unchanged)
-- Preview deliberately never calls apply.
