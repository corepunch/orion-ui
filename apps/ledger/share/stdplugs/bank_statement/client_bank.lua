-- Read-only preview of 1CClientBankExchange 1.02/1.03 statement files.
local M = {}
local cp1251 = { utf8.codepoint("ЂЃ‚ѓ„…†‡€‰Љ‹ЊЌЋЏђ‘’“”•–—�™љ›њќћџ ЎўЈ¤Ґ¦§Ё©Є«¬­®Ї°±Ііґµ¶·ё№є»јЅѕїАБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдежзийклмнопрстуфхцчшщъыьэюя", 1, -1) }
local cp866 = { utf8.codepoint("АБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдежзийклмноп░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀рстуфхцчшщъыьэюяЁёЄєЇїЎў°∙·√№¤■ ", 1, -1) }
local max_size, max_amount = 8 * 1024 * 1024, 99999999999999

local function trim(s)
  return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function decode(raw)
  if raw:sub(1, 3) == "\239\187\191" then raw = raw:sub(4) end
  if utf8.len(raw) then return raw end
  -- Look at the encoding declaration before decoding any Cyrillic fields.
  local windows = "\202\238\228\232\240\238\226\234\224"
  local dos = "\138\174\164\168\224\174\162\170\160"
  local map
  for _, line in ipairs({ {windows, "windows", cp1251}, {dos, "dos", cp866} }) do
    if raw:lower():match("\n" .. line[1] .. "=%s*" .. line[2] .. "%s*\r?\n") then map = line[3]; break end
  end
  assert(map, "encoding must be UTF-8, declared Windows (CP1251) or declared DOS (CP866)")
  return (raw:gsub("[\128-\255]+", function(bytes)
    local result = {}
    for i = 1, #bytes do
      local c = map[bytes:byte(i) - 127]
      assert(c ~= 0xfffd, "undefined byte in statement encoding")
      result[i] = utf8.char(c)
    end
    return table.concat(result)
  end))
end

