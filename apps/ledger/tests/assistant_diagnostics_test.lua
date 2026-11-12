local D = require('diagnostics')

local counterparties = {
  {id=9,name='Same Name',country_code='RU',tax_id='  AB-12 '},
  {id=3,name='Same Name',country_code='ru',tax_id='ab-12'},
  {id=5,name='Blank One',tax_id=''},
  {id=6,name='Blank Two',tax_id='   '},
  {id=4,name='Unique',country_code='DE',tax_id='XY-9'},
  {id=7,name='Different Key, Same Name',country_code='DE',tax_id='XY-10'},
  {id=11,name='Repeated Tax ID',country_code='RU',tax_id=' XY-9 '},
  {id=12,name='Different Name',country_code='de',tax_id='xy-9'},
  {id=13,name='Twin Names',country_code='RU',tax_id='77'},
  {id=14,name='Twin Names',country_code='RU',tax_id='78'},
}
local rows, groups = D.find(counterparties, 'counterparties')
assert(#groups == 2 and groups[1].key == 'ru:ab-12' and groups[1].value == 'ab-12')
assert(groups[1].field == 'tax_id' and groups[2].field == 'tax_id' and groups[2].key == 'de:xy-9')
assert(#rows == 4 and rows[1] == counterparties[1] and rows[2] == counterparties[2]
  and rows[3] == counterparties[5] and rows[4] == counterparties[8])
assert(rows[5] == nil)
assert(groups[1].rows[1] == counterparties[1] and groups[1].rows[2] == counterparties[2])
assert(groups[2].rows[1] == counterparties[5] and groups[2].rows[2] == counterparties[8])
assert(counterparties[1].duplicate_key == nil and counterparties[1].tax_id == '  AB-12 ')

local items = {
  {id=1,name='Pen',sku=' P-1 '}, {id=2,name='Pen copy',sku='p-1'},
  {id=3,name='Same name',sku='p-2'}, {id=4,name='Same name',sku='p-3'},
  {id=5,name='No code',sku=''},
}
rows, groups = D.find(items, 'items')
assert(#groups == 1 and groups[1].field == 'sku' and groups[1].key == 'p-1')
assert(#rows == 2 and rows[1] == items[1] and rows[2] == items[2])
assert(#D.find(items, 'documents') == 0)
assert(#D.find({}, 'items') == 0)

-- Detection runs on the complete source set before ordinary context filters.
-- Refining can only remove candidates; it cannot make a distinct key duplicate.
local refined = {}
for _, row in ipairs(D.find(counterparties, 'counterparties')) do
  if row.name == 'Same Name' then refined[#refined + 1] = row end
end
assert(#refined == 2 and refined[1] == counterparties[1] and refined[2] == counterparties[2])
local narrow = {}
for _, row in ipairs(D.find(counterparties, 'counterparties')) do
  if row.id == 4 then narrow[#narrow + 1] = row end
end
assert(#narrow == 1 and narrow[1] == counterparties[5])

print('assistant duplicate diagnostics: normalized identifiers, stable groups, empty keys and scoped refinements passed')
