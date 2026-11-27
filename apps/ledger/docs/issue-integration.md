# Ledger backlog integration and acceptance

The Ledger application keeps ownership of accounting entries and balances.
Marketplace and provider data never become entries just because an operator
fetched them. Workshop is an optional local workflow, not a new application
and not a prerequisite for the RU or DE accounting configurations.

| Issue | Implemented in this change | Still required before closing |
| --- | --- | --- |
| [#335](https://github.com/corepunch/orion-ui/issues/335), [#345](https://github.com/corepunch/orion-ui/issues/345) | Includes the #347 file import, catalog/analytics and attachment work, plus auditable source VAT register and book evidence. | Platform 8.0/8.1 and server extraction, native operator walkthrough and real-user migration, deletion/security policy and broader register mapping. Historical VAT evidence is not a statutory Ledger report. |
| [#348](https://github.com/corepunch/orion-ui/issues/348) | [Independent report and volume reconciliation](1c-report-reconciliation.md), including exact cents and physical byte checks. | Independent reports from the pinned 1C snapshots and at least one actual non-empty external volume with verified provenance. |
| [#343](https://github.com/corepunch/orion-ui/issues/343), [#359](https://github.com/corepunch/orion-ui/issues/359) | [Native warehouse stock](warehouse-stock.md), explicit reservation/cancellation, transfers/reversals, guarded posting and read-only balance service. | Stock operator UI and cross-product consumption, partial/multi-line fulfillment, physical counts, cost by location, actual supported camera and carrier API validation. |
| [#360](https://github.com/corepunch/orion-ui/issues/360) | [Review-only bank and EDI exchange and settlement reconciliation](provider-exchange.md), plus Workshop invoice intents for review. | Approved invoice issuance and accounting, durable reconciliation and state, provider test accounts, inbound posting policy and signed outbound EDI. |
| [#364](https://github.com/corepunch/orion-ui/issues/364) | [Desktop snapshot export to a local order workbench](marketplaces.md): scoped persistence, SKU mapping, scanning by code, picking/packing, carrier PDF association and byte-preserving reprint. | Camera and live carrier API, native reservation integration, marketplace write validation, commission/returns feeds and pilot. |
| [#350–#355](https://github.com/corepunch/orion-ui/issues/350), [#363](https://github.com/corepunch/orion-ui/issues/363) | [Trusted Ledger service composition](business-modules.md), stable role-filtered commands and discovery. | External package installation/trust, composable data migrations and forms, durable cross-store events, shared CRM writes and Lodge adapters. |
| [#356–#358](https://github.com/corepunch/orion-ui/issues/356), [#361](https://github.com/corepunch/orion-ui/issues/361) | [Workshop pilot](workshop.md): vehicle/ownership history, schedules, estimates and work orders, actual materials, recorded/billable work, print and CSV. | Desktop integration, shared persistent transactions, local timezone/DST presentation, role-scoped exports, statutory invoices, native stock adapter and pilot validation. |
| [#349](https://github.com/corepunch/orion-ui/issues/349), [#362](https://github.com/corepunch/orion-ui/issues/362), [#365](https://github.com/corepunch/orion-ui/issues/365) | Ledger-side components, no Lodge product change. | App-specific Lodge work and real operator pilots; these are not claimed by the Ledger PR. |

Validate on disposable data before activating any integration. Compare stock
before and after a transfer, a cancelled hold and posting/reversing a sale.
Review unresolved marketplace SKUs and totals; never infer stock from an
external availability response. For bank and EDI review, use a provider test
account and do not approve posting from a match suggestion alone.
