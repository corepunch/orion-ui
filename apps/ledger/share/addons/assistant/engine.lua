local tr = require('messages')
local P = require('parser')
local E = {}; E.__index=E
function E.new(host) return setmetatable({host=host,context={},rows={}},E) end
local operations={gt=function(a,b)return a>b end,lt=function(a,b)return a<b end,
  ge=function(a,b)return a>=b end,le=function(a,b)return a<=b end,eq=function(a,b)return a==b end}

function E:query(ast)
  local rows={}
  for _,row in ipairs(self.host.rows(ast.entity)) do
    local match=not ast.party_id or row.counterparty_id==ast.party_id
    if ast.name and not P.normalize(row.name or row.number or ''):find(ast.name,1,true) then match=false end
    if ast.debtors and (row.debt or 0)<=0 then match=false end
    for _,f in ipairs(ast.filters or {}) do
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

function E:result(ok,message,extra)
  local out=extra or {}
  out.ok,out.message,out.rows=ok,message,out.rows or self.rows
  out.animation=out.animation or (ok and 'EXPLAIN' or 'IDLEHEADSCRATCH')
  return out
end

-- THIS-IS-IT: identity survives sorting and changes of result scope.
function E:this_is_it(entity,row)
  self.context.it={entity=entity,id=row.id}
end
function E:live(entity,id)
  for _,row in ipairs(self.host.rows(entity)) do if row.id==id then return row end end
end

