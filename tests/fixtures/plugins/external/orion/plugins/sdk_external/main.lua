return function(dependencies)
  return {next_day = function(date) return dependencies.calendar.add(date, 1) end}
end
