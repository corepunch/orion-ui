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
page:load_documents(0); state.busy, state.pending = false, false
pending({{id = 7, date = '2026-04-01', counterparty_id = 4, doc_type_id = 1, posted = false, correction_of = 0},
  {id = 8, date = '2026-04-01', counterparty_id = 4, doc_type_id = 1, posted = true}}, nil)
state.revision = state.revision + 1
assert(not pcall(function() page:load_draft({documents = {id = 7}}) end), 'stale document cache cannot be reviewed')
state.revision = state.revision - 1
assert(not pcall(function() page:load_draft({documents = {id = 8}}) end), 'posted documents cannot be loaded for editing')
page:load_draft({documents = {id = 7}})
assert(page:data().values.draft_edit_title == 'Editing draft ID 7' and page:data().values.counterparty_id == 4)
state.revision = state.revision + 1
assert(page:data().disabled.update and not pcall(function() page:update({document_date = '2026-04-02', counterparty_id = 5}) end), 'new revision requires renewed review')
state.revision = state.revision - 1
page:update({documents = {id = 8}, document_date = '2026-04-02', counterparty_id = 5})
assert(client.command == 'documents.update' and client.args.document_id == 7 and client.args.counterparty_id == 5, 'save targets the reviewed draft even if selection changes')
state.busy, state.pending = false, false; pending({document_id = 7}, nil)
assert(client.command == 'documents.list'); state.busy, state.pending = false, false; pending({}, nil)
assert(page:data().disabled.update, 'saved draft must be selected again')
assert(not pcall(function() page:show_document_lines({}) end), 'document lines require selection')
page:show_document_lines({documents = {id = 7}})
assert(client.command == 'documents.lines' and client.args.document_id == 7)
state.busy = false; state.pending = false
pending({{id = 8, item_id = 2, vat_rate_id = 1, exact_money = true, quantity_milli = '1250', price_minor = '240', total_minor = '357'},
  {id = 9, item_id = 3, vat_rate_id = 1, exact_money = false, quantity = 2.5, price = 3.5, total = 8.5}}, nil)
