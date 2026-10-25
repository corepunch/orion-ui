-- Fault-injection policies in an isolated configuration (no standard plugins).
ledger.executor.register("documents.number", function()
  return {document={total=999}}
end)
ledger.executor.register("payroll.fill", function(ctx)
  if ctx.document.comment == "bad-reference" then
    return {replace={payroll_lines={{employee_id=1,amount=100},{employee_id=999999,amount=200}}}}
  elseif ctx.document.comment == "bad-number" then
    return {replace={payroll_lines={{employee_id=1,amount=math.huge}}}}
  elseif ctx.document.comment == "missing-field" then
    return {replace={payroll_lines={{employee_id=1}}}}
  elseif ctx.document.comment == "bad-field" then
    return {replace={payroll_lines={{employee_id=1,amount=100,document_id=999}}}}
  elseif ctx.document.comment == "bad-array" then
    return {replace={payroll_lines={stray={employee_id=1,amount=100}}}}
  elseif ctx.document.comment == "write-in-policy" then
    ledger.entry{debit="50.01",credit="70",amount=100}
  end
  return {replace={payroll_lines={{employee_id=1,amount=100},{employee_id=2,amount=200}}}}
end)
