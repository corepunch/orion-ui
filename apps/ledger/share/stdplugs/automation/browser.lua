local orion = require("orion")
local ui = require("ledger.ui")
local Ledger = require("ledger")
local Json = require("orion.json")
local Search, catalog = require("search"), require("catalog")
local Browser = {form="recipes", title=orion.tr("Automation Recipes")}

local presets = {
  ["check.required"]=[[{"table":"items","fields":["sku","name"]}]],
  ["report.item_documents"]=[[{"sku":"","posted_only":false}]],
  ["import.items"]=[[{"csv":"sku;name;price\n0001;Example;2.50"}]],
  ["import.counterparties"]=[[{"csv":"name;tax_id;kpp\nExample;0000000001;"}]],
  ["import.rates"]=[[{"rows":[{"currency_id":1,"date":"2026-01-01","rate":1.25}]}]],
  ["prices.update"]=[[{"csv":"sku;price\n0001;2.50"}]],
  ["report.stock"]=[[{"accounts":[]}]],
  ["report.settlements"]=[[{"accounts":[]}]],
  ["fields.replace"]=[[{"table":"items","equals":{"sku":"0001"},"field":"name","value":"Example"}]],
  ["print.item_labels"]=[[{"item_id":1,"copies":1}]],
  ["print.product_card"]=[[{"item_id":1}]],
  ["report.pick_orders"]=[[{"orders":{"orders":[]}}]],
  ["print.order_labels"]=[[{"orders":{"orders":[]}}]],
  ["preview.bank_statement"]=[[{"account":"","statement":""}]],
}
local csv_recipes = { ["import.items"]=true, ["import.counterparties"]=true, ["import.rates"]=true, ["prices.update"]=true }
local order_recipes = { ["report.pick_orders"]=true, ["print.order_labels"]=true }
local by_id = {}
for _, item in ipairs(catalog.recipes) do by_id[item.id] = item end

function Browser:init()
  self.values = {query="", tags="", params="{}", details="", output="", key=""}
  self:search(self.values)
end

function Browser:search(values)
  local tags = {}
  for tag in (values.tags or ""):gmatch("[^,]+") do tags[#tags+1] = tag end
  self.rows, self.visible = {}, {}
  for _, item in ipairs(Search.find(catalog, values.query, tags, "recipe")) do
    self.visible[item.id] = true
    self.rows[#self.rows+1] = {recipe_id=item.id, title=item.title, tags=table.concat(item.tags,", ")}
  end
  self.selected, self.printable = nil, nil
  self.values = {query=values.query, tags=values.tags, params="{}", details="", output="", key="",
    count=string.format(orion.tr("Recipes found: %d"), #self.rows)}
end

function Browser:data()
  return {values=self.values, lists={recipes=self.rows},
    disabled={choose=#self.rows==0, preview=not self.selected, load_data=not self.selected, print_result=not self.printable,
      item_labels=self.selected ~= "print.item_labels"}}
end

function Browser:choose(values)
  local id = values.recipes and values.recipes.recipe_id
  if not id or not self.visible[id] then orion.fail(orion.tr("Select a recipe first.")) end
  local spec = assert(by_id[id])
  self.selected, self.printable = id,nil
  local names = {}
  for _, param in ipairs(spec.params) do names[#names+1] = param.name .. (param.optional and "?" or "") end
  self.values.query,self.values.tags = values.query,values.tags
  self.values.params, self.values.output, self.values.key = presets[id] or "{}", "", ""
  self.values.details = spec.description .. "\n" .. table.concat(names,", ")
end

function Browser:load_data(values)
  local id = self.selected
  if not id then orion.fail(orion.tr("Select a recipe first.")) end
  local file = ui.recipe_file()
  if not file then return end
  if not utf8.len(file) then orion.fail(orion.tr("Use a UTF-8 file. Legacy bank encodings need the command-line preview.")) end
  file = file:gsub("^\239\187\191","")
  local params
  if csv_recipes[id] and not file:match("^%s*{") then params = {csv=file}
  elseif id == "preview.bank_statement" and not file:match("^%s*{") then
    params = {account=values.key or "",statement=file}
  else
    local ok, decoded = pcall(Json.decode,file)
    if not ok or type(decoded) ~= "table" or getmetatable(decoded) then
      orion.fail(orion.tr("This recipe expects a JSON object."))
    end
    if order_recipes[id] and decoded.orders and getmetatable(decoded.orders) then params = {orders=decoded}
    else params = decoded end
  end
  local output = Json.encode(params)
  if #output > 1048576 then orion.fail(orion.tr("The recipe parameters exceed 1 MiB.")) end
  self.printable = nil
  self.values.params, self.values.output, self.values.key = output, "", values.key or ""
end

local function effective_params(id, values)
  local text = values.params or "{}"
  local key = (values.key or ""):match("^%s*(.-)%s*$")
  if #key == 0 or (id ~= "report.item_documents" and id ~= "preview.bank_statement") then return text end
  local ok, p = pcall(Json.decode,text)
  if not ok or type(p) ~= "table" or getmetatable(p) then
    orion.fail(orion.tr("This recipe expects a JSON object."))
  end
  if id == "report.item_documents" then p.item_id,p.sku = nil,key
  else p.account = key end
  return Json.encode(p)
end

function Browser:preview(values)
  local id = self.selected
  if not id then orion.fail(orion.tr("Select a recipe first.")) end
  self.printable = nil
  self.values.output = ""
  self.values.params, self.values.key = values.params or "{}", values.key or ""
  local output, printable = ui.recipe_preview(id, effective_params(id,values))
  if not output then orion.fail(tostring(printable):gsub("^.-:%d+: ", "")) end
  self.printable = printable and output or nil
  self.values.output = printable and orion.tr("Print preview ready. Save the printable result to open or print it.") or output
end

function Browser:print_result(values)
  if not self.printable or (values.params or "{}") ~= self.values.params or (values.key or "") ~= self.values.key then
    orion.fail(orion.tr("Preview the current parameters before saving."))
  end
  if ui.save_print_html(self.printable) then
    self.values.output = orion.tr("Saved. Open the file to view or print the result.")
  end
end

function Browser:item_labels()
  if self.selected ~= "print.item_labels" then return end
  orion.assert(Ledger.command("catalog.labels"))
  self.app.close(1)
end

return Browser
