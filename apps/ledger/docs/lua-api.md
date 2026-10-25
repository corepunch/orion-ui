# ledger Lua API

<!-- Generated from apps/ledger/ledger-api.xml; do not edit. -->

Ledger's local, trusted Lua API. Function signatures, argument guards, registration, editor annotations and this reference are generated from ledger-api.xml. Record fields come from ledger.orion. All reads return copies. User automation writes only items, counterparties and rates, inside a transaction during an explicitly writable script run. Configuration, posted documents and derived entries are never generic write targets. Existing posting hooks keep their dedicated entry API. No arbitrary downloaded code sandbox is claimed.

## `ledger.find(db, table, id)`

Read one record by its integer ID. Returns nil if it does not exist.

| Argument | Type | Description |
|---|---|---|
| `db` | `string` | Registered database name, normally ib or cfg. |
| `table` | `string` | Schema table name. |
| `id` | `integer` | Record ID. |

**Returns:** `table|nil` — A copy of all stored fields.

```lua
local item = ledger.find("ib", "items", 1)
```

## `ledger.fetch(db, table, field?, value?)`

Read records, optionally filtered by one field's exact value. The filter value is required when field is supplied; integer and string filters are supported.

| Argument | Type | Description |
|---|---|---|
| `db` | `string` | Registered database. |
| `table` | `string` | Schema table. |
| `field` | `string?` | Exact-match field. |
| `value` | `string\|integer?` | Exact-match value. |

**Returns:** `table` — Array of record copies.

## `ledger.field(db, table, id, path)`

Read formatted text of a stored field or a join path. Returns an empty string for a missing record.

| Argument | Type | Description |
|---|---|---|
| `db` | `string` | Database. |
| `table` | `string` | Table. |
| `id` | `integer` | Record ID. |
| `path` | `string` | Field or path such as counterparty.name. |

**Returns:** `string` — Field text.

## `ledger.account(code)`

Find an account in the active configuration by its code.

| Argument | Type | Description |
|---|---|---|
| `code` | `string` | Country-specific account code. |

**Returns:** `table|nil` — Account copy.

## `ledger.entry(entry)`

Add an entry to the document currently being posted. Only available inside posting, closing and post-event hooks. Fields: debit, credit (code or ID), amount, quantity, content, counterparty, item, employee, asset, currency, currency_amount.

| Argument | Type | Description |
|---|---|---|
| `entry` | `table` | Entry fields. |

**Returns:** `nil` — No values.

## `ledger.turnover(account, subconto?, currency?)`

Read cumulative turnovers before the current document plus its entries so far. Only available inside posting context.

| Argument | Type | Description |
|---|---|---|
| `account` | `string\|integer` |  |
| `subconto` | `integer?` | Analytics ID; omitted means all. |
| `currency` | `integer?` | Currency ID; omitted means all. |

**Returns:** `table` — dr, cr, dr_qty, cr_qty, dr_cur, cr_cur.

## `ledger.rate(currency_id, date)`

Read the exchange rate on a date. Zero means no rate was found.

| Argument | Type | Description |
|---|---|---|
| `currency_id` | `integer` |  |
| `date` | `string` | ISO date YYYY-MM-DD. |

**Returns:** `number` — National units per foreign unit.

## `ledger.round2(value)`

Round to two decimal places, halves away from zero.

| Argument | Type | Description |
|---|---|---|
| `value` | `number` |  |

**Returns:** `number` — Rounded value.

## `ledger.log(text)`

Write a diagnostic to stderr with the Ledger script prefix.

| Argument | Type | Description |
|---|---|---|
| `text` | `string` |  |

**Returns:** `nil` — No values.

## `ledger.on(event, callback)`

Register an event subscription in the active configuration's Lua state. Currently the platform emits post; callbacks receive a posting context.

| Argument | Type | Description |
|---|---|---|
| `event` | `string` |  |
| `callback` | `function` |  |

**Returns:** `nil` — No values.

## `ledger.schema(db, table)`

Inspect a table's fields, types, lengths, relations and whether it is an automation write target.

| Argument | Type | Description |
|---|---|---|
| `db` | `string` |  |
| `table` | `string` |  |

**Returns:** `table` — name, writable, fields array (name, type, length, relation).

## `ledger.insert(table, fields)`

Create a catalog record through the Ledger datasource lifecycle. Requires a writable script and an active ledger.transaction. Unknown fields, explicit IDs, invalid types, overlong strings, invalid dates and missing foreign references are rejected.

