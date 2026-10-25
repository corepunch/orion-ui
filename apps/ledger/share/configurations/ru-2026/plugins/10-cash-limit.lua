-- Плагин: предельный размер расчетов наличными между организациями и ИП —
-- 100 000 руб. по одному договору (указание Банка России от 09.12.2019
-- № 5348-У, п. 4). Предупреждает, когда кассовый документ с контрагентом
-- превышает лимит; проведение не блокирует.
local LIMIT = 100000

ledger.on("post", function(ctx)
  local t = ctx.type
  if t.code ~= "cash_in" and t.code ~= "cash_out" then return end
  if ctx.document.counterparty_id == 0 then return end
  if ctx.document.total > LIMIT then
    ledger.log(string.format("%s %s: наличный расчет %.2f руб. с «%s» превышает лимит %d руб. (указание № 5348-У)",
      t.name, ctx.document.number, ctx.document.total, ledger.field("ib", "counterparties", ctx.document.counterparty_id, "name"), LIMIT))
  end
end)
