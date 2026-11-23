local M = dofile("apps/ledger/share/addons/labels/label_sheet.lua")
local function bad(f, message)
  local ok, err = pcall(f)
  assert(not ok and tostring(err):find(message, 1, true), tostring(err))
end
-- Code 128 B/C and weighted modulo-103 checksums.
assert(table.concat(M.encode("AB"), ",") == "104,33,34,102,106")
assert(table.concat(M.encode("000123"), ",") == "105,0,1,23,73,106")
bad(function() M.encode("Номер") end, "printable ASCII")
bad(function() M.encode("") end, "printable ASCII")
bad(function() M.encode("A\n") end, "printable ASCII")
bad(function() M.symbol(string.rep("Z",64),34) end, "too wide")

local rows = {{name='Bolt <img src="remote"> & nut',sku="000123",copies=24},
  {name="Резистор",sku="R-8",barcode="PK-001",copies=1}}
local html, labels, pages = M.render(rows)
assert(labels == 25 and pages == 2)
local _, sections = html:gsub('<section class="label">', "")
assert(sections == 25)
assert(html:find('@page{size:210.000mm 297.000mm;margin:0}',1,true))
assert(html:find('Bolt &lt;img src=&quot;remote&quot;&gt; &amp; nut',1,true))
assert(not html:find("<img",1,true))
assert(html:find('aria-label="PK-001"',1,true))
assert(html:find('000123',1,true))
assert(html:find('x="10" y="0" width="2"',1,true), "quiet zone is ten modules")
local roll, count, rolls = M.render({{name="Box",sku="AB",copies=2}},
  {format="single",stock_width_mm=62,stock_height_mm=32})
assert(count == 2 and rolls == 2 and roll:find('@page{size:62.000mm 32.000mm;margin:0}',1,true))
assert(roll:find('padding:0.000mm 0 0 0.000mm',1,true))
assert(M.render({{name="Box",sku="AB",copies=1}},{format="letter"}):find('215.900mm 279.400mm',1,true))
bad(function() M.render({{name="X",sku="A",copies=1.5}}) end,"whole copy counts")
bad(function() M.render({{name="X",sku="A",copies=501}}) end,"whole copy counts")
bad(function() M.render({{name="X",sku="",copies=1}}) end,"Invalid SKU")
bad(function() M.render(rows,{format="single",stock_width_mm=20,stock_height_mm=20}) end,"Roll size")
bad(function() M.render(rows,{format="invalid"}) end,"Choose A4")
print("Label sheets: Code 128 B/C, checksums, quiet zone, print sizes, pagination, escaping and quantity limits passed")

-- Exercise the add-on entry hooks as well as the pure renderer. The host
-- supplies the selected item and the file picker; a refused save stays open.
package.loaded.label_sheet = M
package.loaded.orion = {tr = function(message) return message end, fail = function(message) error(message,0) end}
package.loaded["orion.model"] = {extend = function(_, name)
  assert(name == "ib.items")
  return {find = function(_, id)
    if id == 15 then return {id=15,name="Nut",sku="0015"} end
    if id == 16 then return {id=16,name="Bolt",sku="B16"} end
  end}
end}
local wrote, accept = nil, false
package.loaded["ledger.ui"] = {
  selected = function() return 15 end,
  save_print_html = function(output)
    if not accept then return false end
    wrote = output
    return true
  end,
}
local dialog = dofile("apps/ledger/share/addons/labels/labels.lua")
dialog:init()
assert(dialog:data().values.item == 15)
dialog:save(dialog:data().values)
assert(not wrote)
accept = true
dialog:save(dialog:data().values)
assert(wrote:find('aria-label="0015"',1,true))
dialog:add({item=15,copies=2,barcode="",width=62,height=32,roll=true})
dialog:add({item=16,copies=3,barcode="LAB16",width=62,height=32,roll=true})
dialog:save(dialog:data().values)
local _, total = wrote:gsub('<section class="label">', "")
assert(total == 5 and wrote:find('aria-label="LAB16"',1,true))
bad(function() dialog:add({item=16,copies=0,barcode=""}) end, "whole copy")
assert(#dialog:data().lists.queue == 2)
dialog:clear(dialog:data().values)
assert(#dialog:data().lists.queue == 0)
