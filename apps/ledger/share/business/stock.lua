local function record_id(value, zero_allowed)
  assert(math.type(value) == "integer" and value >= (zero_allowed and 0 or 1)
         and value <= 2147483647, "invalid stock record id")
  return value
end

return function(ctx)
  return {
    id="ledger.stock", api=1, dependencies={"ledger.items", "ledger.catalogs"},
    owns={"ib.stock_operations"},
    commands={
      {id="stock.balance", mode="read", roles=ctx.view, run=function(args)
        local item = record_id(args.item_id, false)
        local warehouse = record_id(args.warehouse_id, true)
        local row = ctx.ledger.stock_balance(item, warehouse)
        return {item_id=item, warehouse_id=warehouse, on_hand_milli=tostring(row.on_hand_milli),
                reserved_milli=tostring(row.reserved_milli), available_milli=tostring(row.available_milli)}
      end},
    },
  }
end
