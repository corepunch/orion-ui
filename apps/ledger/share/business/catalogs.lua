return function(ctx)
  local page = ctx.page
  local ledger = ctx.ledger
  return {
    id="ledger.catalogs", api=1, dependencies={"ledger.counterparties", "ledger.items"},
    owns={},
    commands={
      {id="catalogs.list", mode="read", roles=ctx.view, run=function(args)
        local tables = {doc_types = "cfg", vat_rates = "cfg", units = "cfg", items = "ib", counterparties = "ib"}
        assert(type(args.table) == "string" and tables[args.table], "unsupported catalog")
        local rows = ledger.fetch(tables[args.table], args.table)
        return page(rows, args)
      end},
    },
  }
end
