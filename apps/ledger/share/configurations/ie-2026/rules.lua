-- Reject cases this development profile cannot calculate accurately.
function validate_domestic_service(ctx)
  if ctx.document.currency_id ~= 0 then error("Foreign currency is not supported by this profile") end
  if ctx.document.date < "2026-01-01" or ctx.document.date > "2026-12-31" then
    error("This configuration has only been reviewed for 2026")
  end
  if ctx.type.vat_book ~= 0 then
    ledger.invoice.check(ctx, {country="IE",standard_only=true})
    assert(ctx.document.supply_kind=="services", "This profile requires a domestic service supply")
  end
  local lines = ledger.fetch("ib", "doc_lines", "document_id", ctx.document.id)
  if ctx.type.vat_book ~= 0 and #lines == 0 then error("Tax invoices require itemised lines") end
  for _, line in ipairs(lines) do
    local rate = ledger.find("cfg", "vat_rates", line.vat_rate_id)
    if not rate or rate.code ~= "standard" then
      error("Only ordinary domestic standard-rated services are verified; reduced/zero/special treatments require implementation")
    end
    if line.quantity < 0 or line.price < 0 then error("Use a supported correction workflow; credit notes are not implemented") end
  end
end

function validate_invoice(ctx)
  local org = ledger.invoice.check(ctx, {country="IE",standard_only=true})
  ledger.invoice.required(org.vat_id,"Supplier VAT registration number")
  assert(ctx.document.supply_kind=="services", "This profile supports domestic services; goods need inventory and classification rules")
end
