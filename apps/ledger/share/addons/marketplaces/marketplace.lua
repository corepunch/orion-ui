local orion = require("orion")
local ui = require("ledger.ui")
local json = require("orion.json")
local tr = orion.tr
local Market = {form="marketplace",title=tr("Marketplaces")}

local function trimmed(s)
  return type(s)=="string" and s:match("^%s*(.-)%s*$") or ""
end

local function clear_result(self)
  self.status,self.request,self.previewed,self.eligible = "idle",nil,false,0
  self.pick_available,self.details = false,""
  self.values.order_summary,self.values.stock_summary,self.values.results = "","",""
  self.values.status = self.supported and tr("Orders and availability are read-only.")
    or tr("Marketplace connections are unavailable in this build.")
end

function Market:init()
  local start, finish
  if ui.market_dates then start,finish=ui.market_dates() end
  self.provider,self.environment = "ozon","production"
  self.supported = not ui.market_supported or ui.market_supported()
  self.values = {account="",client_id="",from=start or "",until_date=finish or "",orders=true,stock=true}
  clear_result(self)
end

function Market:data()
  local busy = self.status=="loading"
  self.values.connection = self.environment=="sandbox" and tr("eBay Sandbox") or tr("Production")
  self.values.secret_label = self.provider=="ozon" and tr("Ozon API key:") or tr("eBay access token:")
  self.values.credential_notice = self.provider=="ozon" and tr("Enter a read-only Seller API key. Ledger does not save it.")
    or tr("Advanced: paste a seller OAuth user access token. Ledger does not save it.")
  return {values=self.values,disabled={
    ozon=busy or self.provider=="ozon",ebay=busy or self.provider=="ebay",
    production=busy or self.environment=="production",sandbox=busy or self.provider~="ebay" or self.environment=="sandbox",
    account=busy,client_id=busy or not self.supported or self.provider~="ozon",secret=busy or not self.supported,orders=busy,stock=busy,from=busy,until_date=busy,
    fetch=busy or not self.supported,stop=not busy or not ui.market_cancel,paste=busy or not self.supported or not ui.market_paste,
    preview=busy or not self.pick_available,save=busy or not self.previewed,
    review=busy or not self.previewed,
  }}
end

local function remember(self, v)
  self.values.account,self.values.client_id = trimmed(v.account),trimmed(v.client_id)
  self.values.from,self.values.until_date = trimmed(v.from),trimmed(v.until_date)
  self.values.orders,self.values.stock = v.orders==true,v.stock==true
end

local function choose(self,v,provider,environment)
  if self.status=="loading" then return end
  remember(self,v)
  if not ui.market_clear_secret then self.values.secret = "" else ui.market_clear_secret() end
  self.provider,self.environment = provider,environment
  clear_result(self)
end

function Market:choose_ozon(v) choose(self,v,"ozon","production") end
function Market:choose_ebay(v) choose(self,v,"ebay","production") end
function Market:choose_production(v) choose(self,v,self.provider,"production") end
function Market:choose_sandbox(v)
  if self.provider=="ebay" then choose(self,v,"ebay","sandbox") end
end

function Market:paste(v)
  if self.status=="loading" or not ui.market_paste then return end
  remember(self,v)
  self.values.secret=nil
  if not ui.market_paste() then return nil,tr("Clipboard paste is unavailable. Enter the credential in the masked field.") end
end

local function stamp(value)
  return value:match("^%d%d%d%d%-%d%d%-%d%dT%d%d:%d%d:%d%dZ$")
end

