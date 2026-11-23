# Importing 1С:Управление нашей фирмой (УНФ) catalogs via OData

This is an opt-in, read-only connector for the **standard 1С OData v3 interface**.
It has been exercised against synthetic fixtures, not a live УНФ tenant.
Metadata and user permissions vary by configuration, release and publication.
The adapter lives in Lua; it is not run by Ledger at startup or while posting.

## Prepare the source

1. Use a trusted HTTPS publication of your УНФ application. For 1С:Фреш,
   follow the [official OData setup guide](https://1cfresh.com/articles/data_odata).
   Create a dedicated read-only service identity and expose only the three
   catalogs you intend to migrate. Expose an accumulation register only if
   you also want its stock snapshot. Do not use an administrator login or
   grant write/web-service rights beyond those the publisher requires for reads.
2. Create a private, host-specific netrc file (owner-only permissions on Unix,
   e.g. `chmod 600 /path/to/unf.netrc`). Add a `machine` entry for exactly
   the trusted hostname with its service login/password. Do not use a
   `default` entry. Keep the file out of Git, shared folders and logs. The
   process receives the credential file **path**, never the password on its
   command line or in JSON. Remove or rotate the credential when finished.
3. On macOS or Linux, use Lua 5.4 and curl. From the repository root, make an
   `unf.json` with an application URL or the full service URL:

   ```json
   {"endpoint": "https://1c.example.test/a/your-app/12345"}
   ```

   Use the publication URL without a language suffix, credentials, query or
   fragment. The transport sends only HTTPS GET, validates TLS by default and
   does not follow HTTP redirects. OData page links must remain on the exact
   same HTTPS service/resource. Do not work around these checks by providing
   an untrusted endpoint or disabling certificate verification.

## Discover, export, and import

```sh
export ORION_UNF_NETRC=/private/path/unf.netrc
lua5.4 apps/ledger/tools/unf_odata.lua discover /private/path/unf.json
umask 077
lua5.4 apps/ledger/tools/unf_odata.lua catalogs /private/path/unf.json > /private/path/unf-stage.xml
```

Use the export only if the command exits successfully. Source data is not
printed incrementally; errors (including authentication, invalid fields or
too many pages) abort instead of producing a partial stage. A shell redirect
can leave an empty file on failure; do not overwrite your last good export.
Prefer a read-only period when the source is not changing. OData catalog
pagination is not a cross-request database snapshot.

The discovery output lists exposed catalog and register names and their
declared entity fields. Defaults are `Catalog_Номенклатура`,
`Catalog_Контрагенты`, and `Catalog_Склады`. Some publications use structural
units for stock locations. Those require confirmation of the exact mapping,
not an automatic interpretation of every structural unit as a warehouse.
To opt in to a published mapping, set for example
`"catalogs": {"warehouses": "Catalog_СтруктурныеЕдиницы"}` **only after**
verifying that the published catalog contains the intended stock locations.
All three catalogs are required for the combined export. Optional limits
are `page_size` (1–500, default 250) and `max_pages` (1–1,000 per resource,
default 200); each stream is limited to 50,000 rows.

Inspect the XML and import it through Ledger's existing **File → Import from
1C** in a backed-up or disposable infobase first. The tagged stage accepts only
items (name/SKU), counterparties (name/INN/KPP), and warehouses (name).
Folders and deleted records are not imported when those fields are published.
Imports run in the existing local database transaction. Existing records
matched by source GUID, SKU, or INN are held with a report, not replaced
or adopted; re-import does not undo local edits or duplicate known source
IDs. Records with the same display name but distinct identities are allowed.
Catalogs are not kept in sync: no remote deletes, manual collision merging,
price/VAT/unit mapping, contacts, orders, financial documents, or postings.

## Preview warehouse quantities

Populate `stock` with your publication's **verified** accumulation register
and output fields. The following names are illustrative, not defaults:

```json
{
  "endpoint": "https://1c.example.test/a/your-app/12345",
  "stock": {
    "register": "AccumulationRegister_StockFixture",
    "period": "2026-11-01T00:00:00",
    "dimensions": ["Item", "Warehouse"],
    "item_field": "Item_Key",
    "warehouse_field": "Warehouse_Key",
    "quantity_field": "QuantityBalance",
    "unit": "pcs"
  }
}
```

```sh
lua5.4 apps/ledger/tools/unf_odata.lua stock /private/path/unf.json
```

The connector calls the register's read-only `Balance` function with an
explicit timestamp in the tenant's time zone. Include every dimension needed
to keep quantities meaningful (for example organization, variant or unit).
Use `unit_field` and its dimension instead of `unit` when the register supplies
units; a shared label is appropriate only when all selected quantities are
already measured in that unit. A reference-valued unit is preserved as returned,
not translated or converted. The result contains item/warehouse GUIDs,
dimension values, unit and signed quantity, without valuation or posting.
Never load that result as Ledger opening stock or infer current availability
from it. Compare a small sample to the tenant's standard report before relying
on the snapshot.

## Existing Ledger scope and next steps

- Warehouse catalogs and document warehouse selection already exist. Ledger's
  stock accounting is item/account based, not per-warehouse; the OData snapshot
  does not change accounting calculations.
- Ledger has document printing/export with templates. Dedicated barcode
  scanning and regulatory/carrier formats are not implemented. See the
  [internal item-label workflow](item-labels.md).
- Keep the connector, label templates, scanner workflows, and warehouse-report
  presentation in Lua. Keep posting, reversal and typed database integrity in
  the core, with a narrow reusable API before adding per-warehouse valuation.
- A later UI add-on can use this module with a supported secure HTTPS binding
  and a review step. This patch provides the command-line source adapter and
  the existing Ledger import UI, not a new network settings screen.

## Validation

Run `lua5.4 apps/ledger/tests/unf_odata_test.lua` or the normal Ledger native
test suite. The latter also checks the existing infobase importer with the
tagged handoff. Before calling this production-ready, connect to a test УНФ
tenant, verify metadata/resource names, compare each catalog count and a
warehouse balance sample, then import to a disposable infobase twice.
