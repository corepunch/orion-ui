-- Правила конфигурации ru-2026, которых нет в таблицах configuration.xml.
-- Функции вызываются платформой по имени из полей script= документов,
-- налогов и правил закрытия месяца (см. apps/ledger/ledger_script.c и
-- COMPLIANCE.md). Обновление законодательства — правка этого файла и
-- configuration.xml, без пересборки программы.

local MROT = 27093.00          -- МРОТ с 01.01.2026 (Федеральный закон от 28.11.2025 № 429-ФЗ)

-- Пониженный тариф страховых взносов для МСП приоритетных отраслей
-- (пп. 17 п. 1 ст. 427 НК РФ; распоряжение Правительства РФ от 27.12.2025
-- № 4125-р): 30% с выплат в пределах 1,5 МРОТ в месяц и 15% свыше. Выплаты
-- сверх единой предельной базы — 15,1% (ctx.tax.cap_year, ctx.tax.rate_above).
-- Включается строкой payroll_taxes с script="contributions_sme".
function contributions_sme(ctx)
  local threshold = 1.5 * MROT
  local gross, ytd = ctx.line.amount, ctx.ytd
  local amount = 0
  local within_base = math.max(0, math.min(gross, ctx.tax.cap_year - ytd))
  local above_base = gross - within_base
  local low = math.min(within_base, threshold)
  amount = low * 0.30 + (within_base - low) * 0.15 + above_base * ctx.tax.rate_above / 100
  return ledger.round2(amount)
end

-- Списание товаров (недостача, порча): Дт 94 Кт 41.01 по средней
-- себестоимости на дату документа (ФСБУ 5/2019, п. 36), количество и
-- номенклатура — на кредитовой стороне. Документ типа "writeoff" с формой
-- "document": его строки дают номенклатуру и количество, цена не нужна.
function post_writeoff(ctx)
  local lines = ledger.fetch("ib", "doc_lines", "document_id", ctx.document.id)
  for _, line in ipairs(lines) do
    local stock = ledger.turnover(ctx.type.credit_id, line.item_id)
    local qty, value = stock.dr_qty - stock.cr_qty, stock.dr - stock.cr
    if qty <= 0 then
      ledger.log(string.format("списание %s: номенклатура %d отсутствует на складе", ctx.document.number, line.item_id))
    else
      local cost = line.quantity >= qty and value or value * line.quantity / qty
      ledger.entry{ debit = ctx.type.debit_id, credit = ctx.type.credit_id, amount = cost, quantity = line.quantity,
                    item = line.item_id, content = "Списание: " .. ledger.field("ib", "items", line.item_id, "name") }
    end
  end
end
