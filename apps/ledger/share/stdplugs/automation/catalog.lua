-- Generated from apps/ledger/automation.xml; do not edit.
local M = {
  id = "ledger.automation", version = "1",
  nodes = {
    {id = "source.rows", title = "Rows", input = "none", output = "rows", impl = "source_rows", description = "Use rows supplied as Lua tables.", tags = {"source", "rows"}, aliases = {}, params = {{name = "rows", type = "table", optional = false}}},
    {id = "source.table", title = "Read table", input = "none", output = "rows", impl = "source_table", description = "Read record copies through ledger.fetch.", tags = {"source", "table"}, aliases = {}, params = {{name = "table", type = "string", optional = false}, {name = "db", type = "string", optional = true}}},
    {id = "parse.csv", title = "Parse CSV", input = "text", output = "rows", impl = "csv", description = "Parse UTF-8 CSV with quoted delimiters, escaped quotes, CRLF and multiline cells.", tags = {"parse", "csv"}, aliases = {}, params = {{name = "delimiter", type = "string", optional = true}}},
    {id = "rows.map", title = "Map columns", input = "rows", output = "rows", impl = "map", description = "Rename columns and convert typed values; invalid conversions fail.", tags = {"rows", "map"}, aliases = {}, params = {{name = "columns", type = "table", optional = false}, {name = "types", type = "table", optional = true}}},
    {id = "rows.filter", title = "Filter rows", input = "rows", output = "rows", impl = "filter", description = "Keep rows matching every exact equality condition.", tags = {"rows", "filter"}, aliases = {}, params = {{name = "equals", type = "table", optional = false}}},
    {id = "rows.group", title = "Group rows", input = "rows", output = "rows", impl = "group", description = "Count rows and optionally sum a numeric field by nonempty keys.", tags = {"rows", "group"}, aliases = {}, params = {{name = "keys", type = "table", optional = false}, {name = "sum", type = "string", optional = true}}},
    {id = "check.required", title = "Check required fields", input = "rows", output = "issues", impl = "required", description = "Return row IDs, field names and required issue codes.", tags = {"check", "required"}, aliases = {}, params = {{name = "fields", type = "table", optional = false}}},
    {id = "rows.match", title = "Match records", input = "rows", output = "matches", impl = "match", description = "Match by composite key; ambiguous stored matches fail.", tags = {"rows", "match"}, aliases = {}, params = {{name = "table", type = "string", optional = false}, {name = "keys", type = "table", optional = false}}},
    {id = "plan.upsert", title = "Plan import", input = "rows", output = "plan", impl = "upsert", description = "Prepare inserts/updates with before snapshots; duplicate input keys fail.", tags = {"plan", "upsert"}, aliases = {}, params = {{name = "table", type = "string", optional = false}, {name = "keys", type = "table", optional = false}, {name = "existing_only", type = "boolean", optional = true}}},
    {id = "plan.patch", title = "Plan field replacement", input = "rows", output = "plan", impl = "patch", description = "Plan a field replacement on selected catalog records.", tags = {"plan", "patch"}, aliases = {}, params = {{name = "table", type = "string", optional = false}, {name = "field", type = "string", optional = false}, {name = "value", type = "any", optional = false}}},
    {id = "check.duplicates", title = "Find duplicates", input = "rows", output = "rows", impl = "duplicates", description = "Report candidate groups; never merge records automatically.", tags = {"check", "duplicates"}, aliases = {}, params = {{name = "keys", type = "table", optional = false}}},
    {id = "report.stock", title = "Stock balances", input = "rows", output = "rows", impl = "stock", description = "Aggregate entries through a cutoff date on exact selected item-analytic accounts; date_from is intentionally not used.", tags = {"report", "stock"}, aliases = {}, params = {{name = "accounts", type = "table", optional = false}, {name = "date_to", type = "string", optional = true}}},
    {id = "report.settlements", title = "Counterparty settlements", input = "rows", output = "rows", impl = "settlements", description = "Compute opening, period debit/credit and closing by counterparty for selected accounts.", tags = {"report", "settlements"}, aliases = {}, params = {{name = "accounts", type = "table", optional = false}, {name = "date_from", type = "string", optional = true}, {name = "date_to", type = "string", optional = true}, {name = "counterparty_id", type = "number", optional = true}}},
    {id = "check.month", title = "Check period", input = "rows", output = "issues", impl = "month", description = "Report unposted documents and missing exchange rates; not an exhaustive accounting audit.", tags = {"check", "month"}, aliases = {}, params = {{name = "date_from", type = "string", optional = false}, {name = "date_to", type = "string", optional = false}}},
    {id = "format.csv", title = "Format CSV", input = "rows", output = "text", impl = "export_csv", description = "Return CSV text; the caller chooses whether and where to save it.", tags = {"format", "csv"}, aliases = {}, params = {{name = "columns", type = "table", optional = false}, {name = "delimiter", type = "string", optional = true}}},
    {id = "source.text", title = "Text", input = "none", output = "text", impl = "source_text", description = "Use supplied text, for example UTF-8 CSV.", tags = {"source"}, aliases = {}, params = {{name = "text", type = "string", optional = false}}},
  },
  recipes = {
    {id = "import.items", title = "Import items", impl = "import_items", description = "Map sku/name/price columns, match by SKU and preview catalog inserts/updates. Excel files must first be saved as CSV.", tags = {"товары", "импорт", "CSV", "Excel"}, aliases = {"Загрузить товары из таблицы"}, params = {{name = "rows", type = "table", optional = true}, {name = "csv", type = "string", optional = true}, {name = "columns", type = "table", optional = true}, {name = "delimiter", type = "string", optional = true}}},
    {id = "import.counterparties", title = "Import counterparties", impl = "import_counterparties", description = "Map name/tax_id/kpp columns and match by INN plus KPP; requires INN and allows an explicitly empty KPP.", tags = {"контрагенты", "импорт", "ИНН", "КПП"}, aliases = {"Загрузить контрагентов ИНН КПП"}, params = {{name = "rows", type = "table", optional = true}, {name = "csv", type = "string", optional = true}, {name = "columns", type = "table", optional = true}, {name = "delimiter", type = "string", optional = true}}},
    {id = "import.rates", title = "Import exchange rates", impl = "import_rates", description = "Map currency_id/date/rate columns and match by currency and date.", tags = {"валюта", "курсы", "импорт"}, aliases = {"Загрузить курсы валют"}, params = {{name = "rows", type = "table", optional = true}, {name = "csv", type = "string", optional = true}, {name = "columns", type = "table", optional = true}, {name = "delimiter", type = "string", optional = true}}},
    {id = "prices.update", title = "Update prices", impl = "prices_update", description = "Update current item prices by SKU; unknown SKUs are plan errors. Historical documents are unchanged.", tags = {"товары", "цены", "прайс", "CSV"}, aliases = {"Обновить цены из прайса поставщика"}, params = {{name = "rows", type = "table", optional = true}, {name = "csv", type = "string", optional = true}, {name = "columns", type = "table", optional = true}, {name = "delimiter", type = "string", optional = true}}},
    {id = "duplicates.items", title = "Find duplicate items", impl = "duplicates_items", description = "Report candidate item duplicates by SKU, or supplied keys.", tags = {"товары", "дубли", "проверка"}, aliases = {"Задвоились товары дубли номенклатуры"}, params = {{name = "keys", type = "table", optional = true}}},
    {id = "duplicates.counterparties", title = "Find duplicate counterparties", impl = "duplicates_counterparties", description = "Report candidates by INN/KPP, or supplied keys; empty keys are skipped.", tags = {"контрагенты", "дубли", "проверка"}, aliases = {"Повторяющиеся контрагенты дубли"}, params = {{name = "keys", type = "table", optional = true}}},
    {id = "duplicates.documents", title = "Find duplicate documents", impl = "duplicates_documents", description = "Report candidates by type, counterparty, number, date and total; no automatic deletion.", tags = {"документы", "дубли", "проверка"}, aliases = {"Повторяющиеся документы задвоились"}, params = {{name = "keys", type = "table", optional = true}}},
    {id = "check.required", title = "Check required fields", impl = "check_required", description = "Return empty required fields for a chosen infobase table.", tags = {"реквизиты", "проверка", "ошибки"}, aliases = {"Проверить пустые реквизиты ошибки заполнения"}, params = {{name = "table", type = "string", optional = false}, {name = "fields", type = "table", optional = false}}},
    {id = "fields.replace", title = "Replace catalog field", impl = "fields_replace", description = "Filter catalog rows by exact equalities and preview replacement of one field.", tags = {"реквизиты", "изменение", "заполнение"}, aliases = {"Массово заполнить заменить реквизит"}, params = {{name = "table", type = "string", optional = false}, {name = "equals", type = "table", optional = false}, {name = "field", type = "string", optional = false}, {name = "value", type = "any", optional = false}}},
    {id = "report.stock", title = "Check stock balances", impl = "report_stock", description = "Read item-analytic entries on selected account codes; report quantity, value and negative flag.", tags = {"товары", "остатки", "проверка"}, aliases = {"Отрицательные остатки товары склад"}, params = {{name = "accounts", type = "table", optional = false}, {name = "date_from", type = "string", optional = true}, {name = "date_to", type = "string", optional = true}}},
    {id = "report.settlements", title = "Counterparty settlements", impl = "report_settlements", description = "Report opening, period movements and closing on selected counterparty-analytic accounts. No contract or overdue analysis is implied.", tags = {"контрагенты", "долги", "сверка"}, aliases = {"Сверка взаиморасчетов долги контрагент"}, params = {{name = "accounts", type = "table", optional = false}, {name = "date_from", type = "string", optional = true}, {name = "date_to", type = "string", optional = true}, {name = "counterparty_id", type = "integer", optional = true}}},
    {id = "check.month", title = "Check before closing", impl = "check_month", description = "Check period for unposted documents and missing exchange rates. Does not perform closing or repair accounting automatically.", tags = {"закрытие", "документы", "проверка"}, aliases = {"Почему не закрывается месяц проверка"}, params = {{name = "date_from", type = "string", optional = true}, {name = "date_to", type = "string", optional = true}}},
  },
}

