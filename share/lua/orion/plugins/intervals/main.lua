return function(dependencies)
  local calendar = dependencies.calendar
  local Intervals = {}
  function Intervals.valid(first, last)
    local ok, days = pcall(calendar.between, first, last)
    return ok and days > 0
  end
  -- Half-open date intervals: an end on the next start does not collide.
  function Intervals.overlap(a, b, c, d)
    assert(Intervals.valid(a, b) and Intervals.valid(c, d), "invalid date interval")
    return a < d and c < b
  end
  return Intervals
end
