# Statutory readiness audit

Reviewed 2026-10-28 against the implementation, not an independent legal
certification. No shipped configuration currently has complete statutory
coverage. A passing demo or configuration-integrity test is not evidence of
legal completeness. Russia/Germany 2003 remain historical fixtures.

## Correctness changes in this revision

- Document posting, previews and complete repost runs use nested database
  change scopes. Hook failures restore previous entries rather than leaving
  partial postings. Tax, closing and post-event errors propagate to callers.
- Scripted payroll taxes must return finite numbers; invalid/missing results
  reject posting instead of silently removing a tax.
- Missing foreign-exchange rates and expired referenced tax rates reject
  posting. Tax codes carry treatment and effective dates; legacy empty dates
  remain unrestricted for compatibility, not because they are verified.
- Invoice tax totals come from the invoice's own lines in document currency,
  independent of report periods and posted/draft status.
- Country document types can declare `validate_script`; validation runs inside
  the posting transaction before entry replacement and also applies to previews.

## Work still required before production use

| Priority | Gap | Completion evidence |
|---|---|---|
| P0 | German payroll approximates the tariff and Vorsorgepauschale; employee circumstances and insurance rates are incomplete | Official BMF PAP implementation with exact decimal/rounding semantics; official expected results across all supported employee profiles and dates |
| P0 | Russian cumulative deductions use accrual counts, with incomplete residency/income categories | Explicit tax bases, actual deductions and prior withholding; independent examples for multiple payments per month, thresholds, hires, residency and corrections |
| P0 | Editable posted documents, unrestricted reposting, no closed-period/audit workflow | Immutable posting revisions, reversals, closed-period controls and audit history, with persistence and failure-recovery tests |
| P0 | Money uses binary floating point | Defined decimal/minor-unit representation and currency-specific rounding with migration and independent boundary fixtures |
| P0 | National tax eligibility is not captured fully | Organization registrations, tax/accounting regimes, counterparty jurisdictions, supply/tax/payment dates and recoverability captured and validated |
| P1 | RU printable forms are not FNS electronic formats; DE HTML is not an EN 16931 invoice | Versioned schema-valid output, mandatory fields, official validation artefacts, correction workflows and acceptance tests |
| P1 | DE UStVA and RU VAT summaries cover only selected cases; zero rates do not identify legal reporting categories | Explicit treatment-to-return-box mappings and official expected returns for the supported regimes; no guessed box assignment |
| P1 | DATEV automatic-account exports can duplicate tax on import | A single documented tax strategy verified through an actual compatible importer with reconciliation |
| P1 | Four new countries have development bookkeeping profiles only | Country-specific accounting/recognition coverage, statutory returns, corrections, independent accountant review and production integration approvals |
| P1 | Annual financial statements, income/corporate tax and payroll filings are incomplete | Define legal entity/regime scope, implement each obligation and verify independently |
| P1 | Updates are documented but not a verified release pipeline | Source snapshots, publication/effective dates, schema versions, boundary fixtures, migrations, signed releases and rollback tests |

These gaps are not resolved by relabelling a configuration. Do not promote a
profile to production or describe it as compliant until its supported scope
has the evidence above. Existing RU/DE payroll remains demonstrative; this
revision does not replace it with an official payroll calculation engine.

## New jurisdiction packages

`gb-2026`, `ie-2026`, `nz-2026`, `mt-2026` have separate development infobases,
English terminology, currency, tax rate catalogs, sources, service invoices,
synthetic examples and internal tax working papers. Each `COVERAGE.md` lists
restrictions and omissions. Only the standard-rated domestic service example
is exercised. Reduced/zero rates are catalogued but automatic posting rejects
them pending legal classification. Payroll and statutory filing are not
implemented. No working paper is labelled a statutory return.

## Official sources for implementation and refresh

- German payroll PAP and machine-checking interface:
  https://www.bmf-steuerrechner.de/interface/programmablauf.xhtml
- HMRC VAT API, sandbox and production requirements:
  https://developer.service.hmrc.gov.uk/api-documentation/docs/api/service/vat-api/1.0
- Ireland rates (including the 2026 farmer compensation change):
  https://www.revenue.ie/en/vat/vat-rates/search-vat-rates/current-vat-rates.aspx
- Ireland return schemas, rules and version notes:
  https://revenue-ie.github.io/business-taxes-documentation/
- NZ taxable supply information requirements (changed from 1 April 2023):
  https://www.ird.govt.nz/gst/tax-invoices-for-gst
- NZ developer services: https://www.ird.govt.nz/digital-service-providers
- Malta rates (including the 12% category):
  https://mtca.gov.mt/business-tax/vat1/vat-compliance/vat-rates/vat-rates
- Malta legislation: https://legislation.mt/
- Russia/Germany legal source inventory and known omissions: ../COMPLIANCE.md

Track law, form and API versions separately. Publication date, effective date,
retrieval date and verified supported transaction scope are distinct facts.
Preserve historical IDs and never replace historical values in place.
