ledger = {}
dofile("apps/ledger/share/stdplugs/30-invoices.lua")
dofile("apps/ledger/share/stdplugs/35-settlement-review.lua")
local review = assert(ledger.invoice.review_settlements)

local function event(kind, id, amount, currency)
  return {
    invoice_ref = "invoice-23", currency = currency, kind = kind,
    source = "lodge", request_key = id, actor = "operator-3",
    amount_minor = amount, date = "2026-11-27"
  }
end

for cfg, currency in pairs({["ru-2026"] = "RUB", ["de-2026"] = "EUR"}) do
  local original = {invoice_ref = "invoice-23", currency = currency, configuration = cfg,
    total_minor = 10000, tax_scheme = "ordinary", tax_date = "2026-11-28"}
  local p1, p2, refund = event("payment", "r1", 4000, currency),
    event("payment", "r2", 8000, currency), event("refund", "r3", 2500, currency)
  refund.payment_source, refund.payment_key = "lodge", "r2"

  local part = review(original, {p1,p1})
  assert(part.outstanding_minor == 6000 and part.applied_minor == 4000 and #part.evidence == 1)
  assert(part.status == "review_required" and part.currency == currency)
  local over = review(original, {p1,p2})
  assert(over.advance_minor == 2000 and over.outstanding_minor == 0)
  local result = review(original, {p1,p1,p2,refund,refund})
  assert(result.paid_minor == 12000 and result.refunded_minor == 2500 and result.applied_minor == 9500)
  assert(result.outstanding_minor == 500 and result.advance_minor == 0 and #result.evidence == 3)
  assert(result.evidence[3].original == "lodge/r2" and result.evidence[3].actor == "operator-3")

  assert(not pcall(review, original, {refund,p1,p2}))
  assert(not pcall(review, original, {p1,event("payment", "r1", 4100, currency)}))
  assert(not pcall(review, original, {p1,event("payment", "r4", 4000, "USD")}))
  refund.amount_minor = 8001
  assert(not pcall(review, original, {p1,p2,refund}))
  original.tax_scheme = "unsupported"
  assert(not pcall(review, original, {}))
end

print("settlement review: exact partial payments, advances, refunds, retries and RU/DE boundaries passed")
