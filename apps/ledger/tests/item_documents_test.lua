local trace = dofile("apps/ledger/share/stdplugs/automation/document_lines.lua")
local function bad(p, text)
  local ok, err = pcall(trace, p)
  assert(not ok and tostring(err):find(text, 1, true), tostring(err))
end
local db = {
  items={{id=7,sku="0007",name="Blue Box"}, {id=11,sku="011",name="Wrap"},
    {id=13,sku="DUP",name="One"}, {id=17,sku="DUP",name="Two"}},
  documents={
    {id=40,date="2026-11-23",number="DRAFT",posted=false},
    {id=3,date="2026-11-21",number="RECEIPT",posted=true, exact_money=true},
    {id=4,date="2026-11-21",number="SALE",posted=true},
    {id=2,date="2026-11-20",number="NONMATCH",posted=true}},
  doc_lines={
    {id=500,document_id=40,item_id=7,quantity=2.25},
    {id=310,document_id=4,item_id=7},
    {id=308,document_id=4,item_id=7},
    {id=123,document_id=2,item_id=11},
    {id=225,document_id=3,item_id=7,quantity_milli=1505}},
}
for i=1,19 do
  table.insert(db.doc_lines,{id=205+i,document_id=3,item_id=11,quantity_milli=i*1000})
end
local function clone(row)
  local result = {}; for k,v in pairs(row) do result[k]=v end
  return result
end
ledger = {
  fetch = function(db_name, table_name, field, value)
    assert(db_name=="ib")
    local out={}
    for _, row in ipairs(db[table_name]) do
      if not field or row[field]==value then table.insert(out, clone(row)) end
    end
    return out
  end,
  find = function(_, table_name, id)
    for _, row in ipairs(db[table_name]) do if row.id==id then return clone(row) end end
  end,
}
local rows=trace({sku="0007"})
assert(#rows==3 and rows[1].document.id==3 and rows[2].document.id==4 and rows[3].document.id==40)
assert(#rows[1].lines==20 and rows[1].matching_lines==1 and rows[2].matching_lines==2)
assert(rows[1].lines[1].item_name=="Wrap" and not rows[1].lines[1].matches_item)
assert(rows[1].lines[20].matches_item and rows[1].lines[20].item_sku=="0007")
assert(rows[1].lines[20].quantity_milli==1505)
assert(db.doc_lines[5].matches_item==nil, "fixture records must remain unchanged")
assert(#trace({item_id=7,posted_only=true})==2)
assert(#trace({item_id=7,date_from="2026-11-23",date_to="2026-11-23"})==1)
assert(#trace({item_id=7,date_from="2026-11-23",posted_only=true})==0)
assert(#trace({item_id=13})==0)
bad({}, "either item_id or sku")
bad({item_id=7,sku="0007"}, "either item_id or sku")
bad({item_id=1.25}, "invalid item ID")
bad({item_id=88}, "unknown item")
bad({sku="DUP"}, "ambiguous SKU")
bad({item_id=7,date_from="2025-02-29"}, "invalid date_from")
bad({item_id=7,date_from="2026-11-22",date_to="2026-11-21"}, "must not follow")
-- Exercise the ready-to-run script, including full-line output and exact decimals.
ledger.automation = {preview = function(id, params)
  assert(id == "report.item_documents")
  return trace(params)
end}
local env = setmetatable({
  os={getenv=function(key) if key == "LEDGER_SKU" then return "0007" end end},
  print=function(value) rows[#rows + 1] = value end,
}, {__index=_ENV})
rows = {}
assert(loadfile("apps/ledger/share/stdplugs/automation/examples/item-documents.lua", "t", env))()
assert(#rows == 24, "header and all 23 product lines are exported")
assert(rows[2]:find("011\tWrap\t1.000\t", 1, true))
assert(rows[21]:find("0007\tBlue Box\t1.505\tyes", 1, true))
assert(rows[24]:find("2.25\tyes", 1, true))
print("item documents tests passed")
