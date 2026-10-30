-- Gregorian civil dates, independent of timezone and DST. Days start at 0001-01-01.
return function()
  local Calendar = {}
  local function leap(y) return y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0) end
  local lengths = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
  function Calendar.day(iso)
    local y, m, d
    if type(iso) == "string" then y, m, d = iso:match("^(%d%d%d%d)%-(%d%d)%-(%d%d)$") end
    y, m, d = tonumber(y), tonumber(m), tonumber(d)
    assert(y and y >= 1 and m and m >= 1 and m <= 12 and d and d >= 1 and
      d <= lengths[m] + ((m == 2 and leap(y)) and 1 or 0), "invalid ISO date: " .. tostring(iso))
    local prior = y - 1
    local n = prior * 365 + prior // 4 - prior // 100 + prior // 400 + d - 1
    for month = 1, m - 1 do n = n + lengths[month] + ((month == 2 and leap(y)) and 1 or 0) end
    return n
  end
  function Calendar.between(first, last) return Calendar.day(last) - Calendar.day(first) end
  function Calendar.month_end(iso)
    Calendar.day(iso)
    local y, m = tonumber(iso:sub(1, 4)), tonumber(iso:sub(6, 7))
    return string.format("%04d-%02d-%02d", y, m, lengths[m] + ((m == 2 and leap(y)) and 1 or 0))
  end
  function Calendar.add(iso, days)
    assert(math.type(days) == "integer", "days must be an integer")
    local n = Calendar.day(iso) + days
    assert(n >= 0 and n < 3652059, "date outside 0001..9999")
    local lo, hi = 1, 10000
    while hi - lo > 1 do
      local mid = (lo + hi) // 2
      if Calendar.day(string.format("%04d-01-01", mid)) <= n then lo = mid else hi = mid end
    end
    local rest, month = n - Calendar.day(string.format("%04d-01-01", lo)), 1
    while true do
      local count = lengths[month] + ((month == 2 and leap(lo)) and 1 or 0)
      if rest < count then break end
      rest, month = rest - count, month + 1
    end
    return string.format("%04d-%02d-%02d", lo, month, rest + 1)
  end
  return Calendar
end
