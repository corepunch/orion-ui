-- Posting rule that exercises the warehouse/contract subject fields and
-- explicit per-side slot-2/3 overrides in ledger.entry.
-- Used by test_subconto2_3_posting in ledger_test.c.
function post_subconto23_lua()
  ledger.entry{
    debit = "41.01", credit = "60.01",
    amount = 300, counterparty = 1, item = 1,
    debit_subconto2 = 55, credit_subconto2 = 66
  }
end
