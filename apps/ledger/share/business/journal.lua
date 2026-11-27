return function(ctx)
  local page = ctx.page
  local ledger = ctx.ledger
  return {
    id="ledger.journal", api=1, dependencies={"ledger.documents"},
    owns={"ib.entries"},
    commands={
      {id="journal.list", mode="read", roles=ctx.view, run=function(args)
        local posted, result = {}, {}
        for _, doc in ipairs(ledger.fetch("ib", "documents")) do if doc.posted then posted[doc.id] = true end end
        for _, entry in ipairs(ledger.fetch("ib", "entries")) do
          if posted[entry.document_id] then result[#result + 1] = entry end
        end
        return page(result, args)
      end},
    },
  }
end
