package.path = 'apps/ledger/share/remote/?.lua;' .. package.path
local pending, state = nil, {pending = false, busy = false, conflict = false, revision = 0}
local client = {}
function client:state() return state end
function client:refresh(callback) pending = callback; state.busy = true; return true end
function client:submit(command, args, callback) self.command, self.args = command, args; pending = callback; state.pending = true; state.busy = true; return true end
function client:retry(callback) pending = callback; state.busy = true; return true end
local cache = {}
local decimal_separator = '.'
package.preload['orion'] = function() return {tr = function(x) return x end, assert = assert, today = function() return '2026-04-01' end, formatNumber = function(n) return tostring(n):gsub("%.", decimal_separator) end, formatDate = function(s) return s end} end
package.preload['orion.remote'] = function() return {new = function(handle, app) assert(handle == 1 and app == "ledger"); return client end, replace_many = function(rows) cache = rows end} end
package.preload['orion.json'] = function() return {array = function(t) return t end} end
local page = require('remote_ledger'); page.app = {handle = 1}; page:init(); page:connect({})
state.busy = false; pending({application = 'ledger', country = 'RU', role = 'accountant'}, nil)
assert(client.command == 'documents.list'); state.busy = false; state.pending = false; pending({}, nil)
page:add_line({item_id = 1, vat_id = 1, quantity = '1.000', price = '10.00'})
assert(cache[1].rows[1].item_id == 1, 'line is projected for review')
page:create({type_id = 1, draft_party_id = 1, draft_date = '2026-04-01'})
assert(client.command == 'documents.create'); local original = client.args
state.busy = false; pending(nil, 'connection lost'); assert(page:data().disabled.create)
page:retry({}); assert(client.args == original); state.busy = false; state.pending = false; pending({document_id = 1}, nil)
assert(client.command == 'documents.list' and #page.lines == 0)
state.busy = false; state.pending = false; pending({}, nil)
page.fields.name = 'Preserved name'; page:remember({quantity = '2.000'})
assert(page.fields.name == 'Preserved name', 'tab changes preserve draft input')
local ok = pcall(function() page:post({}) end); assert(not ok, 'posting requires selection')
page:post({documents = {id = 7}}); assert(client.command == 'documents.post' and client.args.document_id == 7)
state.busy = false; state.pending = false; state.conflict = true; pending(nil, 'stale revision')
assert(page:data().disabled.post and not page:data().disabled.connect, 'conflict requires refresh')
state.conflict = false; page.role = 'auditor'; assert(page:data().disabled.create and page:data().disabled.post, 'auditor cannot mutate')
page.role = 'accountant'; assert(page:data().disabled.close_period, 'accountant cannot close')
page:init(); page:connect({}); state.busy = false; state.pending = true; client.command = 'saved'
pending({application = 'ledger', country = 'DE', role = 'admin'}, nil)
assert(client.command == 'saved' and not page:data().disabled.retry, 'restored request must be retried before listing')
page:retry({}); state.busy = false; state.pending = false; pending({document_id = 9}, nil)
assert(client.command == 'documents.list', 'restored retry refreshes the document list')
state.busy = false; state.pending = false; pending({}, nil)
decimal_separator = ','; page:add_line({item_id = 1, vat_id = 1, quantity = '1,250', price = '2,40'})
assert(page.lines[1].quantity == '1.250' and page.lines[1].price == '2.40', 'localized input becomes exact wire decimals')
assert(cache[1].rows[1].quantity == '1,250', 'staged line uses locale decimal separator')
assert(not pcall(function() page:add_line({item_id = 1, vat_id = 1, quantity = '1.250', price = '2,40'}) end), 'misplaced separator is rejected')
print('Ledger remote controller tests passed')
