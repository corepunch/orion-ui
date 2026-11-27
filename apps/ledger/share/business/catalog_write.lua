-- Narrow business inputs; the Ledger datasource still owns validation and writes.
return function(ledger, table_name, allowed, required)
  return function(args)
    local fields = args.fields
    assert(type(fields) == "table" and not getmetatable(fields), "fields must be an object")
    for key in pairs(fields) do assert(allowed[key], "unsupported catalog field") end
    for _, key in ipairs(required) do
      assert(type(fields[key]) == "string" and fields[key]:match("%S"), key .. " is required")
    end
    local record = ledger.transaction(function()
      if args.id ~= nil then
        assert(math.type(args.id) == "integer" and args.id > 0, "invalid id")
        return ledger.update(table_name, args.id, fields)
      end
      return ledger.insert(table_name, fields)
    end)
    return {id = record.id}
  end
end
