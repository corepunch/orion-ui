# Local Lua automation

Ledger ships 12 recipes and 16 typed node operations, implemented entirely in
Lua. Both use the same public Ledger API; C supplies reflection, validation and
transactional datasource operations. There is no 1C adapter, online service or
visual graph editor in this version. The graph is an executable Lua data value
with typed edges, ready for a future editor.

## Standard and custom plugins

The shipped automation entry is `share/stdplugs/10-automation.lua`, with its
implementation in `share/stdplugs/automation/`. Ledger scans `.lua` entry scripts
in `stdplugs` and in `<infobase>/plugins` through the same loader, in filename
order. Standard plugins load first, then legacy configuration plugins, then
custom plugins. Restart the runtime after changing installed plugins.

`ledger.automation.register(descriptor, nodes, recipes)` registers a plugin's
XML-generated descriptor and its Lua implementation tables. The standard
package uses exactly this interface. IDs must be unique; collisions are errors.
To author a custom recipe/node package, use `automation.xml` or the small test
fixture `tests/fixtures/custom-plugin.xml` as a manifest reference, then run:

```sh
python3 tools/generate_automation_catalog.py my-plugin.xml --lua my-catalog.lua --docs my-plugin.md
```

A custom entry script loads that generated descriptor and passes its implementation
functions to `register`. `impl=` in XML names the function in the implementation
table; registration mappings and public Lua argument wrappers are generated.
There is no dependency resolver, plugin manager UI or live unloading yet. The
[architecture plan](docs/script-first-architecture.md) describes moving the
remaining built-in accounting and UI to standard plugins.

## Run

From the repository root:

```sh
make ledger-api build/bin/ledger-run
build/bin/ledger-run --data /tmp/my-ledger --script apps/ledger/share/stdplugs/automation/examples/search.lua
build/bin/ledger-run --data /tmp/my-ledger --script apps/ledger/share/stdplugs/automation/examples/preview-import.lua
build/bin/ledger-run --data /tmp/my-ledger --script apps/ledger/share/stdplugs/automation/examples/graph.lua
```

The headless host defaults to `ru-2026`; use `--config de-2026`, `ru-2003` or
`de-2003` as needed. The default resources are `apps/ledger/share` relative to the
working directory; `--share DIR` selects installed resources (for example
`/opt/orion/share/ledger`). `--data` is mandatory. A missing infobase starts
empty, without demo data. Existing infobases are opened through `ledger_ib`.

Writes require both an explicit `apply`/transaction in Lua and `--write`:

```sh
build/bin/ledger-run --data /tmp/my-ledger --script apps/ledger/share/stdplugs/automation/examples/apply-import.lua --write
```

Repeating this example with unchanged data changes zero records. Read-only runs
never save an infobase. Script errors revert the whole run, including earlier
nested commits; write runs save only after successful script completion. A
nonzero exit code reports an error. Always inspect preview errors before apply.

Scripts are trusted local code and have standard Lua libraries. Read-only mode
restricts the Ledger mutation API; it is not a filesystem or operating-system
sandbox for arbitrary Lua. No third-party package installation is provided.

## Recipes, search and nodes

- [Generated recipe/node catalog and automation API](docs/automation-catalog.md)
- [Generated Ledger API and record schema reference](docs/lua-api.md)
- [Runnable examples](share/stdplugs/automation/examples)

`ledger.automation.search(query, tags, kind)` returns metadata suitable for a
catalog UI. Titles and descriptions are English; Russian search aliases and tags
are supplied in XML. Every query term and every selected tag must match. Search
and keys trim surrounding whitespace and fold ASCII/Russian Cyrillic case; they
do not implement stemming, transliteration or full Unicode normalization.

`preview(id, params)` returns a plan for imports/replacements, or rows/issues for
reports/checks. `apply(plan)` rejects any plan errors, stale before snapshots,
changed/ambiguous match keys and duplicate targets, then uses a datasource
transaction. Inserts and patches are schema-validated already during preview.
Failed writes roll back the entire apply. Records are copied across the Lua
boundary; mutating a read result alone never edits the database.

Graphs contain `nodes = {{id, type, input, params}, ...}` and an `output` node ID.
Declaration order is arbitrary. Before evaluation, the runtime validates all
node IDs, dependencies, cycles, port types and declared required parameters.
Only ancestors of the output are evaluated. There is deliberately no mutation
node: a graph produces a plan, and the caller explicitly applies it.

## Current boundaries

- Writable tables: `items`, `counterparties`, `rates`. Schema defaults, foreign
  references, date/number/string validation and datasource lifecycle apply.
- CSV is UTF-8 CSV (default semicolon), including quoted delimiters, multiline
  cells, escaped quotes, BOM and CRLF. XLSX is not supported; save as CSV first.
- Item imports match SKU; counterparties match INN/KPP (empty KPP is allowed,
  empty INN is not); rates match currency ID and date. Duplicate keys fail.
  These IDs are local to the selected infobase. Column mapping can include
  `unit_id`/`vat_rate_id`, which must refer to the selected configuration.
- Duplicate detection only proposes candidates. No automatic merging/deletion.
- Updating a current item price does not change historical document lines.
- Stock and settlement reports require exact account codes, including each
  desired subaccount; account analytics kinds are checked. Stock includes all
  entries through `date_to`, not just the period's movements. Settlement results
  separate opening, period debit/credit and closing. No warehouse, contract,
  payment-allocation or overdue model is invented.
- Month checks currently cover unposted documents and missing currency rates;
  they are not an exhaustive check of accounting or legal compliance.
- No generic writes to documents, posted entries, configuration or derived
  reports. Posting hooks keep their dedicated `ledger.entry` interface.
- Plans are in-memory Lua tables, not a durable audit log or cross-process lock.
  Ledger's XML infobase still requires a single writer. Lua arithmetic uses the
  host's existing number representation; no new decimal money type is claimed.

## XML is the source of truth

`ledger-api.xml` defines the C-backed Lua functions: names, implementation
symbols, argument types/optionality, return types, descriptions and examples.
`tools/generate_lua_api.py` generates registration and argument guards, editor
annotations and Markdown. Record fields/relations come directly from the
existing database XML in `ledger.orion`.

`automation.xml` defines recipe/node IDs, titles, descriptions, tags, aliases,
parameters, node port types and the Lua automation API documentation. `impl=` mappings and API argument wrappers are also generated.
`tools/generate_automation_catalog.py` generates the runtime metadata, Lua registration/API glue and
Markdown reference. Business implementations remain Lua in `nodes.lua` and
`init.lua`; neither the runtime nor documentation maintains a second catalog.

```sh
make ledger-api                 # regenerate bindings, metadata and reference
make ledger-api-check           # fail if generated outputs drift from XML
make build/bin/test_ledger_automation_test build/bin/test_ledger_test
build/bin/test_ledger_automation_test
build/bin/test_ledger_test
```

Generated Markdown (including the site pages), editor annotations and catalog metadata are versioned;
the generated C binding header stays under `build/generated`. After editing
XML or `ledger.orion`, regenerate and include changed documentation. Do not edit
generated outputs by hand. C implementation bodies implement the contract;
argument checks and registration are generated, not handwritten duplicates.

Future work is recorded in [ROADMAP.md](ROADMAP.md). The
[script-first architecture audit](docs/script-first-architecture.md) maps the
remaining C functionality to Lua/`.orion` and identifies the integration work
with Orion's newer sandboxed page/model host. The local runner currently uses
Ledger's trusted hook state; those hosts are not yet unified.