| Argument | Type | Description |
|---|---|---|
| `table` | `string` | items, counterparties or rates. |
| `fields` | `table` | Typed stored field values, excluding id. |

**Returns:** `table` — Inserted record copy with ID.

## `ledger.update(table, id, fields)`

Patch an existing catalog record through its datasource. Same write restrictions and field validation as insert. An ID cannot be changed. Does not repost historical documents.

| Argument | Type | Description |
|---|---|---|
| `table` | `string` | items, counterparties or rates. |
| `id` | `integer` |  |
| `fields` | `table` | Changed field values. |

**Returns:** `table` — Updated record copy.

## `ledger.transaction(callback)`

Run a callback atomically in the infobase editing context. A thrown Lua error reverts every change in the scope and is rethrown. Returning false is an ordinary successful return, not a rollback request. Nested transactions are supported. Requires an explicitly writable script run; unavailable during posting hooks.

| Argument | Type | Description |
|---|---|---|
| `callback` | `function` |  |

**Returns:** `any` — The callback's return values.

```lua
ledger.transaction(function()
  return ledger.update("items", 1, {price = 150})
end)
```

## `ledger.validate(table, fields, id?)`

Validate a catalog insert or patch without writing. Applies the same schema, value, reference and table restrictions as insert/update, and fills datasource defaults. Supplying id validates an update; omitting it validates an insert. Available in read-only runs.

| Argument | Type | Description |
|---|---|---|
| `table` | `string` | Writable catalog table. |
| `fields` | `table` | Typed values excluding id. |
| `id` | `integer?` | Existing record ID for an update. |

**Returns:** `table` — Validated record copy; a new record has id=0 until insertion.

# Record fields

Generated from `apps/ledger/ledger.orion`. Read methods return these fields as Lua tables.

## `cfg.settings`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `cash_account_id` | `integer` | accounts.id |
| `payroll_account_id` | `integer` | accounts.id |
| `opening_account_id` | `integer` | accounts.id |
| `code` | `string` |  |
| `name` | `string` |  |
| `country` | `string` |  |
| `currency` | `string` |  |
| `currency_code` | `string` |  |
| `currency_units` | `string` |  |
| `currency_gender` | `string` |  |
| `cents_units` | `string` |  |
| `cents_gender` | `string` |  |
| `language` | `string` |  |
| `tax_name` | `string` |  |
| `tax_id_name` | `string` |  |
| `version` | `string` |  |
| `basis` | `string` |  |

## `cfg.subconto_kinds`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `code` | `string` |  |
| `name` | `string` |  |
| `table` | `string` |  |

## `cfg.accounts`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `subconto_kind_id` | `integer` | subconto_kinds.id |
| `code` | `string` |  |
| `name` | `string` |  |
| `kind` | `string` |  |
| `section` | `string` |  |
| `currency` | `boolean` |  |

## `cfg.units`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `code` | `string` |  |
| `name` | `string` |  |

## `cfg.vat_rates`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `revenue_id` | `integer` | accounts.id |
| `expense_id` | `integer` | accounts.id |
| `output_tax_id` | `integer` | accounts.id |
| `input_tax_id` | `integer` | accounts.id |
| `name` | `string` |  |
| `rate` | `double` |  |

## `cfg.doc_types`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `debit_id` | `integer` | accounts.id |
| `credit_id` | `integer` | accounts.id |
| `vat_debit_id` | `integer` | accounts.id |
| `vat_credit_id` | `integer` | accounts.id |
| `cost_debit_id` | `integer` | accounts.id |
| `cost_credit_id` | `integer` | accounts.id |
| `code` | `string` |  |
| `name` | `string` |  |
| `prefix` | `string` |  |
| `form` | `string` |  |
| `base` | `string` |  |
| `vat_book` | `integer` |  |
| `auto_accounts` | `boolean` |  |
| `script` | `string` |  |
| `description` | `string` |  |

## `cfg.payroll_taxes`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `debit_id` | `integer` | accounts.id |
| `credit_id` | `integer` | accounts.id |
| `code` | `string` |  |
| `name` | `string` |  |
| `rate` | `double` |  |
| `rate_above` | `double` |  |
| `cap_month` | `double` |  |
| `cap_year` | `double` |  |
| `allowance_limit` | `double` |  |
| `withheld` | `boolean` |  |
| `allowance` | `boolean` |  |
| `after_withheld` | `boolean` |  |
| `scale` | `string` |  |
| `rounding` | `string` |  |
| `script` | `string` |  |
| `valid_from` | `date` |  |
| `valid_to` | `date` |  |