-- Resolve only typed, accessible candidates. Never choose the first of many.
function E:resolve(ref,expected,scope,query)
  local focus=self.context.it
  local function in_scope(entity,id)
    if not scope or not scope.bounded then return true end
    if scope.entity~=entity then return false end
    for _,row in ipairs(scope.rows) do if row.id==id then return true end end
    return false
  end
  local entity=ref.entity or expected or (scope and scope.entity) or (focus and focus.entity)
  if expected and entity~=expected then return nil,tr('That object type does not fit this request.') end
  if ref.pronoun then
    if not focus then return nil,tr('There is no object for this or it yet.') end
    if (ref.entity and focus.entity~=entity) or (expected and focus.entity~=expected) then
      return nil,tr('That object type does not fit this request.')
    end
    if not in_scope(focus.entity,focus.id) then return nil,tr('No matching records were found.') end
    local row=self:live(focus.entity,focus.id)
    if row and query and query.party_id and row.counterparty_id~=query.party_id then
      return nil,tr('No matching records were found.')
    end
    if not row then return nil,tr('The referenced record is no longer available.') end
    return {entity=focus.entity,row=row}
  end
  if not entity then return nil,tr('Which object should I open? Try: open first.'),{} end
  if ref.ordinal then
    if not scope or scope.entity~=entity then return nil,tr('List objects of this type first.') end
    local row=scope.rows[ref.ordinal]
    if not row then return nil,tr('That row is not in the current list.') end
    row=self:live(entity,row.id)
    if row and query and query.party_id and row.counterparty_id~=query.party_id then
      return nil,tr('The referenced record is no longer available.')
    end
    if not row then return nil,tr('The referenced record is no longer available.') end
    return {entity=entity,row=row}
  end
  local candidates
  if ref.name then
    candidates=self:query{entity=entity,name=ref.name,party_id=query and query.party_id}
    if scope and scope.bounded and scope.entity==entity then
      local allowed,filtered={},{}
      for _,row in ipairs(scope.rows) do allowed[row.id]=true end
      for _,row in ipairs(candidates) do if allowed[row.id] then filtered[#filtered+1]=row end end
      candidates=filtered
    end
  elseif query and query.party_id then
    candidates=self:query(query)
  else
    if focus and focus.entity==entity and in_scope(entity,focus.id) then
      local row=self:live(entity,focus.id)
      if not row then return nil,tr('The referenced record is no longer available.') end
      return {entity=entity,row=row,inferred=true}
    end
    candidates=scope and scope.entity==entity and scope.rows or self:query{entity=entity}
  end
  if #candidates==1 then
    local row=self:live(entity,candidates[1].id)
    if not row then return nil,tr('The referenced record is no longer available.') end
    if query and query.party_id and row.counterparty_id~=query.party_id then
      return nil,tr('No matching records were found.')
    end
    return {entity=entity,row=row,inferred=not ref.name}
  end
  if #candidates==0 then return nil,tr('No matching records were found.') end
  return nil,tr('Several records match. Which one? Use a row number or a quoted name.'),{entity=entity,rows=candidates}
end

function E:clarify(ast,slot,message,candidates)
  local scope=candidates.entity and candidates or self.scope
  self.pending={command=P.copy(ast),slot=slot,scope=scope and {entity=scope.entity,rows=scope.rows,bounded=true}}
  if candidates.entity then self.scope,self.rows=scope,scope.rows end
  if slot=='for_party' then message=tr('Which customer are these documents for? Use a row number or a quoted name.') end
  return self:result(false,message,{clarification=slot})
end

function E:execute(ast,answer_scope)
  local party
  local mentioned_party=ast.for_party~=nil
  if ast.for_party then
    local message,candidates
    party,message,candidates=self:resolve(ast.for_party,'counterparties',answer_scope or self.scope)
    if not party then
      if candidates then return self:clarify(ast,'for_party',message,candidates) end
      return self:result(false,message)
    end
    ast.party_id=party.row.id; ast.for_party=nil
  elseif ast.party_id then
    local row=self:live('counterparties',ast.party_id)
    if not row then return self:result(false,tr('The referenced record is no longer available.')) end
    party={entity='counterparties',row=row}
  end
  if ast.verb=='open' then
    local object,message,candidates=self:resolve(ast,nil,answer_scope or self.scope,ast)
    if not object then
      if candidates then return self:clarify(ast,'object',message,candidates) end
      return self:result(false,message)
    end
    if not self.host.open then return self:result(false,tr('Opening records is unavailable in this interface.'),{animation='ALERT'}) end
    local ok,opened,err=pcall(self.host.open,object.entity,object.row.id)
    if not ok or not opened then return self:result(false,err or tr('Could not open the record.'),{animation='ALERT'}) end
    self:this_is_it(object.entity,object.row)
    self.pending=nil
    local template=object.inferred and tr('Opened record: %s (inferred from context).') or tr('Opened record: %s')
    return self:result(true,string.format(template,object.row.name or object.row.number or object.row.id),
      {opened={entity=object.entity,id=object.row.id},inferred=object.inferred,animation='GESTURELEFT'})
  end
  local rows=self:query(ast)
  self.rows,self.scope=rows,{entity=ast.entity,rows=rows}
  self.context.query=P.copy(ast); self.pending=nil
  if #rows==1 then self:this_is_it(ast.entity,rows[1])
  elseif party and mentioned_party then self:this_is_it(party.entity,party.row) end
  local message=party and string.format(tr('Found: %d. Customer: %s. Amounts are in the accounting currency.'),#rows,party.row.name)
    or string.format(tr('Found: %d. Amounts are in the accounting currency.'),#rows)
  return self:result(true,message,{command=ast,inferred=party and party.inferred,animation=#rows>0 and 'EXPLAIN' or 'RESTPOSE'})
end

function E:submit(text)
  local ast,message
  local answer_scope
  if self.pending then
    local ref=P.reference(text)
    if ref then
      ast=P.copy(self.pending.command); answer_scope=self.pending.scope
      if self.pending.slot=='for_party' then ast.for_party=ref
      else
        ast.entity=ref.entity or ast.entity; ast.name,ast.ordinal,ast.pronoun=ref.name,ref.ordinal,ref.pronoun
      end
    end
  end
  if not ast then
    ast,message=P.parse(text,self.context)
    if self.pending and not P.has_verb(text) then
      return self:result(false,message or tr('Answer the pending question or enter a new command.'))
    end
  end
  if not ast then return self:result(false,message) end
  if ast.verb=='help' then return self:result(true,tr('Try: show debtors; find customer "Name" → show documents for it → only posted → open first. Missing customer: show documents for. Cancel a question: cancel.')) end
  if ast.verb=='reset' then
    self.context,self.rows,self.pending,self.scope={},{},nil,nil
    return self:result(true,tr('Context cleared.'),{animation='GREETING'})
  end
  if ast.verb=='cancel' then
    self.pending=nil
    return self:result(true,tr('Pending question cancelled.'))
  end
  local ok,result=pcall(self.execute,self,ast,answer_scope)
  if not ok then return self:result(false,string.format(tr('Could not retrieve data: %s'),tostring(result)),{animation='ALERT'}) end
  return result
end
return E
