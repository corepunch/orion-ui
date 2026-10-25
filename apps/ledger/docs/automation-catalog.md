# Local automation catalog

<!-- Generated from apps/ledger/automation.xml; do not edit. -->

Local Lua recipes and typed nodes for Ledger. XML owns IDs, ports, parameters, tags, aliases and descriptions; generated catalog.lua is consumed by the Lua runtime. CSV means UTF-8 CSV, not XLSX. A preview never writes; apply is explicit. Match normalization trims whitespace and folds ASCII and Russian Cyrillic case; it is not full Unicode normalization. Missing match keys are rejected. Counterparty matching requires INN; an explicitly empty KPP is allowed for entrepreneurs.

## Node `source.rows` — Rows

Use rows supplied as Lua tables.

Tags: source, rows

Ports: `none` → `rows`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `rows` | `table` | yes | Array of records. |

## Node `source.table` — Read table

Read record copies through ledger.fetch.

Tags: source, table

Ports: `none` → `rows`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `table` | `string` | yes | Schema table name. |
| `db` | `string` | no | Database, default ib. |

## Node `parse.csv` — Parse CSV

Parse UTF-8 CSV with quoted delimiters, escaped quotes, CRLF and multiline cells.

Tags: parse, csv

Ports: `text` → `rows`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `delimiter` | `string` | no | Single separator, default semicolon. |

## Node `rows.map` — Map columns

Rename columns and convert typed values; invalid conversions fail.

Tags: rows, map

Ports: `rows` → `rows`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `columns` | `table` | yes | Source column to stored field. |
| `types` | `table` | no | Target field to number or integer. |

## Node `rows.filter` — Filter rows

Keep rows matching every exact equality condition.

Tags: rows, filter

Ports: `rows` → `rows`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `equals` | `table` | yes | Field/value conditions. |

## Node `rows.group` — Group rows

Count rows and optionally sum a numeric field by nonempty keys.

Tags: rows, group

Ports: `rows` → `rows`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `keys` | `table` | yes | Grouping fields. |
| `sum` | `string` | no | Numeric field to sum. |

## Node `check.required` — Check required fields

Return row IDs, field names and required issue codes.

Tags: check, required

Ports: `rows` → `issues`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `fields` | `table` | yes | Fields that must be nonempty. |

## Node `rows.match` — Match records

Match by composite key; ambiguous stored matches fail.

Tags: rows, match

Ports: `rows` → `matches`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `table` | `string` | yes | Infobase table. |
| `keys` | `table` | yes | Composite key fields. |

## Node `plan.upsert` — Plan import

Prepare inserts/updates with before snapshots; duplicate input keys fail.

Tags: plan, upsert

Ports: `rows` → `plan`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `table` | `string` | yes | Target table. |
| `keys` | `table` | yes | Match keys. |
| `existing_only` | `boolean` | no | Return not_found errors instead of inserts. |

## Node `plan.patch` — Plan field replacement

Plan a field replacement on selected catalog records.

Tags: plan, patch

Ports: `rows` → `plan`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `table` | `string` | yes | Writable table. |
| `field` | `string` | yes | Target field. |
| `value` | `any` | yes | Typed replacement value. |

## Node `check.duplicates` — Find duplicates

Report candidate groups; never merge records automatically.

Tags: check, duplicates

Ports: `rows` → `rows`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `keys` | `table` | yes | Matching fields. |

## Node `report.stock` — Stock balances

Aggregate entries through a cutoff date on exact selected item-analytic accounts; date_from is intentionally not used.

Tags: report, stock

Ports: `rows` → `rows`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `accounts` | `table` | yes | Exact account codes; include each required subaccount. |
| `date_to` | `string` | no | Inclusive ISO cutoff date. |

## Node `report.settlements` — Counterparty settlements

Compute opening, period debit/credit and closing by counterparty for selected accounts.

Tags: report, settlements

Ports: `rows` → `rows`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `accounts` | `table` | yes | Exact counterparty-analytic account codes. |
| `date_from` | `string` | no | Inclusive ISO start date. |
| `date_to` | `string` | no | Inclusive ISO end date. |
| `counterparty_id` | `number` | no | Optional counterparty ID. |

## Node `check.month` — Check period

Report unposted documents and missing exchange rates; not an exhaustive accounting audit.

Tags: check, month

Ports: `rows` → `issues`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `date_from` | `string` | yes | Inclusive ISO date. |
| `date_to` | `string` | yes | Inclusive ISO date. |

## Node `format.csv` — Format CSV

Return CSV text; the caller chooses whether and where to save it.

Tags: format, csv

Ports: `rows` → `text`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `columns` | `table` | yes | Ordered output fields. |
| `delimiter` | `string` | no | Single separator, default semicolon. |

## Node `source.text` — Text

Use supplied text, for example UTF-8 CSV.

Tags: source

Ports: `none` → `text`.

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `text` | `string` | yes | Input text. |

## Recipe `import.items` — Import items

Map sku/name/price columns, match by SKU and preview catalog inserts/updates. Excel files must first be saved as CSV.

Tags: товары, импорт, CSV, Excel

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `rows` | `table` | no | Input rows; supply rows or csv. |
| `csv` | `string` | no | UTF-8 CSV text; supply rows or csv. |
| `columns` | `table` | no | Input header to target field mapping. |
| `delimiter` | `string` | no | CSV separator, default semicolon. |

## Recipe `import.counterparties` — Import counterparties

Map name/tax_id/kpp columns and match by INN plus KPP; requires INN and allows an explicitly empty KPP.

