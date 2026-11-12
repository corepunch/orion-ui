local json = dofile('share/lua/orion/json.lua')
local saved, callbacks, sent, sequence, loads = nil, {}, {}, 0, 0
local native = {}
function native.newId() sequence = sequence + 1; return 'request-' .. sequence end
function native.outboxLoad() loads = loads + 1; return saved end
function native.outboxSave(body) if saved and saved ~= body then return nil, 'outbox occupied' end; saved = body; return true end
function native.outboxClear(body) if saved ~= body then return nil, 'outbox mismatch' end; saved = nil; return true end
function native.request(_, method, body, callback)
  sent[#sent + 1] = {method = method, body = body}; callbacks[#callbacks + 1] = callback; return #sent
end
local function finish(status, body, error)
  local callback = table.remove(callbacks, 1); assert(callback); callback(status, body, error)
end
package.loaded['orion.remote_native'] = native
package.loaded['orion.json'] = json
package.loaded.orion = {tr = function(text) return text end}
package.loaded['orion.db'] = {}
package.loaded['orion.model'] = {}
local remote = dofile('share/lua/orion/remote.lua')
local wrong, rejected, before = remote.new(6, 'ledger'), nil, loads
assert(wrong:refresh(function(r, e) rejected = not r and e end))
finish(200, '{"protocol":1,"application":"lodge","revision":1,"role":"admin"}')
assert(rejected and loads == before and not wrong:submit('payment', {}, function() end))
local function status(client)
  local result, failure
  assert(client:refresh(function(r, e) result, failure = r, e end))
  finish(200, '{"protocol":1,"application":"lodge","revision":1,"role":"admin"}')
  assert(result and not failure)
end
local client = remote.new(7, 'lodge'); status(client)
local result, failure
assert(client:submit('payment', {id = 9, amount_minor = 500, request_key = 'payment-key'}, function(r,e) result,failure=r,e end))
local original = saved; assert(original == sent[#sent].body)
finish(0, nil, 'timeout'); assert(failure and client:state().pending and saved == original)
assert(not client:refresh(function() end))
assert(client:retry(function(r,e) result,failure=r,e end)); assert(sent[#sent].body == original)
finish(200, '{"ok":true,"revision":2,"request_id":"wrong","result":{}}')
assert(client:state().pending and saved == original)
-- A new client recovers the exact persisted request before allowing mutations.
local recovered = remote.new(8, 'lodge'); status(recovered); assert(recovered:state().pending)
assert(recovered:retry(function(r,e) result,failure=r,e end)); assert(sent[#sent].body == original)
finish(429, '{"error":"busy"}'); assert(recovered:state().pending and saved == original)
assert(recovered:retry(function(r,e) result,failure=r,e end))
finish(200, '{"ok":true,"revision":2,"request_id":"request-1","result":{"id":9}}')
assert(result.id == 9 and not failure and not recovered:state().pending and not saved and recovered:state().revision == 2)
assert(recovered:submit('check_in', {id = 9}, function(r,e) result,failure=r,e end))
local stale = saved; finish(409, '{"error":"stale"}'); assert(recovered:state().conflict and saved == stale)
status(recovered); assert(not saved and not recovered:state().conflict)
