function fail_after_entry(ctx)
  ledger.entry {debit="51", credit="62.01", amount=123.45}
  error("injected failure after writing an entry")
end

function invalid_entry_nan(ctx)
  ledger.entry {debit="51", credit="62.01", amount=0/0}
end

function invalid_entry_infinite(ctx)
  ledger.entry {debit="51", credit="62.01", amount=1, quantity=math.huge}
end

function invalid_tax_string(ctx) return "0" end
function invalid_tax_nil(ctx) return nil end
function invalid_tax_nan(ctx) return 0/0 end
function invalid_tax_infinite(ctx) return math.huge end

function print_writes_entry(ctx)
  ledger.entry {debit="1000", credit="1100", amount=1}
end
