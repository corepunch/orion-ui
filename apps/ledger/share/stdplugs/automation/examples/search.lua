-- Run with ledger-run in its default read-only mode.
for _, recipe in ipairs(ledger.automation.search("", {"проверка"}, "recipe")) do
  print(recipe.id .. "\t" .. recipe.title)
end