local function date(s)
  local d, m, y = (s or ""):match("^(%d%d)%.(%d%d)%.(%d%d%d%d)$")
  d, m, y = tonumber(d), tonumber(m), tonumber(y)
  if not y or y == 0 or not m or m < 1 or m > 12 or d < 1 then return nil end
  local leap = y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0)
  local days = {31, leap and 29 or 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
  if d <= days[m] then return ("%04d-%02d-%02d"):format(y, m, d) end
end

local function amount(s)
  local sign, whole, decimals = (s or ""):match("^(-?)(%d+)[.,]?(%d*)$")
  if not sign or #whole > 12 or #decimals > 2 then return nil end
  if decimals == "" and s:match("[.,]") then return nil end
  local n = tonumber(whole) * 100 + tonumber((decimals .. "00"):sub(1, 2))
  if n <= max_amount then return sign == "-" and -n or n end
end

local function account_number(s)
  return type(s) == "string" and #s == 20 and s:match("^%d+$") ~= nil
end

local function allowed_currency(s)
  return not s or s == "" or s == "643" or s == "810" or s:upper() == "RUB"
end

local function put(fields, key, value, line)
  assert(fields[key] == nil, "duplicate field at line " .. line)
  fields[key] = value
end

local function parse(raw)
  assert(type(raw) == "string" and #raw > 0 and #raw <= max_size, "empty statement or file exceeds 8 MiB")
  assert(not raw:find("[%z\1-\8\11\12\14-\31]"), "invalid control character in statement")
  local header, accounts, sheets, docs = {}, {}, {}, {}
  local section, ended, started, line_no = nil, false, false, 0
  for line in (decode(raw) .. "\n"):gmatch("([^\n]*)\n") do
    line_no = line_no + 1
    line = trim(line)
    assert(#line <= 16384, "line too long at " .. line_no)
    if line ~= "" then
      if not started then
        assert(line == "1CClientBankExchange", "not a 1CClientBankExchange file")
        started = true
      elseif ended then
        error("content after end of statement at line " .. line_no)
      elseif line == "КонецФайла" then
        assert(not section, "file ends inside a section")
        ended = true
      elseif line == "СекцияРасчСчет" or line:match("^СекцияДокумент=") then
        assert(not section, "nested section at line " .. line_no)
        local kind = line:match("^СекцияДокумент=(.*)$")
        assert(not kind or kind ~= "", "empty document kind at line " .. line_no)
        section = { kind = kind, at = line_no, fields = {} }
      elseif line == "КонецРасчСчет" or line == "КонецДокумента" then
        assert(section and (section.kind ~= nil) == (line == "КонецДокумента"),
          "wrong section end at line " .. line_no)
        local bucket = section.kind and docs or sheets
        bucket[#bucket + 1] = section
        section = nil
      else
        local key, value = line:match("^([^=]+)=(.*)$")
        assert(key and trim(key) ~= "", "invalid field at line " .. line_no)
        key, value = trim(key), trim(value)
        if section then
          put(section.fields, key, value, line_no)
        elseif #docs > 0 or #sheets > 0 then
          error("unexpected top-level field at line " .. line_no)
        elseif key == "РасчСчет" then
          assert(account_number(value), "invalid statement account at line " .. line_no)
          accounts[value] = true
        elseif key ~= "Документ" then
          put(header, key, value, line_no)
        end
      end
    end
  end
  assert(ended, "missing end of file; statement may be truncated")
  assert(header["ВерсияФормата"] == "1.02" or header["ВерсияФормата"] == "1.03",
    "supported format versions are 1.02 and 1.03")
  local from, through = date(header["ДатаНачала"]), date(header["ДатаКонца"])
  assert(from and through and from <= through, "invalid statement start/end date")
  return header, accounts, sheets, docs, from, through
end

function M.preview(raw, account)
  assert(account_number(account), "select a 20-digit account from the file")
  local header, accounts, sheets, docs, from, through = parse(raw)
  assert(accounts[account], "selected account is not declared in statement header")
  local result = {
    format = "1c-client-bank-preview-v1", account = account, currency = "RUB",
    period_start = from, period_end = through, incoming_minor = 0, outgoing_minor = 0,
    rows = {}, issues = {}
  }
  local function warn(code, line, direction)
    result.issues[#result.issues + 1] = { code = code, line = line, direction = direction }
  end
  local code = account:sub(6, 8)
  assert((code == "810" or code == "643") and allowed_currency(header["Валюта"])
    and allowed_currency(header["КодВалюты"]), "only RUB statements are supported")
  local seen = {}
  local function add(doc, direction, booking, other)
    local f = doc.fields
    if not allowed_currency(f["Валюта"]) or not allowed_currency(f["КодВалюты"]) then
      warn("unsupported_currency", doc.at, direction); return
    end
    if not date(f["Дата"]) or not date(booking) then
      warn(booking and booking ~= "" and "invalid_date" or "missing_booking_date", doc.at, direction); return
    end
    local day = date(booking)
    if day < from or day > through then warn("outside_period", doc.at, direction); return end
    local value = amount(f["Сумма"])
    if not value or value <= 0 then warn("invalid_amount", doc.at, direction); return end
    if not account_number(f["ПлательщикСчет"]) or not account_number(f["ПолучательСчет"]) or not f["Номер"] or f["Номер"] == "" then
      warn("missing_payment_identity", doc.at, direction); return
    end
    -- Exact source key per 1C file standard; never treat document Date as booking date.
    local key = table.concat({direction, f["ПлательщикСчет"], doc.kind, date(f["Дата"]), f["Номер"]}, "\0")
    local signature = table.concat({day, tostring(value), f["ПолучательСчет"], f["НазначениеПлатежа"] or ""}, "\0")
    if seen[key] then
      warn(seen[key] == signature and "duplicate_payment" or "conflicting_payment_identity", doc.at, direction)
      return
    end
    seen[key] = signature
    local total = direction .. "_minor"
    assert(result[total] <= 100000000000000000 - value, "statement total is out of range")
    result[total] = result[total] + value
    result.rows[#result.rows + 1] = {
      line = doc.at, direction = direction, document_type = doc.kind, number = f["Номер"],
      document_date = date(f["Дата"]), booking_date = day, amount_minor = value,
      counterparty = f[other] or f[other .. "1"] or "", purpose = f["НазначениеПлатежа"] or ""
    }
  end
  for _, doc in ipairs(docs) do
    local f = doc.fields
    if f["ПлательщикСчет"] == account then add(doc, "outgoing", f["ДатаСписано"], "Получатель") end
    if f["ПолучательСчет"] == account then add(doc, "incoming", f["ДатаПоступило"], "Плательщик") end
    if not f["ПлательщикСчет"] or not f["ПолучательСчет"] then warn("missing_account", doc.at) end
  end
  local matching
  for _, sheet in ipairs(sheets) do
    local f = sheet.fields
    if f["РасчСчет"] == account then
      local first, last = date(f["ДатаНачала"]), (f["ДатаКонца"] == nil and through or date(f["ДатаКонца"]))
      if first == from and last == through and not matching then matching = sheet
      else warn("unsupported_balance_section_period_or_duplicate", sheet.at) end
    end
  end
  if matching then
    local f = matching.fields
    local opening, closing = amount(f["НачальныйОстаток"]), amount(f["КонечныйОстаток"])
    local in_total, out_total = amount(f["ВсегоПоступило"]), amount(f["ВсегоСписано"])
    result.opening_minor, result.closing_minor = opening, closing
    if not opening or (f["КонечныйОстаток"] and not closing) or
      (f["ВсегоПоступило"] and (not in_total or in_total < 0)) or
      (f["ВсегоСписано"] and (not out_total or out_total < 0)) then warn("invalid_balance_total", matching.at) end
    if in_total and in_total ~= result.incoming_minor then warn("incoming_total_mismatch", matching.at) end
    if out_total and out_total ~= result.outgoing_minor then warn("outgoing_total_mismatch", matching.at) end
    if opening and closing and opening + (result.incoming_minor - result.outgoing_minor) ~= closing then
      warn("closing_balance_mismatch", matching.at)
    end
  end
  if not matching then warn("no_full_period_balance_section", 1) end
  return result
end

return M
