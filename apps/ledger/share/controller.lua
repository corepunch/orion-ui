-- Application policy. Native capabilities own windows and the accounting engine.
local ui = require("ledger.ui")
local orion = require("orion")
local Model = require("orion.model")
local calendar = require("orion.plugins").use("calendar", 1)
local documents, types, entries = Model:extend("ib.documents"), Model:extend("cfg.doc_types"), Model:extend("ib.entries")
local Controller = {}
function Controller:new_document(form)
  local last = documents:find(ui.selected("documents"))
  local kind = last and types:find(last.doc_type_id)
  if not kind or kind.form ~= form then
    kind = nil
    for _, row in ipairs(types:all()) do if row.form == form then kind = row; break end end
  end
  if not kind then return ui.message(orion.tr("The configuration defines no document type of this kind."), orion.tr("New Document")) end
  local date = ""
  if form == "closing" then
    local today = orion.today()
    local year, month = today:match("^(%d+)%-(%d+)")
    year, month = tonumber(year), tonumber(month) + 1
    if month == 13 then year, month = year + 1, 1 end
    date = calendar.add(string.format("%04d-%02d-01", year, month), -1)
  end
  ui.new_document(kind.id, date)
end
function Controller:status()
  local organization = Model:extend("ib.organizations"):all()[1]
  local configuration = Model:extend("cfg.settings"):all()[1]
  local settings = Model:extend("ib.report_settings"):all()[1]
  local from = settings and settings.date_from ~= "" and orion.formatDate(settings.date_from) or ""
  local to = settings and settings.date_to ~= "" and orion.formatDate(settings.date_to) or ""
  local period = from == "" and to == "" and orion.tr("Report period: all dates") or
    string.format(orion.tr("Report period: %s – %s"), from ~= "" and from or "…", to ~= "" and to or "…")
  local posted, drafts = 0, 0
  for _, doc in ipairs(documents:all()) do if doc.posted then posted = posted + 1 else drafts = drafts + 1 end end
  local org = organization and organization.name or ""
  local parts = {org, configuration and configuration.name or "", period, string.format(orion.tr("Posted: %d · not posted: %d"), posted, drafts)}
  if ui.dirty() then parts[#parts + 1] = orion.tr("modified") end
  local asof = ui.asof()
  local subtitle = asof ~= "" and string.format(orion.tr("%s · as of %s"), org, orion.formatDate(asof)) or org
  ui.status(table.concat(parts, "   |   "), subtitle)
end
local forms = {new = "document", new_operation = "operation", new_payroll = "payroll", new_assets = "assets", close_month = "closing"}
local commands = {
  ["file.save"] = ui.save, ["file.configuration"] = ui.configuration, ["file.language"] = ui.language,
  ["file.import_1c"] = ui.import, ["file.addon"] = ui.addon,
  ["file.organization"] = function() ui.form("organization") end,
  ["file.quit"] = function() ui.window("quit") end,
  ["help.assistant"] = ui.assistant, ["help.find_command"] = ui.assistant,
  ["report.settings"] = function() ui.form("report_settings") end,
  ["report.card"] = ui.account_card, ["report.print"] = ui.print_reports,
  ["report.recalc"] = function()
    if not ui.repost() then ui.message(orion.tr("Recalculation failed. Previous accounting entries were restored. Check the configuration and documents."), orion.tr("Recalculation Failed")) end
    ui.changed(0)
  end,
  ["help.about"] = function() ui.message(orion.tr("Ledger 1.0\n\nDouble-entry bookkeeping on Orion.\nNational rules, accounts and legal forms come from\nthe active configuration."), orion.tr("About Ledger")) end,
}
function Controller:command(name)
  if commands[name] then return commands[name]() end
  local group, action = name:match("^([^.]+)%.(.+)$")
  if group == "view" then return ui.page(action) end
  if group == "window" then return ui.window(action) end
  if group == "catalog" then
    local verb, table = action:match("^(%a+)_(.+)$")
    return ui.catalog(table, verb)
  end
  if group ~= "doc" then error("Unknown command: " .. name) end
  if forms[action] then return self:new_document(forms[action]) end
  if action == "entries" then return ui.page("journal_entries") end
  if action == "post_close" then return ui.window("post_close") end
  if action == "open_entry" then
    local entry = entries:find(ui.selected("entries"))
    if entry then ui.edit_document(entry.document_id) end
    return
  end
  local id = ui.selected("documents")
  if id == 0 then return end
  if action == "open" then ui.edit_document(id)
  elseif action == "post" then ui.changed(id)
  elseif action == "unpost" then ui.unpost(id); ui.changed(0)
  elseif action == "print" then ui.print_document(id)
  elseif action == "delete" and ui.confirm(orion.tr("Delete the selected document and its entries?"), orion.tr("Delete Document")) then
    orion.assert(documents:delete(id)); ui.changed(0)
  end
end
return Controller
