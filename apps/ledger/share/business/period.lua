return function(ctx)
  local page = ctx.page
  local ledger, native = ctx.ledger, ctx.native
  return {
    id="ledger.period", api=1, dependencies={},
    owns={"ib.period_closures"},
    commands={
      {id="period.history", mode="read", roles=ctx.view, run=function(args)
        return page(ledger.fetch("ib", "period_closures"), args)
      end},
      {id="period.preview_close", mode="read", roles=ctx.manage, run=function(args)
        assert(type(args.date) == "string", "invalid date")
        return native.close_preview(args.date)
      end},
      {id="period.close", mode="write", roles=ctx.manage, run=function(args)
        assert(type(args.date) == "string", "invalid date")
        ledger.transaction(function() ledger.close_through(args.date) end)
        return {closed_through = args.date}
      end},
    },
  }
end
