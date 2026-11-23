# Find documents containing an item

Use this read-only Lua recipe to find receipts, sales, and other Ledger
documents that contain your item, then see **all product lines** of each
matching document. This is useful when investigating what else was bought or
sold with the item, instead of seeing only the line that matched. The report
includes draft and posted documents unless you ask for posted only.

The recipe implements the filtering pattern in
[research task 73](1c-programmer-tasks-research-051-150.md#73-отбор-документов-по-одной-строке-с-выводом-всей-табличной-части)
for Ledger's item references: match one or more lines to select a document,
then return its current product rows without applying the item filter again.
Date filters apply to the Ledger document date, not an invented line date.
It does not inspect other tabular parts such as payroll or past revisions.

## Run

From the checkout root, against your local Ledger book:

```sh
LEDGER_SKU=0007 build/bin/ledger-run --data /path/to/book \
  --script apps/ledger/share/stdplugs/automation/examples/item-documents.lua
```

Use the exact SKU as stored, including leading zeroes. Set `LEDGER_ITEM_ID`
instead to disambiguate a duplicate SKU. You can also set
`LEDGER_DATE_FROM=2026-11-01`, `LEDGER_DATE_TO=2026-11-30` (inclusive), and
`LEDGER_POSTED_ONLY=1`. No `--write` option is needed. Output is TSV: the
last column flags the matching line(s); other lines from that document stay
in the output, with their own item, SKU and quantity. The current stored
labels come from the item catalog, not a historical label snapshot.

## Find by tags or call from Lua

```lua
local A = ledger.automation
local hits = A.search("номенклатура", {"документы"}, "recipe")
local groups = A.preview("report.item_documents", {
  sku = "0007", date_from = "2026-11-01", posted_only = true
})
for _, group in ipairs(groups) do
  print(group.document.number, #group.lines, group.matching_lines)
end
```

Alternatively use the English tags `documents` / `items`, or German
`Belege` / `Artikel`. The underlying item ID can be passed as `item_id`
instead of `sku`. The action returns grouped record copies, not a change
plan, and cannot be applied to the database.

Test from the repository root with
`lua apps/ledger/tests/item_documents_test.lua`.