## `cfg.tax_scales`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `tax_id` | `integer` | payroll_taxes.id |
| `base_from` | `double` |  |
| `rate` | `double` |  |
| `fixed` | `double` |  |
| `curve` | `double` |  |

## `cfg.closing_rules`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `account_id` | `integer` | accounts.id |
| `target_id` | `integer` | accounts.id |
| `via_id` | `integer` | accounts.id |
| `gain_id` | `integer` | accounts.id |
| `loss_id` | `integer` | accounts.id |
| `kind` | `string` |  |
| `start` | `string` |  |
| `script` | `string` |  |
| `name` | `string` |  |

## `cfg.balance_rules`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `account_id` | `integer` | accounts.id |
| `currency_account_id` | `integer` | accounts.id |
| `document` | `string` |  |
| `credit` | `boolean` |  |

## `cfg.print_forms`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `doc_type_id` | `integer` | doc_types.id |
| `report` | `string` |  |
| `name` | `string` |  |
| `template` | `string` |  |
| `basis` | `string` |  |

## `cfg.regulations`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `area` | `string` |  |
| `title` | `string` |  |
| `reference` | `string` |  |
| `valid_from` | `date` |  |
| `url` | `string` |  |

## `catalog.configurations`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `code` | `string` |  |
| `name` | `string` |  |
| `country` | `string` |  |

## `catalog.languages`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `code` | `string` |  |
| `name` | `string` |  |

## `catalog.choices`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `configuration_id` | `integer` | configurations.id |
| `language_id` | `integer` | languages.id |

## `ib.organizations`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `name` | `string` |  |
| `tax_id` | `string` |  |
| `kpp` | `string` |  |
| `address` | `string` |  |
| `bank` | `string` |  |
| `bank_account` | `string` |  |
| `bik` | `string` |  |
| `corr_account` | `string` |  |
| `director` | `string` |  |
| `accountant` | `string` |  |
| `cashier` | `string` |  |

## `ib.counterparties`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `name` | `string` |  |
| `tax_id` | `string` |  |
| `kpp` | `string` |  |
| `address` | `string` |  |
| `bank` | `string` |  |
| `bank_account` | `string` |  |
| `bik` | `string` |  |
| `phone` | `string` |  |

## `ib.items`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `unit_id` | `integer` | cfg.units.id |
| `vat_rate_id` | `integer` | cfg.vat_rates.id |
| `sku` | `string` |  |
| `name` | `string` |  |
| `price` | `double` |  |

## `ib.employees`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `account_id` | `integer` | cfg.accounts.id |
| `name` | `string` |  |
| `position` | `string` |  |
| `tax_id` | `string` |  |
| `hired` | `date` |  |
| `salary` | `double` |  |
| `allowance` | `double` |  |

## `ib.assets`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `account_id` | `integer` | cfg.accounts.id |
| `depreciation_id` | `integer` | cfg.accounts.id |
| `expense_id` | `integer` | cfg.accounts.id |
| `inventory` | `string` |  |
| `name` | `string` |  |
| `cost` | `double` |  |
| `life` | `integer` |  |
| `commissioned` | `date` |  |

## `ib.currencies`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `code` | `string` |  |
| `name` | `string` |  |

## `ib.rates`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `currency_id` | `integer` | currencies.id |
| `date` | `date` |  |
| `rate` | `double` |  |

## `ib.documents`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `doc_type_id` | `integer` | cfg.doc_types.id |
| `counterparty_id` | `integer` | counterparties.id |
| `currency_id` | `integer` | currencies.id |
| `number` | `string` |  |
| `date` | `date` |  |
| `comment` | `string` |  |
| `import_source` | `string` |  |
| `rate` | `double` |  |
| `amount` | `double` |  |
| `vat` | `double` |  |
| `total` | `double` |  |
| `posted` | `boolean` |  |

## `ib.doc_lines`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `document_id` | `integer` | documents.id |
| `item_id` | `integer` | items.id |
| `vat_rate_id` | `integer` | cfg.vat_rates.id |
| `quantity` | `double` |  |
| `price` | `double` |  |
| `amount` | `double` |  |
| `vat` | `double` |  |
| `total` | `double` |  |