Tags: контрагенты, импорт, ИНН, КПП

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `rows` | `table` | no | Input rows; supply rows or csv. |
| `csv` | `string` | no | UTF-8 CSV text; supply rows or csv. |
| `columns` | `table` | no | Input header to target field mapping. |
| `delimiter` | `string` | no | CSV separator, default semicolon. |

## Recipe `import.rates` — Import exchange rates

Map currency_id/date/rate columns and match by currency and date.

Tags: валюта, курсы, импорт

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `rows` | `table` | no | Input rows; supply rows or csv. |
| `csv` | `string` | no | UTF-8 CSV text; supply rows or csv. |
| `columns` | `table` | no | Input header to target field mapping. |
| `delimiter` | `string` | no | CSV separator, default semicolon. |

## Recipe `prices.update` — Update prices

Update current item prices by SKU; unknown SKUs are plan errors. Historical documents are unchanged.

Tags: товары, цены, прайс, CSV

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `rows` | `table` | no | Input rows; supply rows or csv. |
| `csv` | `string` | no | UTF-8 CSV text; supply rows or csv. |
| `columns` | `table` | no | Input header to target field mapping. |
| `delimiter` | `string` | no | CSV separator, default semicolon. |

## Recipe `duplicates.items` — Find duplicate items

Report candidate item duplicates by SKU, or supplied keys.

Tags: товары, дубли, проверка

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `keys` | `table` | no | Override the documented composite match keys. |

## Recipe `duplicates.counterparties` — Find duplicate counterparties

Report candidates by INN/KPP, or supplied keys; empty keys are skipped.

Tags: контрагенты, дубли, проверка

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `keys` | `table` | no | Override the documented composite match keys. |

## Recipe `duplicates.documents` — Find duplicate documents

Report candidates by type, counterparty, number, date and total; no automatic deletion.

Tags: документы, дубли, проверка

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `keys` | `table` | no | Override the documented composite match keys. |

## Recipe `check.required` — Check required fields

Return empty required fields for a chosen infobase table.

Tags: реквизиты, проверка, ошибки

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `table` | `string` | yes | Infobase table. |
| `fields` | `table` | yes | Required field names. |

## Recipe `fields.replace` — Replace catalog field

Filter catalog rows by exact equalities and preview replacement of one field.

Tags: реквизиты, изменение, заполнение

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `table` | `string` | yes | Writable catalog table. |
| `equals` | `table` | yes | All exact filter conditions. |
| `field` | `string` | yes | Stored field to replace. |
| `value` | `any` | yes | Typed replacement value. |

## Recipe `report.stock` — Check stock balances

Read item-analytic entries on selected account codes; report quantity, value and negative flag.

Tags: товары, остатки, проверка

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `accounts` | `table` | yes | Exact account codes with the correct analytics kind. |
| `date_from` | `string` | no | Inclusive ISO start date (not used for stock). |
| `date_to` | `string` | no | Inclusive ISO cutoff date. |

## Recipe `report.settlements` — Counterparty settlements

Report opening, period movements and closing on selected counterparty-analytic accounts. No contract or overdue analysis is implied.

Tags: контрагенты, долги, сверка

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `accounts` | `table` | yes | Exact account codes with the correct analytics kind. |
| `date_from` | `string` | no | Inclusive ISO start date (not used for stock). |
| `date_to` | `string` | no | Inclusive ISO cutoff date. |
| `counterparty_id` | `integer` | no | Optional counterparty filter. |

## Recipe `check.month` — Check before closing

Check period for unposted documents and missing exchange rates. Does not perform closing or repair accounting automatically.

Tags: закрытие, документы, проверка

| Parameter | Type | Required | Meaning |
|---|---|---|---|
| `date_from` | `string` | no | Inclusive ISO start date (not used for stock). |
| `date_to` | `string` | no | Inclusive ISO cutoff date. |

## `ledger.automation.search(query?, tags?, kind?)`

Search documented recipe/node titles, aliases and tags. Query terms use AND matching, tags use exact AND matching; kind is recipe or node. Returns catalog metadata.

Returns: `table`.

| Parameter | Type | Meaning |
|---|---|---|
| `query` | `string` | Text query; empty means all. |
| `tags` | `table` | Exact tag array. |
| `kind` | `string` | recipe or node. |

## `ledger.automation.preview(id, params?)`

Run a recipe without writes. Import/replacement recipes return a change plan; checks and reports return result arrays.

Returns: `table`.

| Parameter | Type | Meaning |
|---|---|---|
| `id` | `string` | Recipe ID from this XML catalog. |
| `params` | `table` | Recipe parameters documented below. |

## `ledger.automation.execute(graph)`

Validate a typed acyclic node graph, then evaluate its output. Graph nodes have id/type/input/params; graph.output names the result node. No apply node exists: graph evaluation never commits data.

Returns: `table|string`.

| Parameter | Type | Meaning |
|---|---|---|
| `graph` | `table` | Graph data with nodes and output. |

## `ledger.automation.apply(plan)`

Apply a prepared change plan atomically. Refuses plan errors, stale snapshots, changed match keys and duplicate targets. Requires a writable host run. Catalog API enforces types and references even for hand-written plans.

Returns: `table`.

| Parameter | Type | Meaning |
|---|---|---|
| `plan` | `table` | Plan returned by preview or a plan node. |

## `ledger.automation.register(descriptor, nodes, recipes)`

Register an XML-generated plugin descriptor and its Lua implementations. Standard and custom plugins use the same registry; duplicate plugin, node or recipe IDs are rejected before registration.

Returns: `nil`.

| Parameter | Type | Meaning |
|---|---|---|
| `descriptor` | `table` | Generated metadata or Lua implementation table. |
| `nodes` | `table` | Generated metadata or Lua implementation table. |
| `recipes` | `table` | Generated metadata or Lua implementation table. |
