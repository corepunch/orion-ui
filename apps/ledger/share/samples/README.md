# Large synthetic Ledger samples

These committed files are entirely synthetic and can be redistributed. They
contain no data from the real-company export referenced in the app README.
They complement the small configuration demos, which remain unchanged.

| File | Configuration | Counterparties | Items | Documents | Entries / opening rows |
|---|---|---:|---:|---:|---:|
| `ru-2003-large.xml` | ru-2003, RUB | 500 | 2,000 | 1,000 | 10,000 |
| `de-2003-large.xml` | de-2003 / SKR 03, EUR | 500 | 2,000 | 1,000 | 10,000 |
| `ru-2003-enterprisedata-large.xml` | ru-2003, RUB + USD | 500 | 2,000 | 4 source documents → 1 opening operation | 10,000 |
| `de-2003-enterprisedata-large.xml` | de-2003 / SKR 03, EUR + USD | 500 | 2,000 | 4 source documents → 1 opening operation | 10,000 |

## Open a native sample

Run from the repository root. Use a new directory so your books are not replaced:

```sh
make build/bin/ledger share
sample_dir=$(mktemp -d /tmp/orion-ledger-ru-XXXXXX)
cp apps/ledger/share/samples/ru-2003-large.xml "$sample_dir/infobase.xml"
build/bin/ledger --config ru-2003 --lang ru --data "$sample_dir"
```

For Germany, replace `ru-2003-large.xml`, `--config ru-2003`, and `--lang ru`
with `de-2003-large.xml`, `--config de-2003`, and `--lang de`.

The native samples have invented Russian/German catalog names and 1,000 manual
operations across 2003. The operations model stock purchases, supplier payments,
sales and customer payments. Each entry is 100.00, each document totals 1,000.00,
and full-year debit and credit turnover are both 1,000,000.00. These are load/UI
examples, not comprehensive tax/VAT or statutory accounting examples. Small
payroll and asset catalogs are copied from the existing synthetic country demos.

## Import the EnterpriseData samples

Choose the matching configuration, then use `File > Import from 1C
(EnterpriseData)...` and select the corresponding `*-enterprisedata-large.xml`.
These are synthetic importer fixtures; the German variant tests SKR 03 mapping
and EUR as the national currency, not a claim of export from German software.
EnterpriseData element names remain Russian in both files.

For an empty infobase, expect 500 counterparties, 2,000 items, one USD currency,
one posted opening operation and 10,000 entries. A base seeded with the small
demo already has catalog rows, so its total counts will be larger. Each exchange
contains 2,500 rows each of bank balances, supplier debts, customer debts and
stock. Of the bank rows, 250 are USD with a fixed national/foreign ratio of 2;
all stock rows carry quantity 10. National debit is 1,184,325.00 and national
credit is 343,737.50. Every row posts against the configuration's opening account
(000 / 9000); total debit and credit turnover both equal 1,528,062.50.

Reimport, including after saving/reloading: expect no new entries or catalog
objects. Import fixtures and native infobases are alternative examples, not data
to combine in a production infobase.

## Reproduce and verify

```sh
python3 apps/ledger/tests/generate_large_samples.py --check
make build/bin/test_ledger_large_test
build/bin/test_ledger_large_test
```

Run the generator without `--check` to reproduce all four XML files byte for
byte. The test loads both native samples, recalculates their postings, checks
balanced reports and save/reload, then imports each country's exchange into an
empty base. It checks counts, national/currency amounts, goods analytics and
quantities, balanced reports and repeat import after reload. `make test` also
includes this test.
