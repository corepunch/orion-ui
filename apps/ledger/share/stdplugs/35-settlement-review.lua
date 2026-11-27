-- Review financial evidence without creating documents or journal entries.
local invoice = assert(ledger.invoice)
local currencies = {["ru-2026"] = "RUB", ["de-2026"] = "EUR"}
local max_minor = 99999999999999

local function key(value, label)
  assert(type(value) == "string" and #value <= 100 and value:match("^[%w_.:-]+$"), "Invalid " .. label)
  return value
end

local function amount(value)
  assert(math.type(value) == "integer" and value > 0 and value <= max_minor, "Invalid exact amount")
  return value
end

local function add(a, b)
  assert(a <= max_minor - b, "Settlement total is out of range")
  return a + b
end

local function refund_key(event)
  if event.kind == "payment" then
    assert(event.payment_source == nil and event.payment_key == nil, "Payment cannot reference another payment")
    return nil
  end
  return key(event.payment_source, "original payment source") .. "/" .. key(event.payment_key, "original payment key")
end

local function same(a, b)
  return a.kind == b.kind and a.actor == b.actor and a.date == b.date and
    a.amount_minor == b.amount_minor and a.original == b.original
end

-- The caller supplies an accountant-selected invoice snapshot and ordered
-- immutable events. Repeating the same source key is accepted only when its
-- evidence is identical. This is never payment execution or posting.
function invoice.review_settlements(snapshot, events)
  assert(type(snapshot) == "table" and not getmetatable(snapshot), "Invalid invoice snapshot")
  local currency = currencies[snapshot.configuration]
  assert(currency and snapshot.currency == currency and snapshot.tax_scheme == "ordinary",
    "Unsupported invoice configuration, currency or tax scheme")
  local id, total = key(snapshot.invoice_ref, "invoice reference"), amount(snapshot.total_minor)
  local tax_date = invoice.date(snapshot.tax_date, "Tax point date")
  assert(type(events) == "table" and not getmetatable(events) and #events <= 10000,
    "Invalid settlement evidence")
  local count = 0
  for index in pairs(events) do
    assert(math.type(index) == "integer" and index >= 1 and index <= #events, "Settlement evidence must be an array")
    count = count + 1
  end
  assert(count == #events, "Settlement evidence must be an array")

  local seen, payments, evidence = {}, {}, {}
  local paid, refunded = 0, 0
  for _, event in ipairs(events) do
    assert(type(event) == "table" and not getmetatable(event) and event.invoice_ref == id and event.currency == currency,
      "Settlement evidence belongs to another invoice or currency")
    assert(event.kind == "payment" or event.kind == "refund", "Unsupported settlement event")
    local source, operation = key(event.source, "source"), key(event.request_key, "request key")
    local identity = source .. "/" .. operation
    local new = {
      source = source, request_key = operation, kind = event.kind,
      actor = key(event.actor, "actor"), date = invoice.date(event.date, "Settlement date"),
      amount_minor = amount(event.amount_minor), original = refund_key(event)
    }
    local previous = seen[identity]
    if previous then
      assert(same(previous, new), "Source request key has conflicting evidence")
    else
      if new.kind == "payment" then
        payments[identity] = { remaining = new.amount_minor, date = new.date }
        paid = add(paid, new.amount_minor)
      else
        local original = payments[new.original]
        assert(original and new.date >= original.date and new.amount_minor <= original.remaining,
          "Refund must reference prior payment and stay within its remaining amount")
        original.remaining = original.remaining - new.amount_minor
        refunded = add(refunded, new.amount_minor)
      end
      seen[identity], evidence[#evidence + 1] = new, new
    end
  end
  local net = paid - refunded
  return {
    invoice_ref = id, currency = currency, tax_date = tax_date, total_minor = total,
    paid_minor = paid, refunded_minor = refunded, applied_minor = math.min(net,total),
    outstanding_minor = math.max(total-net,0), advance_minor = math.max(net-total,0),
    evidence = evidence, status = "review_required"
  }
end
