local orion = require("orion")
local Items = require("orion.model"):extend("ib.items")
local ui = require("ledger.ui")
local sheet = require("label_sheet")
local Labels = {form="labels", title=orion.tr("Print Item Labels")}

function Labels:init()
  self.rows = {}
  self.values = {item=ui.selected("items"),copies=1,barcode="",width=62,height=32}
end

function Labels:data()
  local queue = {}
  for i, row in ipairs(self.rows) do queue[i] = {item_id=row.item_id, sku=row.sku, copies=row.copies} end
  return {
    values=self.values, lists={queue=queue},
    disabled={clear=#self.rows == 0},
  }
end

function Labels:options(values)
  if values.roll and values.letter then orion.fail(orion.tr("Choose only one paper option.")) end
  return {format=values.roll and "single" or values.letter and "letter" or "a4",
          stock_width_mm=values.width, stock_height_mm=values.height}
end

local function print_sheet(rows, options)
  local success, result, count = pcall(sheet.render, rows, options)
  if not success then orion.fail(orion.tr(tostring(result):gsub("^.-:%d+: ", ""))) end
  return result, count
end

function Labels:row(values)
  local item = Items:find(values.item)
  if not item then orion.fail(orion.tr("Choose an item from the catalog.")) end
  return {item_id=item.id,name=item.name,sku=item.sku,
    barcode=values.barcode ~= "" and values.barcode or nil,copies=values.copies}
end

function Labels:add(values)
  local row = self:row(values)
  local queue = {table.unpack(self.rows)}
  queue[#queue + 1] = row
  local _, count = print_sheet(queue, self:options(values))
  self.rows = queue
  self.values = values
  self.values.barcode = ""
  self.values.summary = string.format(orion.tr("Labels in batch: %d"), count)
end

function Labels:clear(values)
  self.rows, self.values = {}, values
  self.values.summary = ""
end

function Labels:save(values)
  local rows = #self.rows > 0 and self.rows or {self:row(values)}
  local output = print_sheet(rows, self:options(values))
  self.values = values
  if ui.save_print_html(output) then
    self.values.summary = orion.tr("Saved. Open the file and print at actual size.")
  end
end

return Labels
