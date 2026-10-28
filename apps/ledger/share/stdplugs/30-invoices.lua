-- Shared invoice primitives; national rules stay in each configuration.
ledger.invoice = {}
local M = ledger.invoice
function M.required(value, label)
  assert(type(value) == 'string' and value:match('%S'), label .. ' is required')
  return value
end
function M.date(value, label)
  M.required(value, label)
  local y,m,d = value:match('^(%d%d%d%d)%-(%d%d)%-(%d%d)$')
  y,m,d = tonumber(y),tonumber(m),tonumber(d)
  local days = {31,28,31,30,31,30,31,31,30,31,30,31}
  assert(y and y >= 1900 and m >= 1 and m <= 12, label .. ' must be an ISO calendar date')
  if y%4 == 0 and (y%100 ~= 0 or y%400 == 0) then days[2] = 29 end
  assert(d >= 1 and d <= days[m], label .. ' is not a calendar date')
  return value
end
local function finite(n) return type(n)=='number' and n==n and math.abs(n)<math.huge end
function M.check(ctx, options)
  local d = assert(ctx.document, 'Invoice document is required')
  local org = assert(ledger.find('ib','organizations',1), 'Organization is required')
  local buyer = assert(ledger.find('ib','counterparties',d.counterparty_id), 'Customer is required')
  M.required(d.number, 'Invoice number')
  M.date(d.date,'Invoice date'); M.date(d.supply_date,'Supply date'); M.date(d.tax_date,'Tax point date')
  if d.due_date ~= '' then M.date(d.due_date,'Due date') end
  assert(d.tax_scheme == 'ordinary', 'This invoice form requires the ordinary tax scheme; special schemes need their own form')
  assert(d.currency_id == 0, 'Foreign-currency tax invoices require national-currency tax disclosure and are not supported by this form')
  M.required(org.name,'Supplier name'); M.required(org.address,'Supplier address')
  M.required(buyer.name,'Customer name'); M.required(buyer.address,'Customer address')
  if options.country then
    assert(org.country_code==options.country and buyer.country_code==options.country,
      'This form requires a domestic supply with explicit supplier and customer country codes')
  end
  for _,other in ipairs(ledger.fetch('ib','documents')) do
    if other.id~=d.id and other.number==d.number then error('Invoice number is already used by another document') end
  end
  local lines = ledger.fetch('ib','doc_lines','document_id',d.id)
  assert(#lines>0, 'An invoice requires itemised lines')
  local net,tax,gross = 0,0,0
  for _,line in ipairs(lines) do
    local item=assert(ledger.find('ib','items',line.item_id),'Invoice line has no item')
    M.required(item.name,'Item or service description')
    local rate=assert(ledger.find('cfg','vat_rates',line.vat_rate_id),'Invoice line has no tax code')
    assert(finite(line.quantity) and finite(line.price) and line.quantity>0 and line.price>=0,'Invoice line has invalid quantity or unit price')
    assert(finite(rate.rate) and rate.rate>=0,'Invalid tax rate')
    assert(rate.valid_from=='' or d.tax_date>=rate.valid_from,'Tax rate is not yet effective at the tax point')
    assert(rate.valid_to=='' or d.tax_date<=rate.valid_to,'Tax rate has expired at the tax point')
    local amount=ledger.round2(line.quantity*line.price)
    local vat=ledger.round2(amount*rate.rate/100)
    assert(finite(line.amount) and finite(line.vat) and finite(line.total) and math.abs(amount-line.amount)<0.005 and
      math.abs(vat-line.vat)<0.005 and math.abs(amount+vat-line.total)<0.005,'Invoice line totals are stale or inconsistent')
    if options.standard_only then assert(rate.code=='standard','Only standard-rated domestic services are implemented by this profile') end
    net,tax,gross=net+amount,tax+vat,gross+amount+vat
  end
  assert(finite(d.amount) and finite(d.vat) and finite(d.total) and math.abs(net-d.amount)<0.005 and
    math.abs(tax-d.vat)<0.005 and math.abs(gross-d.total)<0.005,'Invoice totals do not match the lines')
  return org,buyer,lines
end
