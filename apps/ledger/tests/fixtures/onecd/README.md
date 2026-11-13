`fake_1cd.py` is from [onec-converter](https://github.com/dadosh1984/onec-converter),
revision `485f0069fddb5c820353681e6830607f59f47f95`, under the accompanying
MIT license. It generates synthetic physical 1CD files without company data.
`catalog.xml` is generated from its `_Reference42` fixture by
`tools/inspect_1cd.py`; the Python test verifies the exact bytes and the C test
imports it through Ledger's existing transaction-based importer.
