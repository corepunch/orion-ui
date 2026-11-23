-- Local read-only CLI; output may contain private bank data.
local repo = assert(arg[0]:match("^(.*)apps/ledger/tools/preview_bank_statement%.lua$"), "invoke by repository path")
local parser = dofile(repo .. "apps/ledger/share/stdplugs/bank_statement/client_bank.lua")

local explanations = {
  unsupported_currency = "Неподдерживаемая валюта: требуется рублёвая выписка",
  invalid_date = "Некорректная дата документа или проводки",
  missing_booking_date = "Нет даты списания или зачисления: не включено в оборот",
  outside_period = "Дата банковской проводки вне периода",
  invalid_amount = "Некорректная сумма",
  missing_payment_identity = "Нет номера документа или полного номера счета стороны",
  duplicate_payment = "Повтор платежа: учтён только первый экземпляр",
  conflicting_payment_identity = "Одинаковый ключ платежа, разные данные: сравните строки вручную",
  missing_account = "Не указан счет плательщика или получателя",
  unsupported_balance_section_period_or_duplicate = "Неожиданный период или повтор секции остатков",
  invalid_balance_total = "Некорректный итог выписки",
  incoming_total_mismatch = "Поступления не совпадают с итогом выписки",
  outgoing_total_mismatch = "Списания не совпадают с итогом выписки",
  closing_balance_mismatch = "Конечный остаток не совпадает с рассчитанным по строкам",
  no_full_period_balance_section = "Нет остатков по выбранному счёту за полный период",
}
local function escape(value)
  return (tostring(value):gsub("[&<>\"']", { ["&"]="&amp;", ["<"]="&lt;", [">"]="&gt;", ['"']="&quot;", ["'"]="&#39;" }))
end
local function money(value)
  if not value then return "—" end
  local n = math.abs(value)
  return ("%s%d.%02d ₽"):format(value < 0 and "-" or "", n // 100, n % 100)
end
local function html(report)
  local parts = {
    '<!doctype html><html lang="ru"><meta charset="utf-8"><title>Выписка: предварительный просмотр</title>',
    '<style>body{font:16px system-ui,sans-serif;margin:2rem}table{border-collapse:collapse;width:100%}th,td{text-align:left;border-bottom:1px solid #888;padding:.45rem;overflow-wrap:anywhere}.amount{text-align:right;white-space:nowrap}tr{break-inside:avoid}</style>',
    "<h1>Выписка: предварительный просмотр</h1>",
    "<p>Только чтение. Проводки не созданы; платежи не сопоставлены со счетами.</p>",
    ("<p>Расчётный счёт: %s · Период: %s — %s</p>"):format(escape(report.account), escape(report.period_start), escape(report.period_end)),
    ("<p>Начальный остаток: %s · Поступило: %s · Списано: %s · Конечный остаток: %s</p>"):format(
      money(report.opening_minor), money(report.incoming_minor), money(report.outgoing_minor), money(report.closing_minor)),
  }
  local function append(s) parts[#parts+1] = s end
  if #report.issues > 0 then
    append("<h2>Проверьте перед использованием итогов</h2><ul>")
    for _, issue in ipairs(report.issues) do
      append(("<li>Строка %d: %s</li>"):format(issue.line, escape(explanations[issue.code] or issue.code)))
    end
    append("</ul>")
  end
  append("<table><thead><tr><th>Строка</th><th>Дата банка</th><th>Документ</th><th>Операция</th><th>Контрагент</th><th>Назначение</th><th class=amount>Сумма</th></tr></thead><tbody>")
  for _, row in ipairs(report.rows) do
    append(("<tr><td>%d<td>%s<td>%s № %s<td>%s<td>%s<td>%s<td class=amount>%s"):format(
      row.line, escape(row.booking_date), escape(row.document_type), escape(row.number),
      row.direction == "incoming" and "Поступление" or "Списание",
      escape(row.counterparty), escape(row.purpose), money(row.amount_minor)))
  end
  append("</tbody></table></html>\n")
  return table.concat(parts, "\n")
end

local function main()
  assert((#arg == 2 or (#arg == 3 and arg[3] == "--json")),
    "usage: lua apps/ledger/tools/preview_bank_statement.lua STATEMENT.txt ACCOUNT [--json]")
  local input = assert(io.open(arg[1], "rb"), "cannot open statement file")
  local raw = input:read(8 * 1024 * 1024 + 1)
  input:close()
  local report = parser.preview(raw, arg[2])
  local output
  if arg[3] == "--json" then
    output = dofile(repo .. "share/lua/orion/json.lua").encode(report) .. "\n"
  else output = html(report) end
  assert(io.write(output) and io.flush(), "cannot write preview")
end

local ok, err = pcall(main)
if not ok then
  io.stderr:write("Bank statement preview: ", tostring(err):gsub("^.-:%d+: ", ""), "\n")
  os.exit(1)
end
