# Ireland 2026 — development profile

Reviewed: 2026-10-28. Status: **development, not production compliance**.

The working subset is domestic standard-rated service purchases and sales,
invoice-basis recognition, amounts excluding tax, full input-tax recovery,
bank receipts/payments, manual journals, and invoice-local tax totals.
The invoice date must equal the supply/tax point date. These are explicit
scope restrictions, not automatic eligibility determinations. The caller must
verify domestic status, registration, full recovery and matching tax point.
The current schema cannot independently establish those facts.

The supplied chart is a Ledger chart, not a government-mandated chart.
All sample entities, addresses and identifiers are synthetic.

Other published rates are catalogued for future implementation, but automatic
posting rejects them until classification and reporting rules are implemented.
Foreign currency, negative lines/credit notes and dates outside 2026 are rejected.
No payroll document types or guessed payroll rates are installed.

Not implemented: special schemes, cash/payment basis, partial exemption,
imports/exports, reverse charge, cross-border supplies, separate tax points,
credit notes, payroll, asset tax depreciation, annual accounts, income/corporate
tax, statutory return generation, electronic submission and receipts.
Manual journals are not a statutory filing workflow.

## Official update sources

- Tax rates and guidance: https://www.revenue.ie/en/vat/vat-rates/search-vat-rates/current-vat-rates.aspx
- Filing integration: https://revenue-ie.github.io/business-taxes-documentation/

Monitor legislation and authority guidance as well as schema/API releases.
Preserve old rate IDs, add effective-dated revisions, verify boundary dates,
and validate official return mappings before claiming filing support.
