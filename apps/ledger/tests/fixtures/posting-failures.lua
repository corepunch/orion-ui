function fail_after_entry(ctx)
  ledger.entry {debit="51", credit="62.01", amount=123.45}
  error("injected failure after writing an entry")
end

function invalid_tax_string(ctx) return "0" end
function invalid_tax_nil(ctx) return nil end
function invalid_tax_nan(ctx) return 0/0 end
function invalid_tax_infinite(ctx) return math.huge end