assert(cache[1].source == 'ledger_remote_cache.document_lines' and cache[1].rows[1].quantity == '1.250' and cache[1].rows[1].price == '2.40' and cache[1].rows[1].total == '3.57')
assert(cache[1].rows[2].quantity:sub(1, 1) == '~' and cache[1].rows[2].total:sub(1, 1) == '~', 'legacy values are marked approximate')
assert(page:data().values.document_lines_title == 'Lines for document ID 7' and page:data().disabled.next_document_lines)
page:add_line({item_id = 2, vat_id = 1, quantity = '1.000', price = '2.00'})
page:replace_lines({documents = {id = 7}})
state.busy = false; state.pending = false; pending({}, nil)
assert(page.document_lines_document == nil and #cache[1].rows == 0, 'replacement clears the old lines before refreshing documents')
state.busy = false; state.pending = false; pending({}, nil)
page.lines = {}; page:publish_lines()
assert(not pcall(function() page:show_posting_history({}) end), 'history requires selection')
page:show_posting_history({documents = {id = 7}})
assert(client.command == 'documents.history' and client.args.document_id == 7 and client.args.offset == 0)
state.busy = false; state.pending = false
pending({{id = 2, recorded_utc = '2026-04-01T00:00:00Z', actor = 'accountant', action = 'post', date = '2026-03-31', number = '7', exact_money = true, total_minor = '1234'}}, nil)
assert(cache[1].source == 'ledger_remote_cache.posting_history' and cache[1].rows[1].total == '12.34')
assert(page:data().values.posting_history_title == 'Posting history for document ID 7')
assert(page:data().disabled.previous_posting_history and page:data().disabled.next_posting_history)
assert(not pcall(function() page:find_posting_history({history_document_id = -1}) end), 'search requires positive ID')
page:find_posting_history({history_document_id = 999})
assert(client.command == 'documents.history' and client.args.document_id == 999)
state.busy = false; state.pending = false; pending({}, nil)
assert(page:data().values.posting_history_title == 'Posting history for document ID 999')
assert(cache[1].source == 'ledger_remote_cache.posting_history' and #cache[1].rows == 0)
assert(not pcall(function() page:show_revision_entries({}) end), 'snapshot selection is required')
page:show_revision_entries({posting_history = {id = 2}})
assert(client.command == 'documents.history_entries' and client.args.revision_id == 2)
state.busy = false; state.pending = false
pending({{id = 4, date = '2026-03-31', debit_id = 1, credit_id = 2, exact_money = true, quantity_milli = '1250', amount_minor = '1234', content = 'test'}}, nil)
assert(cache[1].source == 'ledger_remote_cache.revision_entries' and cache[1].rows[1].amount == '12.34' and cache[1].rows[1].quantity == '1.250')
assert(page:data().values.revision_entries_title == 'Entries for posting snapshot ID 2')
page:show_history({}); assert(client.command == 'period.history' and client.args.offset == 0)
state.busy = false; state.pending = false
pending({{id = 1, previous_through = '', closed_through = '2026-03-31', recorded_utc = '2026-04-01T00:00:00Z', actor = 'admin'}}, nil)
assert(cache[1].source == 'ledger_remote_cache.period_history' and cache[1].rows[1].actor == 'admin')
assert(page:data().disabled.previous_history and page:data().disabled.next_history)
page:add_line({item_id = 1, vat_id = 1, quantity = '1.000', price = '10.00'})
assert(cache[1].rows[1].item_id == 1, 'line is projected for review')
page:create({type_id = 1, draft_party_id = 1, draft_date = '2026-04-01'})
assert(client.command == 'documents.create'); local original = client.args
local callback = page.pending_callback
assert(not pcall(function() page:post({documents = {id = 7}}) end), 'a second action must be refused while busy')
assert(page.pending_callback == callback, 'refusal preserves the original completion callback')
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
page:items({}); state.busy, state.pending = false, false
pending({{id = 42, name = 'Goods', sku = 'G1', unit_id = 1, vat_rate_id = 2}}, nil)
state.revision = state.revision + 1
assert(not pcall(function() page:load_catalog_entry({catalog = {id = 42}}) end), 'stale catalog cache cannot be reviewed')
state.revision = state.revision - 1
page:load_catalog_entry({catalog = {id = 42}})
assert(page:data().values.catalog_edit_title == 'Editing item ID 42' and page:data().values.sku == 'G1')
page:update_catalog_entry({name = 'Goods revised', sku = 'G2', unit_id = 1, catalog_vat_id = 2})
assert(client.command == 'items.save' and client.args.id == 42 and client.args.fields.sku == 'G2')
state.busy, state.pending = false, false; pending({id = 42}, nil)
assert(client.command == 'catalogs.list'); state.busy, state.pending = false, false; pending({}, nil)
assert(page:data().disabled.update_catalog_entry, 'saved entry must be selected again')
page:counterparties({}); state.busy, state.pending = false, false
pending({{id = 7, name = 'Party', tax_id = '123'}}, nil)
page:load_catalog_entry({catalog = {id = 7}}); state.revision = state.revision + 1
assert(page:data().disabled.update_catalog_entry and not pcall(function() page:update_catalog_entry({name = 'Changed'}) end), 'revision change requires a new selection')
state.revision = state.revision - 1
page:update_catalog_entry({name = 'Changed', tax_id = '456'})
assert(client.command == 'counterparties.save' and client.args.id == 7 and client.args.fields.tax_id == '456' and client.args.fields.country_code == nil)
state.busy, state.pending = false, false; pending({id = 7}, nil)
state.busy, state.pending = false, false; pending({}, nil)
for _, country in ipairs({'RU', 'DE'}) do
  page:init(); state.pending, state.busy, state.conflict, state.revision = false, false, false, 10
  page:connect({}); state.busy = false; pending({application = 'ledger', country = country, role = 'admin'}, nil)
  state.busy, state.pending = false, false; pending({}, nil)
  page:prepare_close({close_date = '2026-03-31'})
  assert(not page:data().disabled.close_period, country .. ': reviewed close is enabled')
  page:remember({close_date = '2026-04-30'})
  page:remember({close_date = '2026-03-31'})
  assert(page:data().disabled.close_period, country .. ': changing and restoring the date requires review')
  page:prepare_close({close_date = '2026-03-31'}); state.revision = 11
  assert(page:data().disabled.close_period, country .. ': changed revision requires review')
  assert(not pcall(function() page:close_period({close_date = '2026-03-31'}) end))
  page:prepare_close({close_date = '2026-03-31'}); page:connect({})
  state.busy = false; pending({application = 'ledger', country = country, role = 'admin'}, nil)
  state.busy, state.pending = false, false; pending({}, nil)
  assert(page:data().disabled.close_period, country .. ': reconnect requires review')
  page:prepare_close({close_date = '2026-03-31'}); page:close_period({close_date = '2026-03-31'})
  assert(client.command == 'period.close' and client.args.date == '2026-03-31')
  local close_args = client.args
  state.busy = false; pending(nil, 'connection lost')
  assert(page:data().disabled.close_period and not page:data().disabled.retry)
  page:retry({}); assert(client.args == close_args, country .. ': retry preserves the close request')
  state.busy, state.pending = false, false; pending({}, nil)
  assert(client.command == 'documents.list' and page:data().disabled.close_period)
  state.busy, state.pending = false, false; pending({}, nil)
  page:prepare_close({close_date = '2026-04-30'}); page:close_period({close_date = '2026-04-30'})
  state.busy, state.pending, state.conflict = false, false, true; pending(nil, 'stale revision')
  state.conflict = false
  assert(page:data().disabled.close_period, country .. ': conflict invalidates review')
end
print('Ledger remote controller tests passed')