local function request_for(self,v)
  local account=trimmed(v.account)
  if #account>24 or not account:match("^[A-Za-z0-9_.-]+$") then
    return nil,tr("Enter a short account label using only A-Z, 0-9, dots, dashes or underscores.")
  end
  local client_id=trimmed(v.client_id)
  if self.provider=="ozon" and (#client_id>24 or not client_id:match("^[0-9]+$")) then
    return nil,tr("Enter the numeric Ozon Client ID.")
  end
  local order,stock=v.orders==true,v.stock==true
  if not order and not stock then return nil,tr("Select Orders or Availability.") end
  local from,until_date=trimmed(v.from),trimmed(v.until_date)
  local options={account=account,environment=self.environment,
    mode=order and (stock and "both" or "orders") or "availability"}
  if order and (self.provider=="ozon" or from~="" or until_date~="") then
    if not stamp(from) or not stamp(until_date) or from>=until_date then
      return nil,tr("Enter both UTC dates in increasing order: YYYY-MM-DDTHH:MM:SSZ.")
    end
    options.from,options.until_date=from,until_date
  end
  return {options=options,provider=self.provider,client_id=self.provider=="ozon" and client_id or ""}
end

local function same_request(a,b)
  if not a or not b or a.provider~=b.provider or a.client_id~=b.client_id then return false end
  for _,key in ipairs({"account","environment","mode","from","until_date"}) do
    if a.options[key]~=b.options[key] then return false end
  end
  return true
end

function Market:fetch(v)
  if self.status=="loading" or not self.supported then return end
  self.values.secret=nil
  remember(self,v)
  clear_result(self)
  local request,message=request_for(self,v)
  if not request then return nil,message end
  self.request=request
  local ok,why=ui.market_start(request.provider,json.encode(request.options),request.client_id)
  if not ok then self.request=nil; return nil,why or tr("Cannot start the request.") end
  self.status,self.values.status="loading",tr("Loading marketplace data...")
end

local function count(value)
  return type(value)=="table" and #value or type(value)=="number" and value or 0
end

local function show_details(data)
  local lines={}
  local function add(s) lines[#lines+1]=s end
  local function render(section,title,fn)
    if type(section)~="table" or #section==0 then return end
    if #lines>0 then add("") end
    add(tr(title))
    for i=1,math.min(#section,20) do add(fn(section[i])) end
    if #section>20 then add(tr("Showing the first 20 rows.")) end
  end
  if type(data.details)=="string" then return data.details end
  local held={}
  if type(data.orders)=="table" then
    for _, order in ipairs(data.orders) do
      if type(order)=="table" and not order.eligible then held[#held+1]=order end
    end
  end
  render(held,"Held orders",function(row)
    return string.format("%s: %s",tostring(row.id or "?"),tostring(row.hold_reason or row.status or "?"))
  end)
  render(data.availability,"Marketplace availability (not Ledger stock)",function(row)
    if type(row)~="table" then return tostring(row) end
    local quantity={}
    if row.available~=nil then quantity[#quantity+1]=string.format(tr("available: %s"),tostring(row.available)) end
    if row.reserved~=nil then quantity[#quantity+1]=string.format(tr("reserved: %s"),tostring(row.reserved)) end
    if row.listed_total~=nil then quantity[#quantity+1]=string.format(tr("listed: %s"),tostring(row.listed_total)) end
    return string.format("%s [%s]: %s",tostring(row.sku or row.source_item_id or "?"),
      tostring(row.source_location or row.location_kind or "?"),table.concat(quantity,", "))
  end)
  render(data.warnings,"Warnings",tostring)
  return table.concat(lines,"\n")
end

function Market:market_refresh()
  if self.status~="loading" then return end
  local status,value=ui.market_status()
  if status=="loading" then return end
  if status=="error" then
    self.status,self.values.status="error",tostring(value or tr("The request failed."))
  elseif status=="ready" then
    local data=value
    if type(value)=="string" then
      local ok,decoded=pcall(json.decode,value)
      if ok then data=decoded end
    end
    if type(data)~="table" then
      self.status,self.values.status="error",tr("The response could not be displayed.")
      return
    end
    self.status,self.values.status="ready",tr("Data loaded. Nothing was changed.")
    local eligible=count(data.eligible or data.eligible_orders)
    if type(data.orders)=="table" and not data.eligible and not data.eligible_orders then
      for _,order in ipairs(data.orders) do if order.eligible then eligible=eligible+1 end end
    end
    self.eligible=eligible
    self.pick_available=data.pick_available~=false and eligible>0
    if self.request.options.mode~="availability" then
      self.values.order_summary=string.format(tr("Orders: %d total, %d ready to pick, %d held."),
        count(data.orders or data.order_count),eligible,count(data.held or data.held_orders or (count(data.orders or data.order_count)-eligible)))
    end
    if self.request.options.mode~="orders" then
      self.values.stock_summary=string.format(tr("Marketplace availability: %d rows. Not Ledger stock."),
        count(data.availability or data.stock_count))
    end
    self.details=show_details(data)
    self.values.results=self.details
  else
    clear_result(self)
  end
end

function Market:stop()
  if self.status=="loading" and ui.market_cancel then
    ui.market_cancel()
    clear_result(self)
    self.values.status=tr("Request cancelled.")
  end
end

local function current(self,v)
  if self.status=="ready" and same_request(self.request,request_for(self,v)) then return true end
  remember(self,v)
  clear_result(self)
  return nil,tr("Inputs changed. Load marketplace data again.")
end

function Market:preview(v)
  local ok,message=current(self,v)
  if not ok then return nil,message end
  self.previewed=false
  local result,why=ui.market_preview()
  if type(result)~="string" or result=="" then return nil,why or tr("There are no orders ready to pick.") end
  self.values.results=result
  self.values.status=tr("Pick sheet preview. Save it when ready.")
  self.previewed=true
end

function Market:review(v)
  local ok,message=current(self,v)
  if not ok then return nil,message end
  self.previewed=false
  self.values.results=self.details
  self.values.status=tr("Data loaded. Nothing was changed.")
end

function Market:save(v)
  local ok,message=current(self,v)
  if not ok then return nil,message end
  if not self.previewed then return nil,tr("Preview the pick sheet before saving.") end
  local saved,why=ui.market_save()
  if saved then self.values.status=tr("Pick sheet saved.")
  elseif why then return nil,why end
end

return Market
