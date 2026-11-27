-- Operator-owned, local marketplace picking. No provider writes or ledger postings.
local M = {}
local schema = "orion-market-picks-v1"
local function text(value, max)
  assert(type(value)=="string" and #value<=max and utf8.len(value) and value:find("%S")
    and not value:find("[%z\1-\31\127]") and value==value:match("^%s*(.-)%s*$"),"invalid marketplace text value")
  return value
end
local function integer(value, min, max)
  assert(math.type(value)=="integer" and value>=min and value<=max,"invalid marketplace integer")
  return value
end
local function collection(rows,max)
  assert(type(rows)=="table" and #rows<=max,"invalid marketplace collection")
  local count=0
  for k in pairs(rows) do integer(k,1,#rows); count=count+1 end
  assert(count==#rows,"sparse marketplace collection")
  return rows
end
local function optional(value, max) return value~=nil and text(value,max) or nil end
local function sorted_keys(map)
  local keys={}
  for key in pairs(map) do keys[#keys+1]=key end
  table.sort(keys)
  return keys
end
local function scope(snapshot)
  assert(snapshot.provider=="ozon" or snapshot.provider=="ebay","unknown marketplace")
  assert(snapshot.environment=="production" or snapshot.provider=="ebay" and snapshot.environment=="sandbox",
    "unknown marketplace environment")
  assert(text(snapshot.account,24):match("^[A-Za-z0-9_.-]+$"),"invalid marketplace account")
  return snapshot.provider.."/"..snapshot.environment.."/"..snapshot.account
end
local function source_key(sku,variant) return sku.."\0"..(variant or "") end
local function source_order(src,json)
  assert(type(src)=="table" and type(src.eligible)=="boolean","invalid marketplace order")
  local out={id=text(src.id,64),status=text(src.status,64),eligible=src.eligible,
    hold_reason=optional(src.hold_reason,160),source_order_id=optional(src.source_order_id,128),
    source_order_number=optional(src.source_order_number,100),fulfillment=optional(src.fulfillment,64),
    lines=json.array()}
  local seen={}
  for _, item in ipairs(collection(src.lines,4000)) do
    assert(type(item)=="table","invalid marketplace line")
    local line={id=text(item.id,128),sku=optional(item.sku,100),variant=optional(item.variant,512),
      source_item_id=optional(item.source_item_id,128),source_variant_id=optional(item.source_variant_id,128),
      barcode=optional(item.barcode,100),quantity=item.quantity~=nil and integer(item.quantity,1,1000000) or nil}
    assert(not seen[line.id],"duplicate marketplace line")
    assert(not out.eligible or line.sku and line.quantity,"incomplete eligible marketplace line")
    seen[line.id]=true
    out.lines[#out.lines+1]=line
  end
  assert(not out.eligible or #out.lines>0 and not out.hold_reason
    and not ({cancelled=true,canceled=true})[out.status:lower()],"invalid eligible marketplace order")
  table.sort(out.lines,function(a,b) return a.id<b.id end)
  return out
end
local function is_cancelled(source)
  local status=source.status:lower()
  return status=="cancelled" or status=="canceled"
end
function M.new()
  return {schema=schema,orders={},mappings={}}
end
function M.open(value)
  assert(type(value)=="table" and value.schema==schema and type(value.orders)=="table"
    and type(value.mappings)=="table","unknown marketplace workspace format")
  return value
end

-- Build changes separately so a bad row never leaves a partly-applied import.
-- "observed" is capture start time, not a provider's revision or date filter.
function M.sync(state,snapshot,observed,json)
  assert(type(snapshot)=="table" and snapshot.schema=="orion-marketplace-v1","unknown marketplace snapshot format")
  assert(snapshot.mode=="orders" or snapshot.mode=="both","snapshot does not contain orders")
  local identity=scope(snapshot)
  assert(not state.scope or state.scope==identity,"snapshot belongs to a different seller or environment")
  observed=integer(observed or snapshot.observed_at,1,math.maxinteger)
  local changes,seen,counts={},{},{inserted=0,updated=0,unchanged=0,stale=0}
  for _, raw in ipairs(collection(snapshot.orders,10000)) do
    local src=source_order(raw,json)
    local id=src.id
    assert(not seen[id],"duplicate marketplace order")
    seen[id]=true
    local row=state.orders[id]
    if row and observed<row.observed then
      counts.stale=counts.stale+1
    elseif row and json.encode(src)==json.encode(row.source) then
      counts.unchanged=counts.unchanged+1
      if observed>row.observed then
        local copy={}
        for k,v in pairs(row) do copy[k]=v end
        copy.observed=observed
        changes[id]=copy
      end
    else
      assert(not row or observed>row.observed,"conflicting observations at the same timestamp")
      assert(not row or not row.cancelled or not src.eligible,
        "cancelled order cannot be reopened by snapshot")
      local copy={source=src,revision=row and row.revision+1 or 1,observed=observed,
        stage=row and row.stage or "open",picked=row and row.picked or {},
        scans=row and row.scans or {},labels=row and row.labels or {},
        previous_picks=row and row.previous_picks or nil, archived_labels=row and row.archived_labels or nil,
        cancelled=is_cancelled(src) or row and row.cancelled or false,
        pick_epoch=row and row.pick_epoch or 1}
      if row and (row.review or next(row.picked) or next(row.labels) or row.stage~="open") then
        -- Never interpret a changed upstream line as the same physical item;
        -- operator must review the discrepancy before restarting the pick.
        copy.review=true
        if not row.review then
          copy.previous_picks={}
          for _, line in ipairs(row.source.lines) do
            if row.picked[line.id] then
              copy.previous_picks[line.id]={sku=line.sku,variant=line.variant,picked=row.picked[line.id]}
            end
          end
        end
      end
      changes[id]=copy
      local k=row and "updated" or "inserted"
      counts[k]=counts[k]+1
    end
  end
  state.scope=identity
  for id, row in pairs(changes) do state.orders[id]=row end
  return counts
end

function M.map(state,sku,variant,item_id,barcode)
  assert(state.scope,"sync a seller before mapping")
  sku,variant=text(sku,100),optional(variant,512)
  item_id,barcode=text(item_id,100),text(barcode,100)
  local key=source_key(sku,variant)
  local old=state.mappings[key]
  if old and old.item_id==item_id and old.barcode==barcode then return end
  for other, mapped in pairs(state.mappings) do
    assert(other==key or mapped.barcode~=barcode or mapped.item_id==item_id,
      "barcode is already associated with a different Ledger item")
  end
  for _, row in pairs(state.orders) do
    for _, line in ipairs(row.source.lines) do
      if line.sku and source_key(line.sku,line.variant)==key and (row.picked[line.id] or row.review) then
        error("mapping has existing picks; use a separate reviewed workspace",0)
      end
    end
  end
  state.mappings[key]={item_id=item_id,barcode=barcode}
end
local function local_line(state,line)
  local mapped=line.sku and state.mappings[source_key(line.sku,line.variant)]
  if mapped and line.barcode and mapped.barcode~=line.barcode then return nil,"barcode_conflict" end
  if not mapped then return nil,"unresolved_sku" end
  return mapped
end
local function order(state,id)
  return assert(state.orders[text(id,64)],"unknown marketplace order")
end
local function status(row)
  if row.cancelled then return "cancelled" end
  if row.review then return "needs_review" end
  if not row.source.eligible then return "held" end
  return row.stage
end
local function ready(row)
  assert(status(row)=="open","order is not open for picking")
end

function M.scan(state,id,barcode,event)
  local row=order(state,id)
  barcode,event=text(barcode,100),text(event,100)
  local replay=row.scans[event]
  if replay then
    assert(replay.barcode==barcode,"scan event ID was used with a different barcode")
    assert(replay.epoch==row.pick_epoch,"scan event belongs to an earlier pick")
    return {line_id=replay.line_id,picked=replay.picked,replayed=true}
  end
  ready(row)
  local match
  for _, line in ipairs(row.source.lines) do
    local mapped=local_line(state,line)
    assert(mapped,"order has unresolved SKU or barcode conflicts")
    if mapped.barcode==barcode then
      assert(not match,"ambiguous barcode in this order")
      match=line
    end
  end
  assert(match,"barcode does not belong to this order")
  local count=(row.picked[match.id] or 0)+1
  assert(count<=match.quantity,"this line has already been picked")
  row.picked[match.id]=count
  row.scans[event]={barcode=barcode,line_id=match.id,picked=count,epoch=row.pick_epoch}
  return {line_id=match.id,picked=count,replayed=false}
end
function M.pack(state,id)
  local row=order(state,id)
  if status(row)=="packed" then return end
  ready(row)
  for _, line in ipairs(row.source.lines) do
    assert(local_line(state,line) and row.picked[line.id]==line.quantity,"order is not completely picked")
  end
  row.stage="packed"
end

-- Store an operator-imported carrier PDF verbatim. The checks identify format,
-- not the sender or order; the operator verifies the label in the seller portal.
M.max_label=131072
function M.attach(state,id,key,pdf)
  local row=order(state,id)
  key=text(key,100)
  assert(type(pdf)=="string" and #pdf<=M.max_label and pdf:match("^%%PDF%-%d%.%d")
    and pdf:sub(-2048):match("%%%%EOF%s*$"),"expected an exported carrier-label PDF up to 128 KiB")
  local hex=pdf:gsub(".",function(c) return string.format("%02x",c:byte()) end)
  for other_id, other in pairs(state.orders) do
    for existing, bytes in pairs(other.archived_labels or {}) do
      assert(bytes~=hex and existing~=key,"carrier label was retired and cannot be reassigned")
    end
    for existing, bytes in pairs(other.labels) do
      if other_id==id and existing==key then
        assert(bytes==hex,"label ID already refers to different bytes")
        return
      end
      assert(bytes~=hex and existing~=key,"carrier label already assigned to another package")
    end
  end
  assert(status(row)=="packed","pack the order before attaching a carrier label")
  row.labels[key]=hex
end
function M.label(state,id,key)
  local row=order(state,id)
  assert(status(row)=="packed","order is no longer approved for label reprint")
  local hex=assert(row.labels[text(key,100)],"no such carrier label for this order")
  assert(#hex%2==0 and #hex<=M.max_label*2 and hex:match("^[0-9a-f]+$"),"invalid saved carrier label")
  return (hex:gsub("..",function(chunk) return string.char(tonumber(chunk,16)) end))
end

function M.restart(state,id)
  local row=order(state,id)
  assert(row.review and row.source.eligible and not row.cancelled,"order cannot be restarted")
  row.archived_labels=row.archived_labels or {}
  for key, bytes in pairs(row.labels) do row.archived_labels[key]=bytes end
  row.review,row.stage,row.picked,row.labels=false,"open",{},{}
  row.pick_epoch=row.pick_epoch+1
end

function M.report(state,json)
  local out={scope=state.scope,orders=json.array(),
    finance="unknown: no fee, return or payout feed connected",
    stock="not reconciled: marketplace availability is not physical Ledger stock"}
  for _, id in ipairs(sorted_keys(state.orders)) do
    local row=state.orders[id]
    local item={id=id,remote_status=row.source.status,hold_reason=row.source.hold_reason,
      revision=row.revision,observed_at=row.observed,status=status(row),needs_review=row.review or false,
      lines=json.array(),carrier_labels=json.array(sorted_keys(row.labels)),
      previous_picks=row.previous_picks}
    for _, line in ipairs(row.source.lines) do
      local mapped, why=local_line(state,line)
      item.lines[#item.lines+1]={id=line.id,sku=line.sku,variant=line.variant,quantity=line.quantity,
        picked=row.picked[line.id] or 0,item_id=mapped and mapped.item_id,issue=why}
    end
    out.orders[#out.orders+1]=item
  end
  return out
end

function M.picks(state,json)
  local out={orders=json.array()}
  for _, id in ipairs(sorted_keys(state.orders)) do
    local row=state.orders[id]
    if status(row)=="open" then
      local lines=json.array()
      local resolved=true
      for _, line in ipairs(row.source.lines) do
        local mapped=local_line(state,line)
        if not mapped then resolved=false; break end
        local remain=line.quantity-(row.picked[line.id] or 0)
        if remain>0 then lines[#lines+1]={sku=line.sku,variant=line.variant,barcode=mapped.barcode,quantity=remain} end
      end
      if resolved and #lines>0 then
        out.orders[#out.orders+1]={id=text(state.scope.."/"..id,100),recipient=state.scope,status="open",selected=true,lines=lines}
      end
    end
  end
  return out
end
return M
