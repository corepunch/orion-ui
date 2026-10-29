-- The renderer owns syntax and traversal. Sources own their domain's data.
return function(dependencies)
  local Escape, Filters = dependencies.text_escape, dependencies.template_filters
  local orion = require("orion")
  local Template = {html = Escape.html}
  local function parse(text)
    local position = 1
    local function nodes(expected)
      local out = {}
      while position <= #text do
        local first = text:find("{{", position, true)
        if not first then out[#out + 1] = text:sub(position); position = #text + 1; break end
        out[#out + 1] = text:sub(position, first - 1)
        local last = assert(text:find("}}", first + 2, true), "unterminated template tag")
        local tag = text:sub(first + 2, last - 1):match("^%s*(.-)%s*$")
        position = last + 2
        if tag:sub(1, 1) == "/" then
          assert(tag:sub(2) == expected, "unmatched template closing tag: " .. tag)
          return out
        elseif tag:sub(1, 1) == "#" then
          local name, rest = tag:match("^#([%w_]+)(.*)$")
          assert(name, "invalid template block: " .. tag)
          local block = {name = name, filters = {}}
          for condition in rest:gmatch("%S+") do
            local field, operator, value = condition:match("^([%w_.]+)(!?=)(.*)$")
            assert(field, "invalid template condition: " .. condition)
            block.filters[#block.filters + 1] = {field = field, value = value, negate = operator == "!="}
          end
          block.children = nodes(name)
          out[#out + 1] = block
        else
          local name, filter = tag:match("^([%w_.]+)|?([%w_]*)$")
          assert(name, "invalid template field: " .. tag)
          out[#out + 1] = {field = name, filter = filter ~= "" and filter or nil}
        end
      end
      assert(not expected, "unterminated template block: " .. tostring(expected))
      return out
    end
    return nodes()
  end
  local function path(row, name)
    if type(row) ~= "table" then return nil end
    if row[name] ~= nil then return row[name] end
    local value = row
    for field in name:gmatch("[^.]+") do
      if type(value) ~= "table" then return nil end
      value = value[field]
    end
    return value
  end
  function Template.render(text, fields, options)
    options, fields = options or {}, fields or {}
    local source, output = options.source, {}
    local function resolve(row, name, ordinal)
      if name == "n" then return tostring(ordinal) end
      local value
      if source then value = source.resolve(row, name) else value = path(row, name); if value == nil then value = path(fields, name) end end
      assert(value ~= nil, "missing template field: " .. name)
      return tostring(value)
    end
    local function matches(row, conditions, ordinal, numeric)
      for _, condition in ipairs(conditions) do
        local actual = resolve(row, condition.field, ordinal)
        local equal
        if numeric and condition.value ~= "" and not condition.value:find("[^%d.,%-]") then
          equal = orion.parseNumber(actual) == orion.parseNumber(condition.value)
        else equal = actual == condition.value end
        if equal == condition.negate then return false end
      end
      return true
    end
    local function render(nodes, row, ordinal)
      for _, node in ipairs(nodes) do
        if type(node) == "string" then output[#output + 1] = node
        elseif node.field then
          local value = Filters.apply(resolve(row, node.field, ordinal), node.filter, source and source.filter)
          output[#output + 1] = Escape.apply(value, options.format)
        elseif node.name == "if" then
          if #node.filters > 0 and matches(row, node.filters, ordinal, false) then render(node.children, row, ordinal) end
        else
          local rows = source and source.rows(node.name, row) or path(row, node.name) or path(fields, node.name)
          assert(type(rows) == "table", "unknown template block: " .. node.name)
          local n = 0
          for _, child in ipairs(rows) do
            if matches(child, node.filters, n + 1, true) then n = n + 1; render(node.children, child, n) end
          end
        end
      end
    end
    render(parse(text), source and 0 or fields, 0)
    return table.concat(output)
  end
  return Template
end
