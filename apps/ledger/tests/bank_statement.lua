local root = (arg[0]:match("^(.*)apps/ledger/tests/bank_statement%.lua$") or "")
local bank = dofile(root .. "apps/ledger/share/stdplugs/bank_statement/client_bank.lua")
local a, b, fx = "40702810000000000001", "40702810000000000002", "40702840000000000001"
local header = ([[1CClientBankExchange
ВерсияФормата=1.03
Кодировка=Windows
ДатаНачала=01.10.2026
ДатаКонца=31.10.2026
РасчСчет=%s
РасчСчет=%s
РасчСчет=%s
СекцияРасчСчет
РасчСчет=%s
ДатаНачала=01.10.2026
НачальныйОстаток=1000
ВсегоПоступило=123.45
ВсегоСписано=25.25
КонечныйОстаток=1098.20
КонецРасчСчет
]]):format(a, b, fx, a)
local function doc(n, date, payer, receiver, money, debited, credited, options)
  local s = ([[СекцияДокумент=Платежное поручение
Номер=%s
Дата=%s
Сумма=%s
ПлательщикСчет=%s
ПолучательСчет=%s
Плательщик=Тестовая фирма
Получатель=Тестовый поставщик
НазначениеПлатежа=<тест>& = Тест
]]):format(n, date, money, payer, receiver)
  if debited then s = s .. "ДатаСписано=" .. debited .. "\n" end
  if credited then s = s .. "ДатаПоступило=" .. credited .. "\n" end
  return s .. (options or "") .. "КонецДокумента\n"
end
local incoming = doc("01", "28.09.2026", b, a, "123.45", "28.09.2026", "01.10.2026")
local outgoing = doc("02", "02.10.2026", a, b, "25,25", "03.10.2026", "03.10.2026")
local eof = "КонецФайла\n"
local good = header .. incoming .. outgoing .. eof
local p = bank.preview("\239\187\191" .. good:gsub("\n", "\r\n"), a)
assert(#p.rows == 2 and #p.issues == 0)
assert(p.rows[1].booking_date == "2026-10-01" and p.rows[1].document_date == "2026-09-28")
assert(p.rows[1].number == "01" and p.rows[1].purpose == "<тест>& = Тест")
assert(p.incoming_minor == 12345 and p.outgoing_minor == 2525 and p.closing_minor == 109820)
p = bank.preview(header .. incoming .. outgoing .. incoming .. eof, a)
assert(#p.rows == 2 and #p.issues == 1 and p.issues[1].code == "duplicate_payment")
local conflict = incoming:gsub("Сумма=123%.45","Сумма=999.45")
p = bank.preview(header .. incoming .. outgoing .. conflict .. eof, a)
assert(#p.rows == 2 and #p.issues == 1 and p.issues[1].code == "conflicting_payment_identity")
local invalid = {
  { doc("03", "30.02.2026", b, a, "10.00", nil, "15.10.2026"), "invalid_date" },
  { doc("03", "15.10.2026", b, a, "10.00", nil, "15.10.2026", "Валюта=USD\n"), "unsupported_currency" },
  { doc("03", "15.10.2026", b, a, "10.00", nil, nil), "missing_booking_date" },
  { doc("03", "15.10.2026", b, a, "10.00", nil, "01.11.2026"), "outside_period" },
  { doc("03", "15.10.2026", b, a, "10.001", nil, "15.10.2026"), "invalid_amount" },
}
for _, sample in ipairs(invalid) do
  p = bank.preview(header .. incoming .. outgoing .. sample[1] .. eof, a)
  assert(#p.rows == 2 and #p.issues == 1 and p.issues[1].code == sample[2])
end
p = bank.preview(header .. incoming .. eof, a)
assert(#p.rows == 1 and p.issues[1].code == "outgoing_total_mismatch")
assert(p.issues[2].code == "closing_balance_mismatch")
assert(not pcall(bank.preview, good, fx))
assert(not pcall(bank.preview, good, "40702810000000000005"))
assert(not pcall(bank.preview, header .. incoming .. outgoing, a))
assert(not pcall(bank.preview, good .. outgoing, a))
assert(not pcall(bank.preview, good:gsub("Сумма=123%.45","Сумма=123.45\nСумма=999"), a))

-- One internal transfer appears once for each selected account, using that
-- account's own booking date rather than the other side or the document date.
local transfer = doc("04", "03.10.2026", a, b, "50.25", "04.10.2026", "05.10.2026")
local transfer_header = header:match("^(.-)СекцияРасчСчет")
local function balance(account, incoming, outgoing, closing)
  return ([[СекцияРасчСчет
РасчСчет=%s
ДатаНачала=01.10.2026
НачальныйОстаток=100
ВсегоПоступило=%s
ВсегоСписано=%s
КонечныйОстаток=%s
КонецРасчСчет
]]):format(account, incoming, outgoing, closing)
end
local transfer_file = transfer_header .. balance(a, "0", "50.25", "49.75")
  .. balance(b, "50.25", "0", "150.25") .. transfer .. eof
local source = bank.preview(transfer_file, a)
local target = bank.preview(transfer_file, b)
assert(#source.rows == 1 and #source.issues == 0 and source.rows[1].direction == "outgoing")
assert(source.rows[1].booking_date == "2026-10-04")
assert(source.outgoing_minor == 5025 and source.incoming_minor == 0)
assert(#target.rows == 1 and #target.issues == 0 and target.rows[1].direction == "incoming")
assert(target.rows[1].booking_date == "2026-10-05")
assert(target.incoming_minor == 5025 and target.outgoing_minor == 0)

-- Verify raw Cyrillic declarations and values in both legacy code pages.
local function legacy(s, upper_start, lower_start, special)
  local out = {}
  for _, c in utf8.codes(s) do
    local n = c < 128 and c or
      c >= 0x410 and c <= 0x42f and upper_start + c - 0x410 or
      c >= 0x430 and c <= 0x43f and lower_start + c - 0x430 or
      c >= 0x440 and c <= 0x44f and special[1] + c - 0x440 or
      c == 0x401 and special[2] or c == 0x451 and special[3]
    out[#out + 1] = string.char(assert(n))
  end
  return table.concat(out)
end
for _, encoded in ipairs({
  legacy(good, 192, 224, {240, 168, 184}),
  legacy(good:gsub("Кодировка=Windows","Кодировка=DOS"), 128, 160, {224, 240, 241})
}) do
  p = bank.preview(encoded, a)
  assert(#p.rows == 2 and #p.issues == 0 and p.rows[1].counterparty == "Тестовая фирма")
end
if arg[1] then
  local file = assert(io.open(arg[1], "wb"))
  assert(file:write(legacy(good, 192, 224, {240, 168, 184})))
  file:close()
end
print("Bank statement preview tests passed")
