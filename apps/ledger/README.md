# Ledger

![Ledger document journal](../../docs/screenshots/ledger_journal.jpg)

Double-entry bookkeeping in the spirit of the early-2000s desktop accounting
suites: a document journal with posting, catalogs of counterparties and goods,
a chart of accounts, an entry journal, a trial balance and printed legal forms.

```bash
make build/bin/ledger share
build/bin/ledger                       # last selected configuration (ru-2003)
build/bin/ledger --config ru-2003 --data /tmp/books
```

## Platform and configuration

Ledger separates the **platform** from the **configuration**, the way 1C:Enterprise
separates its engine from a country's typical configuration.

| Layer | Where | What it holds |
|---|---|---|
| Platform | `ledger.orion`, `*.c` | Schemas, screens, menus, posting engine, print engine |
| Configuration | `share/configurations/<code>/` | Chart of accounts, units, tax rates, document types with posting rules, print forms |
| Infobase | `<settings>/ledger/<code>/infobase.xml` | The organization's counterparties, items, documents, entries and totals |

The platform never names an account, a tax rate or a national form. A document
type in the configuration says which accounts it debits and credits, whether
amounts include tax, and which print forms it has. Supporting another country
means adding a configuration directory, not changing code.

`File > Select Configuration...` lists `share/configurations/index.xml`; each
configuration opens its own infobase on the next start. A new infobase starts
from the configuration's `demo.xml` and is posted on first open.

## Languages

The interface is written in English and translated by string catalogs
([docs/localization.md](../../docs/localization.md)). Three layers stack:

1. the framework's catalog (`share/locale/` → message boxes, file picker);
2. Ledger's own, `share/locale/ru.xml` — every menu, form, column and message;
3. the configuration's, `configurations/<code>/locale/` — national terms. The
   platform says "Tax ID" and "Tax"; `ru-2003` turns them into "ИНН" and "НДС"
   in Russian and "INN" and "VAT" in English.

The language is the one chosen in `File > Language...`, otherwise the
configuration's (`ru` for `ru-2003`), otherwise the system's (`LANG`).
`--lang ru|en|qps-ploc` overrides it for one run; `qps-ploc` is the
pseudo-locale for checking layouts. Numbers and dates follow the catalog:
`16 656,00` and `10.03.2003` in Russian.

![Ledger in Russian](../../docs/screenshots/ledger_document.jpg)

After changing texts in `ledger.orion`, refresh the catalog and translate the
new entries:

```bash
build/bin/orionc --input apps/ledger/ledger.orion --strings apps/ledger/share/locale/ru.xml
```

## Adding a configuration

1. Copy `share/configurations/ru-2003` to `share/configurations/<code>`.
2. Edit `configuration.xml`: `settings`, `accounts`, `units`, `vat_rates`,
   `doc_types` (posting rules reference account ids) and `print_forms`.
3. Write the print templates in `print/` (HTML, see below) and a `demo.xml`
   infobase, or leave the demo empty.
4. Add a `<configuration>` row to `index.xml`.
5. Set `language` in `settings` and, if the country has its own terms, add
   `locale/<language>.xml` catalogs that rename the platform's neutral ones.

Print templates are HTML files filled from the document:

| Tag | Value |
|---|---|
| `{{number}}`, `{{total}}`, `{{counterparty.name}}` | Document fields and join paths |
| `{{doc_type.debit.code}}` | Joins across databases (infobase → configuration) |
| `{{org.tax_id}}`, `{{cfg.currency}}` | Organization details, configuration settings |
| `{{#lines}} {{n}} {{item.unit.code}} {{/lines}}` | One block per document line |
| `{{date\|dmy}}` | ISO date as DD.MM.YYYY |

`Documents > Print Forms` writes every form of the document's type to
`<infobase dir>/print/` for printing from a browser.

## How it is built

Almost everything is declared in `ledger.orion`:

- Three `db_schema_xml` databases — `cfg` (configuration), `ib` (infobase) and
  `catalog` (installed configurations). The schema-driven class needs no
  adaptor code: CRUD, filtering, persistence and join paths such as
  `item.unit.name` come from the generated schema. Relations may cross
  databases (`relation="cfg.accounts.id"`).
- A page host with seven pages; master–detail tables (documents → lines and
  entries) are linked by `relation=` alone.
- Database dialogs for documents, lines, counterparties, items and the
  organization. Numeric fields bind through typed DDX; ComboBoxes bind by id.

The C code holds what declarations cannot express: posting rules
(`posting.c`), the print engine (`print.c`), and command routing plus the
document's line buttons (`controller.c`).

## First configuration: Russia, 2003

`ru-2003` follows the rules in force in 2003:

- Chart of accounts per Order of the Ministry of Finance of 31.10.2000 No. 94n,
  with the subaccounts typical desktop configurations of the time used
  (19.03, 41.01, 60.01, 62.01, 68.02, 90.01–90.09, ...).
- VAT 20% (Tax Code art. 164; reduced to 18% from 01.01.2004 by Federal Law
  No. 117-FZ of 07.07.2003), plus 10%, 0% and "without VAT".
- Print forms: goods waybill TORG-12 (Goskomstat Resolution No. 132 of
  25.12.1998), VAT invoice (Government Resolution No. 914 of 02.12.2000, with
  the 2002 column set), cash receipt and payment orders KO-1/KO-2 (Goskomstat
  Resolution No. 88 of 18.08.1998), payment order 0401060 (Bank of Russia
  Regulation No. 2-P of 03.10.2002) and a free-form invoice for payment.

Simplifications: no cost-of-sales write-off (90.02) on sales, advances
received are kept on 62.01 rather than 62.02, and amounts in words are left
for the signer to write in, as on hand-filled forms.
