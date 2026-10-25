-- Generated from apps/ledger/executors.xml; do not edit.
return function(implementations)
  ledger.executor.register("documents.number", assert(implementations.number_document))
  ledger.executor.register("payroll.fill", assert(implementations.fill_payroll))
end