## `ib.payroll_lines`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `document_id` | `integer` | documents.id |
| `employee_id` | `integer` | employees.id |
| `amount` | `double` |  |
| `withheld` | `double` |  |
| `contributions` | `double` |  |
| `net` | `double` |  |

## `ib.asset_lines`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `document_id` | `integer` | documents.id |
| `asset_id` | `integer` | assets.id |
| `amount` | `double` |  |

## `ib.entries`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `document_id` | `integer` | documents.id |
| `debit_id` | `integer` | cfg.accounts.id |
| `credit_id` | `integer` | cfg.accounts.id |
| `debit_subconto_id` | `integer` | ib.{debit.subconto_kind.table}.id |
| `credit_subconto_id` | `integer` | ib.{credit.subconto_kind.table}.id |
| `currency_id` | `integer` | currencies.id |
| `date` | `date` |  |
| `quantity` | `double` |  |
| `currency_amount` | `double` |  |
| `amount` | `double` |  |
| `content` | `string` |  |

## `ib.report_settings`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `account_id` | `integer` | cfg.accounts.id |
| `date_from` | `date` |  |
| `date_to` | `date` |  |

## `ib.balances`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `account_id` | `integer` | cfg.accounts.id |
| `subaccount` | `boolean` |  |
| `opening_dr` | `double` |  |
| `opening_cr` | `double` |  |
| `debit_turnover` | `double` |  |
| `credit_turnover` | `double` |  |
| `balance_dr` | `double` |  |
| `balance_cr` | `double` |  |

## `ib.card`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `document_id` | `integer` | documents.id |
| `account_id` | `integer` | cfg.accounts.id |
| `subconto_id` | `integer` | ib.{account.subconto_kind.table}.id |
| `corr_id` | `integer` | cfg.accounts.id |
| `date` | `date` |  |
| `debit` | `double` |  |
| `credit` | `double` |  |
| `balance` | `double` |  |
| `content` | `string` |  |

## `ib.analysis_rows`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `account_id` | `integer` | cfg.accounts.id |
| `subconto_id` | `integer` | ib.{account.subconto_kind.table}.id |
| `opening_dr` | `double` |  |
| `opening_cr` | `double` |  |
| `debit_turnover` | `double` |  |
| `credit_turnover` | `double` |  |
| `balance_dr` | `double` |  |
| `balance_cr` | `double` |  |

## `ib.vat_book`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `document_id` | `integer` | documents.id |
| `counterparty_id` | `integer` | counterparties.id |
| `vat_rate_id` | `integer` | cfg.vat_rates.id |
| `book` | `integer` |  |
| `date` | `date` |  |
| `total` | `double` |  |
| `amount` | `double` |  |
| `vat` | `double` |  |

## `ib.vat_summary`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `vat_rate_id` | `integer` | cfg.vat_rates.id |
| `book` | `integer` |  |
| `documents` | `integer` |  |
| `total` | `double` |  |
| `amount` | `double` |  |
| `vat` | `double` |  |

## `ib.journal_rows`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `document_id` | `integer` | documents.id |
| `debit_id` | `integer` | cfg.accounts.id |
| `credit_id` | `integer` | cfg.accounts.id |
| `debit_subconto_id` | `integer` | ib.{debit.subconto_kind.table}.id |
| `credit_subconto_id` | `integer` | ib.{credit.subconto_kind.table}.id |
| `currency_id` | `integer` | currencies.id |
| `date` | `date` |  |
| `quantity` | `double` |  |
| `currency_amount` | `double` |  |
| `amount` | `double` |  |
| `content` | `string` |  |

## `ib.cash_book`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `document_id` | `integer` | documents.id |
| `corr_id` | `integer` | cfg.accounts.id |
| `subconto_id` | `integer` | ib.{corr.subconto_kind.table}.id |
| `date` | `date` |  |
| `content` | `string` |  |
| `receipt` | `double` |  |
| `expense` | `double` |  |
| `balance` | `double` |  |
| `summary` | `boolean` |  |

## `ib.asset_rows`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `asset_id` | `integer` | assets.id |
| `cost` | `double` |  |
| `monthly` | `double` |  |
| `depreciation` | `double` |  |
| `residual` | `double` |  |

## `ib.payroll_rows`

| Field | XML type | Relation |
|---|---|---|
| `id` | `integer` |  |
| `employee_id` | `integer` | employees.id |
| `opening` | `double` |  |
| `accrued` | `double` |  |
| `deducted` | `double` |  |
| `closing` | `double` |  |
