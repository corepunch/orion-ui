return function(ctx)
  local save = ctx.catalog_write(ctx.ledger, "counterparties", {
    name=true, tax_id=true, kpp=true, address=true, bank=true, bank_account=true, bik=true, phone=true, country_code=true,
  }, {"name"})
  return {
    id="ledger.counterparties", api=1, dependencies={},
    owns={"ib.counterparties"},
    commands={{id="counterparties.save", mode="write", roles=ctx.edit, run=save}},
  }
end
