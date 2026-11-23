local directory = "apps/ledger/share/stdplugs/automation/"
local json = dofile("share/lua/orion/json.lua")
local sample = {id=12,name="Nut & bolt",sku="00012"}
local writes = 0
_G.ledger = {
  find=function(d,t,i) assert(d=="ib" and t=="items"); return i==12 and sample or nil end,
  fetch=function(d,t) assert(d=="ib" and t=="items"); return {sample} end,
  transaction=function() writes=writes+1; error("write attempted") end,
}
local A=dofile(directory.."init.lua")
local function reject(fn, detail)
  local ok,err=pcall(fn)
  assert(not ok and tostring(err):find(detail,1,true),tostring(err))
end
assert(#A.recipes==18 and #A.nodes==16)
local found=A.search("ПЕЧАТЬ",{" СКЛАД "," Этикетки "},"recipe")
assert(#found==2 and found[1].id=="print.item_labels" and found[2].id=="print.order_labels")
assert(#A.search("",{"склад","несуществующий"},"recipe")==0)
assert(#A.search("report.item_documents",nil,"recipe")==1)
assert(#A.search("печать",nil,"node")==0)
local html,is_html=A.browser_preview("print.item_labels",'{"item_id":12,"copies":2}')
assert(is_html and html:find('aria-label="00012"',1,true))
local _, count=html:gsub('<section class="label">',"")
assert(count==2)
local report,not_html=A.browser_preview("check.required",'{"table":"items","fields":["price"]}')
assert(not not_html and report:find("price",1,true))
reject(function() A.browser_preview("arbitrary.code","{}") end,"unsupported")
reject(function() A.browser_preview("print.item_labels",'{"item_id":12,"path":"/etc/passwd"}') end,"unknown parameter")
reject(function() A.browser_preview("print.item_labels",'return os.execute("test")') end,"invalid JSON")
local file=assert(io.open("apps/ledger/tests/fixtures/fulfillment/orders.json","rb"))
local input=file:read("*a");file:close()
local orders,is_order_html=A.browser_preview("report.pick_orders",json.encode({orders=json.decode(input)}))
assert(is_order_html and orders:find("Batch pick list",1,true))
assert(writes==0 and sample.name=="Nut & bolt")

package.loaded.search=dofile(directory.."search.lua")
package.loaded.catalog=dofile(directory.."catalog.lua")
package.loaded["orion.json"]=json
package.loaded.orion={tr=function(x)return x end,fail=function(x)error(x,0)end,assert=assert}
local invoked,output_file
package.loaded.ledger={command=function(id) assert(id=="catalog.labels"); invoked=id; return true end}
package.loaded["ledger.ui"]={
  recipe_preview=function(id,params)
    local ok,result,media=pcall(A.browser_preview,id,params)
    if ok then return result,media else return nil,result end
  end,
  recipe_file=function()return input end,
  save_print_html=function(value) output_file=value;return true end,
}
local browser=dofile(directory.."browser.lua")
local closed
browser.app={close=function()closed=true end}
browser:init()
assert(#browser:data().lists.recipes==18)
browser:search({query="ПЕЧАТЬ",tags=" СКЛАД, ЭТИКЕТКИ"})
assert(#browser:data().lists.recipes==2)
browser:choose({recipes={recipe_id="print.item_labels"}})
browser:item_labels()
assert(closed and invoked=="catalog.labels")
browser:preview({params='{"item_id":12}',key=""})
assert(browser.printable)
reject(function() browser:print_result({params='{"item_id":12,"copies":4}',key=""}) end,"Preview the current")
assert(not output_file)
browser:print_result({params='{"item_id":12}',key=""})
assert(output_file:find("00012",1,true))
reject(function() browser:preview({params="broken",key=""}) end,"invalid JSON")
assert(not browser.printable)
browser:search({query="сборка",tags="склад, заказы"})
browser:choose({recipes={recipe_id="report.pick_orders"}})
browser:load_data({key=""})
browser:preview({params=browser.values.params,key=""})
assert(browser.printable:find("Batch pick list",1,true))
print("Recipe browser: normalized tags, real previews, typed files and stale-save protection passed")
