local tr = require('messages')
-- A portable command engine. The host supplies data access and opening records.
local P = require('parser')
local E = {}; E.__index=E
function E.new(host) return setmetatable({host=host,context={},rows={}},E) end
local operations={gt=function(a,b)return a>b end,lt=function(a,b)return a<b end,
  ge=function(a,b)return a>=b end,le=function(a,b)return a<=b end,eq=function(a,b)return a==b end}

function E:query(ast)
  local rows={}
  for _, row in ipairs(self.host.rows(ast.entity)) do
    local match=true
    if ast.name and not P.normalize(row.name or row.number or ''):find(ast.name,1,true) then match=false end
    if ast.debtors and (row.debt or 0)<=0 then match=false end
    for _, f in ipairs(ast.filters) do
      local actual=row[f.field]
      if actual==nil or not operations[f.op](actual,f.value) then match=false end
    end
    if match then rows[#rows+1]=row end
  end
  local sort=ast.sort or {field='name',direction='asc'}
  table.sort(rows,function(a,b)
    local x,y=a[sort.field],b[sort.field]
    if x==y then return a.id<b.id end
    if x==nil then return false end
    if y==nil then return true end
    if sort.direction=='desc' then return x>y end
    return x<y
  end)
  return rows
end

function E:submit(text)
  local ast,message,pending=P.parse(text,self.context)
  if not ast and self.pending and self.pending.verb=='open' then
    ast,message=P.parse('открой '..text,self.context)
  end
  if not ast then
    self.pending=pending
    return {ok=false,message=message,animation='IDLEHEADSCRATCH',rows=self.rows}
  end
  if ast.verb=='help' then return {ok=true,animation='EXPLAIN',rows=self.rows,message=tr('Show debtors → over fifty thousand → sort by debt → open first. Also: show documents; only posted; find customer "Name".')} end
  if ast.verb=='reset' then
    self.context,self.rows,self.pending={},{},nil
    return {ok=true,message=tr('Context cleared.'),animation='GREETING',rows=self.rows}
  end
  if ast.verb=='open' then
    local entity=ast.entity or (self.context.query and self.context.query.entity)
    local matches=self.rows
    if ast.name then
      if not entity then return {ok=false,message=tr('Which object type should I open?'),animation='IDLEHEADSCRATCH',rows=self.rows} end
      matches=self:query{entity=entity,name=ast.name,filters={}}
      if #matches~=1 then
        self.pending={verb='open'}
        self.rows=matches; self.context.query={entity=entity,name=ast.name,filters={}}; self.context.selected=nil
        return {ok=false,message=string.format(tr('Found: %d. Specify a row number.'),#matches),animation='EXPLAIN',rows=matches}
      end
    elseif ast.entity and self.context.query and ast.entity~=self.context.query.entity then
      return {ok=false,message=tr('List objects of this type first.'),animation='IDLEHEADSCRATCH',rows=self.rows}
    end
    local ordinal=ast.ordinal or (ast.name and 1)
    local row=ordinal and matches[ordinal]
    if not row then return {ok=false,message=tr('That row is not in the current list.'),animation='IDLEHEADSCRATCH',rows=self.rows} end
    if not self.host.open then return {ok=false,message=tr('Opening records is unavailable in this interface.'),animation='ALERT',rows=self.rows} end
    local ok,opened,err=pcall(self.host.open,entity,row.id)
    if not ok or not opened then return {ok=false,message=err or tr('Could not open the record.'),animation='ALERT',rows=self.rows} end
    if ast.name then
      self.rows=matches; self.context.query={entity=entity,name=ast.name,filters={}}
    end
    self.context.selected=ordinal; self.pending=nil
    return {ok=true,message=string.format(tr('Opened record: %s'),row.name or row.number or row.id),animation='GESTURELEFT',rows=self.rows,opened={entity=entity,id=row.id}}
  end
  local ok,rows=pcall(self.query,self,ast)
  if not ok then return {ok=false,message=string.format(tr('Could not retrieve data: %s'),tostring(rows)),animation='ALERT',rows=self.rows} end
  self.rows=rows; self.context.query=P.copy(ast); self.context.selected=nil; self.pending=nil
  return {ok=true,message=string.format(tr('Found: %d. Amounts are in the accounting currency.'),#rows),animation=#rows>0 and 'EXPLAIN' or 'RESTPOSE',rows=rows,command=ast}
end
return E