function M.bind_nodes(impl)
  return {
    ["source.rows"] = assert(impl.source_rows, "missing implementation: source_rows"),
    ["source.table"] = assert(impl.source_table, "missing implementation: source_table"),
    ["parse.csv"] = assert(impl.csv, "missing implementation: csv"),
    ["rows.map"] = assert(impl.map, "missing implementation: map"),
    ["rows.filter"] = assert(impl.filter, "missing implementation: filter"),
    ["rows.group"] = assert(impl.group, "missing implementation: group"),
    ["check.required"] = assert(impl.required, "missing implementation: required"),
    ["rows.match"] = assert(impl.match, "missing implementation: match"),
    ["plan.upsert"] = assert(impl.upsert, "missing implementation: upsert"),
    ["plan.patch"] = assert(impl.patch, "missing implementation: patch"),
    ["check.duplicates"] = assert(impl.duplicates, "missing implementation: duplicates"),
    ["report.stock"] = assert(impl.stock, "missing implementation: stock"),
    ["report.settlements"] = assert(impl.settlements, "missing implementation: settlements"),
    ["check.month"] = assert(impl.month, "missing implementation: month"),
    ["format.csv"] = assert(impl.export_csv, "missing implementation: export_csv"),
    ["source.text"] = assert(impl.source_text, "missing implementation: source_text"),
  }
