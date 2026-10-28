# United Kingdom 2026 — development profile

Reviewed: 2026-10-28. Status: **development, not production compliance**.

The working subset is domestic standard-rated service purchases and sales,
invoice-basis recognition, amounts excluding tax, full input-tax recovery,
bank receipts/payments, manual journals, and invoice-local tax totals.
Supply and tax-point dates are entered explicitly; the tax point drives tax-book
periods and rate validity. Supplier/customer country and VAT/GST registration
are stored separately. These records do not independently prove eligibility or
full input recovery. The caller must establish the actual tax point and regime.
The national invoice validates required fields and reconciled amounts; see
[the form audit](../../../docs/invoice-forms-audit.md) for its official-source audit.

The supplied chart is a Ledger chart, not a government-mandated chart.
All sample entities, addresses and identifiers are synthetic.

Other published rates are catalogued for future implementation, but automatic
posting rejects them until classification and reporting rules are implemented.
Foreign currency, negative lines/credit notes and dates outside 2026 are rejected.
No payroll document types or guessed payroll rates are installed.

Not implemented: special schemes, cash/payment basis, partial exemption,
imports/exports, reverse charge, cross-border supplies, automatic time-of-supply determination,
credit notes, payroll, asset tax depreciation, annual accounts, income/corporate
tax, statutory return generation, electronic submission and receipts.
Manual journals are not a statutory filing workflow.

## Official update sources

- Tax rates and guidance: https://www.gov.uk/vat-rates
- Filing integration: https://developer.service.hmrc.gov.uk/api-documentation/docs/api/service/vat-api/1.0

Monitor legislation and authority guidance as well as schema/API releases.
Preserve old rate IDs, add effective-dated revisions, verify boundary dates,
and validate official return mappings before claiming filing support.
