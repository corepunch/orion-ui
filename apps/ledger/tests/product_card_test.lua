local card = dofile("apps/ledger/share/addons/product_card/product_card.lua")
local function reject(fn, want)
  local ok, err = pcall(fn)
  assert(not ok and tostring(err):find(want,1,true), tostring(err))
end
local item = {id=10, name=[[Box <small> & "blue" * 'oak']], sku="0005"}
local snapshot = {ok=true, result={{id=12,sku="0005",name="Other box"}, item}}
assert(card.select_item(snapshot,10) == item)
reject(function() card.select_item(snapshot,15) end, "not in catalog")
reject(function() card.select_item({ok=true,result={item,item}},10) end, "duplicate item")
reject(function() card.select_item({ok=false,result={item}},10) end, "successful catalogs.list")
local details = {language="ru", size="4×6 cm", variant="<script>x</script>",
  stock={quantity="-14.005", unit="шт.", source="UNF <Main>",as_of="2024-02-29"}}
-- A small structurally plausible header and body; production photo rendering is exercised separately.
local png = "\137PNG\r\n\26\n\0\0\0\13IHDR" .. string.pack(">I4I4",1,1) .. string.rep("\0",21)
local html = card.render(item, details, png)
for _, s in ipairs({"Карточка товара","4×6 cm","0005","&lt;script&gt;","-14.005 шт.", "UNF &lt;Main&gt;",
  "&amp; &quot;blue&quot; * &#39;oak&#39;", 'src="data:image/png;base64,iVBORw0KGgo', "2024-02-29", "@page"}) do
  assert(html:find(s,1,true), s)
end
assert(not html:find("<script>",1,true))
assert(not card.render(item):find("Quantity snapshot",1,true))
reject(function() card.render(item,{photo="../private.jpg"}) end, "unsupported")
reject(function() card.render(item,{language="fr"}) end, "language")
reject(function() card.render(item,{},string.rep("x",96)) end, "unsupported photo")
reject(function()
  card.render(item,{},png:sub(1,16) .. string.pack(">I4I4",4096,4096) .. png:sub(25))
end, "unsupported photo")
reject(function() card.render(item,{},string.rep("x",card.MAX_PHOTO + 1)) end, "under 2 MiB")
details.stock.as_of = "2025-02-29"
reject(function() card.render(item,details) end, "invalid quantity date")
details.stock.quantity = 10.125
reject(function() card.render(item,details) end, "invalid quantity")
print("product card tests passed")