end

function M.bind_recipes(impl)
  return {
    ["import.items"] = assert(impl.import_items, "missing implementation: import_items"),
    ["import.counterparties"] = assert(impl.import_counterparties, "missing implementation: import_counterparties"),
    ["import.rates"] = assert(impl.import_rates, "missing implementation: import_rates"),
    ["prices.update"] = assert(impl.prices_update, "missing implementation: prices_update"),
    ["duplicates.items"] = assert(impl.duplicates_items, "missing implementation: duplicates_items"),
    ["duplicates.counterparties"] = assert(impl.duplicates_counterparties, "missing implementation: duplicates_counterparties"),
    ["duplicates.documents"] = assert(impl.duplicates_documents, "missing implementation: duplicates_documents"),
    ["check.required"] = assert(impl.check_required, "missing implementation: check_required"),
    ["fields.replace"] = assert(impl.fields_replace, "missing implementation: fields_replace"),
    ["report.stock"] = assert(impl.report_stock, "missing implementation: report_stock"),
    ["report.settlements"] = assert(impl.report_settlements, "missing implementation: report_settlements"),
    ["check.month"] = assert(impl.check_month, "missing implementation: check_month"),
  }
end

function M.bind_api(impl, target)
  assert(type(impl.search) == "function", "missing API implementation: search")
  target.search = function(...)
    local args = table.pack(...)
    assert(args.n <= 3, "too many arguments: search")
    assert(args[1] == nil or (type(args[1]) == "string"), "invalid argument: query")
    assert(args[2] == nil or (type(args[2]) == "table"), "invalid argument: tags")
    assert(args[3] == nil or (type(args[3]) == "string"), "invalid argument: kind")
    return impl.search(table.unpack(args, 1, args.n))
  end
  assert(type(impl.preview) == "function", "missing API implementation: preview")
  target.preview = function(...)
    local args = table.pack(...)
    assert(args.n <= 2, "too many arguments: preview")
    assert((type(args[1]) == "string"), "invalid argument: id")
    assert(args[2] == nil or (type(args[2]) == "table"), "invalid argument: params")
    return impl.preview(table.unpack(args, 1, args.n))
  end
  assert(type(impl.execute) == "function", "missing API implementation: execute")
  target.execute = function(...)
    local args = table.pack(...)
    assert(args.n <= 1, "too many arguments: execute")
    assert((type(args[1]) == "table"), "invalid argument: graph")
    return impl.execute(table.unpack(args, 1, args.n))
  end
  assert(type(impl.apply) == "function", "missing API implementation: apply")
  target.apply = function(...)
    local args = table.pack(...)
    assert(args.n <= 1, "too many arguments: apply")
    assert((type(args[1]) == "table"), "invalid argument: plan")
    return impl.apply(table.unpack(args, 1, args.n))
  end
  assert(type(impl.register) == "function", "missing API implementation: register")
  target.register = function(...)
    local args = table.pack(...)
    assert(args.n <= 3, "too many arguments: register")
    assert((type(args[1]) == "table"), "invalid argument: descriptor")
    assert((type(args[2]) == "table"), "invalid argument: nodes")
    assert((type(args[3]) == "table"), "invalid argument: recipes")
    return impl.register(table.unpack(args, 1, args.n))
  end
end

return M
