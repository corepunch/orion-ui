return function(ctx)
  local save = ctx.catalog_write(ctx.ledger, "items", {name=true, sku=true, unit_id=true, vat_rate_id=true}, {"name", "sku"})
  return {
    id="ledger.items", api=1, dependencies={},
    owns={"ib.items"},
    commands={{id="items.save", mode="write", roles=ctx.edit, run=save}},
  }
end
