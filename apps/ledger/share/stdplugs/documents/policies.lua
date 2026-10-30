-- First-party workflows use the same read API and executor as custom Lua code.
local policies = {}
function policies.number_document(ctx)
  local document, kind = ctx.document, ctx.type
  if not kind or document.number ~= "" then return {} end
  local prefix, last = kind.prefix, 0
  for _, other in ipairs(ledger.fetch("ib", "documents")) do
    if other.number:sub(1, #prefix) == prefix then
      -- Match the native numbering policy's leading decimal suffix semantics.
      local suffix = other.number:sub(#prefix + 1):match("^%s*([+-]?%d+)")
      local number = tonumber(suffix) or 0
      if number > last then last = number end
    end
  end
  return {document = {number = prefix .. string.format("%04d", last + 1)}}
end
function policies.fill_payroll(ctx)
  local kind = ctx.type
  if not kind or kind.form ~= "payroll" then return {} end
  local rows = {}
  for _, employee in ipairs(ledger.fetch("ib", "employees")) do
    local amount = employee.salary
    if kind.base == "net" then
      local balance = ledger.turnover(kind.debit_id, employee.id)
      amount = ledger.round2(balance.cr - balance.dr)
    end
    if amount > 0 then rows[#rows + 1] = {employee_id = employee.id, amount = amount} end
  end
  return {replace = {payroll_lines = rows}}
end
return policies
