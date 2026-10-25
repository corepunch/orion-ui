-- The graph is a Lua value; these are the same operations as the ready-made recipes.
local plan = ledger.automation.execute({nodes={
  {id="file", type="source.text", params={text="sku;name;price\nEXAMPLE-A;Example item;125.5\n"}},
  {id="csv", type="parse.csv", input="file"},
  {id="columns", type="rows.map", input="csv", params={
    columns={sku="sku",name="name",price="price"}, types={price="number"}}},
  {id="import", type="plan.upsert", input="columns", params={table="items",keys={"sku"}}},
}, output="import"})
print("Planned changes", #plan.changes, "Errors", #plan.errors)
