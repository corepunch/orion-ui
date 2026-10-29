return function()
  local Money = {}
  function Money.minor(value)
    assert(type(value) == "number" and value == value and math.abs(value) < 1e12, "invalid money amount")
    local absolute = math.floor(math.abs(value) * 100 + 0.5 + 1e-7)
    return value < 0 and -absolute or absolute
  end
  function Money.round(value) return Money.minor(value) / 100 end
  return Money
end
