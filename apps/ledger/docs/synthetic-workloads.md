# Large synthetic RU/DE workloads

These fixtures exercise the shipped **ru-2026** and **de-2026** configurations.
All organizations, people, addresses and catalog names are explicitly marked
`SYNTHETIC`. Tax identifiers use invalid `TEST-*` placeholders; bank details
and personal identifiers are deliberately absent. Never submit their output
to a bank, tax authority or payroll service.

The original small `demo.xml` files remain the fast first-start examples.
The separately downloaded 26 MB Russian EnterpriseData 1.24 export described
in [the README](../README.md#real-enterprisedata-export-for-validation) is
real-company **importer** validation input, not a synthetic fixture and not a
German dataset. This generator produces Ledger-native XML, not EnterpriseData,
and does not use or copy that export or the demo organizations.

## Create an infobase

Run from the repository root with Python 3.9+, the normal Ledger build
dependencies and the platform submodule initialized:

```sh
git submodule update --init
make build/bin/ledger-fixture-check OPT=-O2 -j2
python3 apps/ledger/tools/generate_large_infobase.py --country ru --size medium --output /tmp/ledger-ru-10000
build/bin/ledger-fixture-check ru-2026 /tmp/ledger-ru-10000 10000 --materialize
python3 apps/ledger/tools/generate_large_infobase.py --country de --size medium --output /tmp/ledger-de-10000
build/bin/ledger-fixture-check de-2026 /tmp/ledger-de-10000 10000 --materialize
```

Use a new directory for each run. Generation refuses to replace an existing
`infobase.xml`. Sizes are `small` (1,000 documents), `medium` (10,000), and
`large` (50,000); `--documents N` overrides the size (100–1,000,000).
`--seed 2026` is the default. Identical source configuration, generator,
country, count and seed produce byte-identical raw XML. `synthetic.json`
records the inputs and raw SHA-256; its state describes the **raw generation**,
not subsequent materialization. Configuration changes may change results.
Generated XML and benchmark output are not checked into Git.

The second command is required: the C materializer calculates invoices,
posts them through the real engine, fills net salary payments **after** each
month's accrual, posts closing documents, saves, reopens and checks reports.
It only supports these generated fixtures (contiguous chronological IDs),
not arbitrary or real infobases. It prints CPU seconds for each operation.
Do not open the raw intermediate XML as a completed accounting example.

Open the materialized result using the usual application build:

```sh
make ledger share OPT=-O2 -j2
build/bin/ledger --config ru-2026 --lang ru --data /tmp/ledger-ru-10000
build/bin/ledger --config de-2026 --lang de --data /tmp/ledger-de-10000
```

## Workload and accounting scope

Each country contains twelve months of purchases and sales at both standard
and reduced VAT rates, bank settlements, 12 gross payrolls and net payouts,
12 period closings and a commissioned depreciating asset. Catalog cardinality
scales to 1,000 counterparties, 2,000 items and 50 employees; the 10,000-document
case contains 500 counterparties, 1,000 items and 50 employees. Document count
includes opening, payroll and closing documents. End-of-month partial trade
cycles deliberately leave some unsettled invoices/stock.

RU uses the shipped chart (41.01/60.01, 62.01/90, 19.03/68.02, 51, 70),
explicit input VAT deductions, average-cost sales, progressive payroll and
month closing. DE uses the shipped SKR 03 automatic accounts per VAT rate,
1400/1600 settlements, 1200 bank, payroll deductions/contributions and AfA.
Rates and tax formulas are owned by the configurations, not reimplemented
by the generator. See [COMPLIANCE.md](../COMPLIANCE.md) for their scope and
simplifications. These are trading-company stress fixtures, not exhaustive
statutory accounting examples. They omit real identity/bank validation,
foreign currency, returns and special tax regimes; existing targeted tests
cover other behaviors.

## Automated checks and performance

```sh
make ledger-large-test OPT=-O2 -j2
# Faster iteration or a larger benchmark, using the same integration checker:
python3 apps/ledger/tools/check_large_infobases.py --documents 1000
python3 apps/ledger/tools/check_large_infobases.py --documents 50000 --timeout 1800
python3 apps/ledger/tests/large_infobase_test.py
```

The integration harness uses disposable directories outside the checkout and
runs both countries. It verifies generation determinism, document counts and
references, loading, engine posting, entries for every document, valid debit
and credit accounts, finite nonnegative amounts, annual and June trial-balance debit/
credit turnover equality and equal opening/closing sides, zero net payroll
debt, VAT/journal/account-card report output, independently calculated invoice
net/VAT/gross amounts,
ID/number lookup (including a missing number), saving and reopening. It compares
every persisted field across another process's save/reopen and a second posting
pass (movement IDs may be regenerated by reposting; their accounting fields
must match). Transient reports must stay out of the saved XML. Each paired entry
carries the same amount on its debit and credit side; the independent report
sums also detect omitted/duplicated account aggregation.

The checker prints CPU times for load, posting, reports, 100 searches, save,
and reopen/report generation. The harness prints raw/saved sizes and total
wall time. Its default timeout is 600 seconds **per process**; timing values
are observations, not machine-independent performance promises. CI runs the
10,000-document scenario for each country. The 50,000-document case is an
opt-in stress run to avoid inflating routine CI time.

Headless checks exercise the database/report/search paths used by the UI.
They do not measure interactive scrolling, painting, startup to first frame,
or printed-page layout. For a manual UI smoke check, open each materialized
base, find an early/middle/late document by number, inspect invoice/payroll/
closing entries, select a month and the year in Reports, save, exit and reopen.
