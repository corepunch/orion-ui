# Physical warehouse stock

Ledger owns the physical inventory. Current on-hand quantity comes from
posted entries on the configuration's goods cost accounts, plus tracked
transfers. It uses the warehouse analytics slot if one is stored, otherwise
the document's warehouse. Goods posted without a warehouse remain in a
separate unassigned bucket; Ledger does not infer a physical location or
relabel past accounting entries. Goods purchased and sold at named warehouses
participate automatically. Existing monetary valuation and journal reports
are unchanged, and marketplace-reported quantities are not local stock.

Trusted Ledger scripts can read exact thousandths with
`ledger.stock_balance(item_id, warehouse_id)`. It returns
`on_hand_milli`, `reserved_milli`, and `available_milli`. Callers use a
stable, printable ASCII idempotency key unique to an operation and infobase.
Keep the provider, account and order/line IDs (or work-order IDs) in the key
and use different keys for reserve, cancel and completion. Successful retries
with the same payload return the original operation ID. A different payload
with the same key fails.

Example for a draft goods sale that ships the held quantity:

```lua
local hold = ledger.transaction(function()
  return ledger.stock_reserve("marketplace/account-1/order-2/line-1/reserve",
    21, 4, 2500)
end)
local stock = ledger.stock_balance(21, 4)
assert(stock.available_milli == stock.on_hand_milli - stock.reserved_milli)

-- Choose one based on the business event.
-- On cancellation: ledger.stock_release("marketplace/account-1/order-2/line-1/cancel", hold)
-- On completion: ledger.stock_fulfill("marketplace/account-1/order-2/line-1/ship", hold, draft_id)
-- Mutations, including both calls above, go inside a writable ledger.transaction.
```

Reservations and transfers use a separate append-only operation journal, not
additional accounting postings. A transfer must have sufficient available
stock, moves it to another named warehouse without changing the infobase's
total quantity, and is corrected by a separately recorded transfer reversal.
Releasing a reservation restores availability. Fulfillment posts exactly
one matching draft goods sale, consuming the complete reservation and
physical quantity in the same transaction. An accounting reversal to a
fulfilled sale restores its physical stock at the original warehouse; it
does not reopen the completed reservation. Posted changes that would deplete
held stock are rejected. Save the infobase normally to persist operations.

This API represents current balances, not dated warehouse valuation or a
complete warehouse-management UI. It does not yet expose multi-line or
partial fulfillment, per-location costing, intrawarehouse bins, an allocator
for historical unassigned balances, or per-item base-unit conversion.
Configurations without goods cost accounts cannot reserve or transfer
stock via this API. Re-importing a historical infobase after live allocation
requires reconciliation. Provider and work-order integrations must explicitly
adopt the native commands; a read-only marketplace preview cannot reserve
or change physical stock.
