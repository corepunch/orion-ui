for _, doc in ipairs(ledger.fetch("ib","documents")) do
  if doc.number == "EXEC-ACCRUAL" then
    ledger.executor.execute("payroll.fill",doc.id)
    error("abort after an executor commit")
  end
end
error("fixture document missing")
